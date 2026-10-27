/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Nav3DManager.h"

#include "Log.h"
#include "Map.h"
#include "PlayerbotAIConfig.h"
#include "StringFormat.h"
#include "Timer.h"
#include "World.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <queue>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr float kMinCellSize = 0.5f;
    constexpr float kMaxCellSize = 4.0f;
    constexpr float kXYPad = 20.0f;
    constexpr float kZPad = 15.0f;
    // The grid is sparse (A* only visits reachable cells), so a wider coordinate bound costs
    // nothing until it is searched. Uldis is ~263 yd in Y and ~171 yd down; at 0.5-yd detail
    // cells that no longer fits in 512.
    constexpr int32 kMaxCellsPerAxis = 2048;
    constexpr float kSurfaceMargin = 0.5f;
    constexpr float kFloorMargin = 0.25f;  // navigability: keep tight tunnel cells connected
    constexpr float kCapsuleFloorClearance = 0.5f;  // final body clearance
    constexpr float kTileBodyRadius = 0.5f;  // tile clearance gate (0.25-yd units: 2)
    constexpr float kHardFloorClearance = -0.05f;  // final path may touch the floor, not pass it
    constexpr uint32 kNeighborsCardinal = 6;
    constexpr uint32 kNeighborsEdge = 18;
    constexpr uint32 kNeighborsFull = 26;
    constexpr uint32 kHardMaxNodes = 2000000;
    constexpr uint32 kHardMaxTimeMs = 10000;
    constexpr uint32 kTimeCheckMask = 0x7F;
    constexpr float kDuplicateEpsilon = 0.05f;
    constexpr float kPartialAcceptDistance = 5.0f;  // interaction range
    constexpr float kEarlyPartialDistance = 2.0f;   // stop as soon as a chest is in reach
    // A lazy 3D route this dense is a sign the search wandered through a huge open volume.
    // Reject it before the O(n^2) smoothing and capsule pass can stall the map thread.
    constexpr size_t kMaxPathPoints = 2000;
    // Bound the smoothing candidate scan: a full O(n^2) pass over a long cell chain can block
    // the map thread for minutes. Chunked smoothing still removes most cell stair-stepping.
    constexpr size_t kMaxSmoothSkip = 64;
    struct GridNode
    {
        int32 X = 0;
        int32 Y = 0;
        int32 Z = 0;
    };

    struct NeighborOffset
    {
        int8 X = 0;
        int8 Y = 0;
        int8 Z = 0;
        uint8 Axes = 0;
    };

    bool operator==(GridNode const& left, GridNode const& right)
    {
        return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
    }

    uint64_t Pack(GridNode const& node)
    {
        return (uint64_t(uint32(node.X)) << 42) | (uint64_t(uint32(node.Y)) << 21) | uint64_t(uint32(node.Z));
    }

    struct GridNodeHash
    {
        size_t operator()(GridNode const& node) const { return std::hash<uint64_t>()(Pack(node)); }
    };

    struct NodeState
    {
        float G = std::numeric_limits<float>::max();
        float F = std::numeric_limits<float>::max();
        GridNode Parent;
        bool Closed = false;
        bool HasParent = false;
        int8 Nav = 0;
    };

    struct QueueNode
    {
        float F = 0.0f;
        GridNode Node;
    };

    bool operator>(QueueNode const& left, QueueNode const& right)
    {
        return left.F > right.F;
    }

    struct Grid
    {
        Map* MapPtr = nullptr;
        uint32 Phase = 0;
        float Cell = 1.0f;
        float MinX = 0.0f;
        float MinY = 0.0f;
        float MinZ = 0.0f;
        bool RestrictSky = false;
        Nav3DTile const* Tile = nullptr;
        std::shared_ptr<Nav3DTile> SolidTile;
        int32 SizeX = 0;
        int32 SizeY = 0;
        int32 SizeZ = 0;

        Position Center(GridNode const& node) const
        {
            return Position(MinX + (float(node.X) + 0.5f) * Cell, MinY + (float(node.Y) + 0.5f) * Cell,
                            MinZ + (float(node.Z) + 0.5f) * Cell);
        }

        bool Contains(GridNode const& node) const
        {
            return node.X >= 0 && node.Y >= 0 && node.Z >= 0 && node.X < SizeX && node.Y < SizeY &&
                   node.Z < SizeZ;
        }

        GridNode Snap(Position const& position) const
        {
            int32 const x = int32((position.GetPositionX() - MinX) / Cell);
            int32 const y = int32((position.GetPositionY() - MinY) / Cell);
            int32 const z = int32((position.GetPositionZ() - MinZ) / Cell);
            return {std::clamp(x, 0, SizeX - 1), std::clamp(y, 0, SizeY - 1),
                    std::clamp(z, 0, SizeZ - 1)};
        }
    };

    std::shared_ptr<Nav3DTile> ResolveSolidTile(Map* map, Position const& position)
    {
        if (!map)
            return nullptr;

        constexpr float gridSize = 533.3333f;
        uint16 const tileX =
            uint16(std::max<int32>(0, int32(std::floor(32.0f - position.GetPositionX() / gridSize))));
        uint16 const tileY =
            uint16(std::max<int32>(0, int32(std::floor(32.0f - position.GetPositionY() / gridSize))));
        std::shared_ptr<Nav3DTile> tile =
            Nav3DManager::Instance().GetTile(map->GetId(), tileX, tileY, Nav3D::MediumWater);
        if (!tile)
            return nullptr;

        Nav3DTileHeader const& header = tile->Header();
        if (!(header.Flags & Nav3D::FlagHasSolid) || header.GeneratorVersion < 3)
            return nullptr;
        return tile;
    }

    // The tile is water-only; cells above the baked water column are air and must not be
    // vetoed. Everything below the top water cell is either navigable water or solid hull.
    bool TileBlocks(Grid const& grid, float x, float y, float z)
    {
        if (!grid.SolidTile)
            return false;

        Nav3DTileHeader const& header = grid.SolidTile->Header();
        if (x < header.OriginX || y < header.OriginY || z < header.OriginZ ||
            x > header.OriginX + float(header.SizeX) * header.CellSize ||
            y > header.OriginY + float(header.SizeY) * header.CellSize ||
            z > header.OriginZ + float(header.SizeZ) * header.CellSize)
            return false;

        // The water column ends at the surface; the node just above the top water cell is
        // still a valid ascent target. Keep a 1-yd surface band out of the solid veto.
        float const waterLevel =
            header.OriginZ + float(header.SizeZ) * header.CellSize - 1.0f;
        if (z >= waterLevel - 1.0f)
            return false;

        return !grid.SolidTile->IsNavigable(x, y, z, kTileBodyRadius);
    }

    bool SolidTileLineClear(Grid const& grid, G3D::Vector3 const& from, G3D::Vector3 const& to)
    {
        if (!grid.SolidTile)
            return true;

        float const dx = to.x - from.x;
        float const dy = to.y - from.y;
        float const dz = to.z - from.z;
        float const distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        int const steps = std::max(1, int(std::ceil(distance / 0.5f)));
        for (int i = 0; i <= steps; ++i)
        {
            G3D::Vector3 const point = from + (to - from) * (float(i) / float(steps));
            if (TileBlocks(grid, point.x, point.y, point.z))
                return false;
        }
        return true;
    }

    float Distance(Position const& left, Position const& right)
    {
        float const dx = left.GetPositionX() - right.GetPositionX();
        float const dy = left.GetPositionY() - right.GetPositionY();
        float const dz = left.GetPositionZ() - right.GetPositionZ();
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    float Distance(G3D::Vector3 const& left, G3D::Vector3 const& right)
    {
        float const dx = left.x - right.x;
        float const dy = left.y - right.y;
        float const dz = left.z - right.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    bool HasFloorClearance(Map* map, uint32 phase, Position const& position, float minClearance)
    {
        PositionFullTerrainStatus status;
        map->GetFullTerrainStatusForPosition(phase, position.GetPositionX(), position.GetPositionY(),
                                             position.GetPositionZ(), 1.0f, status, {});
        return status.floorZ > INVALID_HEIGHT &&
               position.GetPositionZ() - status.floorZ >= minClearance;
    }

    uint32 Nav3DNeighborCount()
    {
        uint32 const configured = sPlayerbotAIConfig.Nav3DNeighbors;
        if (configured == kNeighborsCardinal || configured == kNeighborsEdge)
            return configured;
        return kNeighborsFull;
    }

    std::vector<NeighborOffset> BuildNeighborOffsets(uint32 mode)
    {
        std::vector<NeighborOffset> offsets;
        for (int32 dx = -1; dx <= 1; ++dx)
        {
            for (int32 dy = -1; dy <= 1; ++dy)
            {
                for (int32 dz = -1; dz <= 1; ++dz)
                {
                    uint8 const axes =
                        uint8((dx != 0 ? 1 : 0) + (dy != 0 ? 1 : 0) + (dz != 0 ? 1 : 0));
                    if (!axes)
                        continue;
                    if (mode == kNeighborsCardinal && axes != 1)
                        continue;
                    if (mode == kNeighborsEdge && axes > 2)
                        continue;
                    offsets.push_back(
                        {static_cast<int8>(dx), static_cast<int8>(dy), static_cast<int8>(dz), axes});
                }
            }
        }
        return offsets;
    }

    bool PathFloorSafe(Map* map, uint32 phase, Movement::PointsArray const& path, float minClearance)
    {
        if (path.size() < 2)
            return false;

        float travelled = 0.0f;
        for (size_t i = 1; i < path.size(); ++i)
        {
            float const distance = Distance(path[i - 1], path[i]);
            if (distance < 0.001f)
                continue;

            int const steps = std::max(1, int(std::ceil(distance / 0.5f)));
            for (int step = 1; step <= steps; ++step)
            {
                float const t = float(step) / float(steps);
                G3D::Vector3 const point = path[i - 1] + (path[i] - path[i - 1]) * t;
                travelled += distance / float(steps);

                // The bot may already be slightly embedded in the floor at the start; do not
                // reject the whole path for the first yard it is trying to climb out of.
                if (travelled < 1.0f)
                    continue;

                if (!HasFloorClearance(map, phase, Position(point.x, point.y, point.z), minClearance))
                    return false;
            }
        }

        return true;
    }

    bool LineClear(Map* map, uint32 phase, Position const& from, Position const& to)
    {
        return map->isInLineOfSight(from.GetPositionX(), from.GetPositionY(), from.GetPositionZ(),
                                    to.GetPositionX(), to.GetPositionY(), to.GetPositionZ(), phase,
                                    LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing);
    }

    // WMO floors (wrecks, caves, buildings) need the detailed grid to keep a path through the
    // real opening. Open water can use a coarser grid: the volume is unobstructed and the
    // search cost drops by roughly the square of the cell-size ratio.
    bool NeedsDetailGrid(Map* map, uint32 phase, Position const& position)
    {
        float const vmapHeight = map->GetHeight(phase, position.GetPositionX(), position.GetPositionY(),
                                                position.GetPositionZ(), true);
        float const terrainHeight = map->GetHeight(phase, position.GetPositionX(), position.GetPositionY(),
                                                   position.GetPositionZ(), false);
        // No vmap floor here: open water/terrain, so the coarse grid is safe. A missing terrain
        // height with a valid vmap floor is a WMO over the void; keep the detailed grid.
        if (vmapHeight <= INVALID_HEIGHT)
            return false;
        if (terrainHeight <= INVALID_HEIGHT)
            return true;
        return std::abs(vmapHeight - terrainHeight) > 0.5f;
    }

    bool IsOpenWaterArea(Map* map, uint32 phase, Position const& position)
    {
        uint32 const area = map->GetAreaId(phase, position.GetPositionX(), position.GetPositionY(),
                                           position.GetPositionZ());
        auto const& areas = sPlayerbotAIConfig.Nav3DOpenWaterAreas;
        return std::find(areas.begin(), areas.end(), area) != areas.end();
    }

    bool LineClear(Map* map, uint32 phase, G3D::Vector3 const& from, G3D::Vector3 const& to)
    {
        return map->isInLineOfSight(from.x, from.y, from.z, to.x, to.y, to.z, phase, LINEOFSIGHT_ALL_CHECKS,
                                    VMAP::ModelIgnoreFlags::Nothing);
    }

    bool TileLineClear(Nav3DTile const* tile, G3D::Vector3 const& from, G3D::Vector3 const& to,
                       float radius)
    {
        if (!tile)
            return false;

        float const dx = to.x - from.x;
        float const dy = to.y - from.y;
        float const dz = to.z - from.z;
        float const distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        int const steps = std::max(1, int(std::ceil(distance / 0.5f)));
        for (int i = 0; i <= steps; ++i)
        {
            G3D::Vector3 const point = from + (to - from) * (float(i) / float(steps));
            if (!tile->IsNavigable(point.x, point.y, point.z, radius))
                return false;
        }
        return true;
    }

    bool GridLineClear(Grid const& grid, Map* map, uint32 phase, G3D::Vector3 const& from,
                       G3D::Vector3 const& to)
    {
        if (grid.Tile)
            return TileLineClear(grid.Tile, from, to, kTileBodyRadius);
        return LineClear(map, phase, from, to) && SolidTileLineClear(grid, from, to);
    }

    bool GridLineClear(Grid const& grid, Map* map, uint32 phase, Position const& from,
                       Position const& to)
    {
        G3D::Vector3 const fromPoint{from.GetPositionX(), from.GetPositionY(), from.GetPositionZ()};
        G3D::Vector3 const toPoint{to.GetPositionX(), to.GetPositionY(), to.GetPositionZ()};
        if (grid.Tile)
            return TileLineClear(grid.Tile, fromPoint, toPoint, kTileBodyRadius);
        return LineClear(map, phase, fromPoint, toPoint) && SolidTileLineClear(grid, fromPoint, toPoint);
    }

    bool PathCenterLineSafe(Map* map, uint32 phase, Movement::PointsArray const& path)
    {
        for (size_t i = 1; i < path.size(); ++i)
        {
            if (!LineClear(map, phase, path[i - 1], path[i]))
                return false;
        }
        return true;
    }

    void RemoveCollinear(Movement::PointsArray& path)
    {
        if (path.size() < 3)
            return;

        Movement::PointsArray result;
        result.push_back(path.front());
        for (size_t i = 1; i + 1 < path.size(); ++i)
        {
            G3D::Vector3 const& previous = result.back();
            G3D::Vector3 const& current = path[i];
            G3D::Vector3 const& next = path[i + 1];
            G3D::Vector3 const first = current - previous;
            G3D::Vector3 const second = next - current;
            if (first.cross(second).squaredLength() <= 1.0e-6f)
                continue;
            result.push_back(current);
        }
        result.push_back(path.back());
        path = result;
    }

    // A swimmer has a body. The planner's single ray can pass while the model clips a wall or
    // floor, so validate the final path with a short capsule: terrain/floor clearance plus
    // offset rays at the player radius.
    bool SegmentClear(Map* map, uint32 phase, G3D::Vector3 const& from, G3D::Vector3 const& to,
                      float radius)
    {
        float const dx = to.x - from.x;
        float const dy = to.y - from.y;
        float const dz = to.z - from.z;
        float const distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        int const steps = std::max(1, int(std::ceil(distance / 0.5f)));

        static G3D::Vector3 const offsets[] = {
            {0.5f, 0.0f, 0.0f},   {-0.5f, 0.0f, 0.0f},  {0.0f, 0.5f, 0.0f},
            {0.0f, -0.5f, 0.0f},  {0.3535f, 0.3535f, 0.0f}, {-0.3535f, 0.3535f, 0.0f},
            {0.3535f, -0.3535f, 0.0f}, {-0.3535f, -0.3535f, 0.0f},
            {0.0f, 0.0f, 1.0f},   {0.0f, 0.0f, -1.0f}};

        for (int i = 0; i <= steps; ++i)
        {
            float const t = float(i) / float(steps);
            G3D::Vector3 const point = from + (to - from) * t;

            // floorZ is terrain- and WMO-aware and ignores an outdoor surface above a cave
            // hollow, so a swimmer below the seabed's outside terrain is not treated as
            // embedded in it.
            PositionFullTerrainStatus status;
            map->GetFullTerrainStatusForPosition(phase, point.x, point.y, point.z, 1.0f, status, {});
            if (status.floorZ > INVALID_HEIGHT && point.z - status.floorZ < kCapsuleFloorClearance)
                return false;

            for (G3D::Vector3 const& offset : offsets)
            {
                G3D::Vector3 const probe = point + offset * (radius / 0.5f);
                if (!map->isInLineOfSight(point.x, point.y, point.z, probe.x, probe.y, probe.z, phase,
                                          LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
                    return false;
            }
        }

        return true;
    }

    bool IsNavigable(Grid const& grid, GridNode const& node, NodeState& state)
    {
        if (state.Nav != 0)
            return state.Nav > 0;

        Position const center = grid.Center(node);
        if (grid.Tile)
        {
            bool const navigable = grid.Tile->IsNavigable(center.GetPositionX(), center.GetPositionY(),
                                                          center.GetPositionZ(), kTileBodyRadius);
            state.Nav = navigable ? 1 : -1;
            return navigable;
        }

        float const x = center.GetPositionX();
        float const y = center.GetPositionY();
        float const z = center.GetPositionZ();

        LiquidData const liquid = grid.MapPtr->GetLiquidData(grid.Phase, x, y, z, 1.0f, {});
        bool const hasWater = liquid.Level > INVALID_HEIGHT && liquid.DepthLevel > INVALID_HEIGHT;
        bool const underWater = grid.MapPtr->IsUnderWater(grid.Phase, x, y, z, 1.0f);
        bool const inWater = grid.MapPtr->IsInWater(grid.Phase, x, y, z, 1.0f);

        bool navigable = underWater || inWater;
        if (!navigable)
        {
            // Above the water line, or a dry WMO hollow / cave below it. The edge LOS check
            // is what keeps the search from entering solid geometry.
            if (!hasWater || z >= liquid.Level - kSurfaceMargin || z >= liquid.DepthLevel + kFloorMargin)
                navigable = true;

            // The seabed itself: a dry cell below the local water floor is not navigable.
            if (hasWater && z < liquid.DepthLevel + kFloorMargin)
                navigable = false;
        }
        else if (!sPlayerbotAIConfig.Nav3DSwim)
        {
            navigable = false;
        }

        // A submerged start must not let A* escape into the open sky above the water; dry
        // cells below the water line (caves, WMO hollows) remain available.
        if (navigable && grid.RestrictSky && !underWater && !inWater && hasWater &&
            z > liquid.Level + kSurfaceMargin)
            navigable = false;

        // A liquid cell is not automatically valid: the seabed, a hull floor, or a cave floor
        // must actually be below it. This is the hard void guard; the body capsule stays
        // advisory so hull windows remain reachable. The liquid depth is the cheap floor
        // source; dry/WMO cells fall back to the terrain query.
        if (navigable)
        {
            bool const floorSafe =
                hasWater ? (z - liquid.DepthLevel >= kFloorMargin)
                         : HasFloorClearance(grid.MapPtr, grid.Phase, center, kFloorMargin);
            if (!floorSafe)
                navigable = false;
        }

        if (navigable && TileBlocks(grid, x, y, z))
            navigable = false;

        state.Nav = navigable ? 1 : -1;
        return navigable;
    }

    bool FindNearestNavigable(Grid const& grid, Position const& position, GridNode& result,
                              std::unordered_map<GridNode, NodeState, GridNodeHash>& states)
    {
        GridNode const snapped = grid.Snap(position);
        for (int32 radius = 0; radius <= 2; ++radius)
        {
            for (int32 dx = -radius; dx <= radius; ++dx)
            {
                for (int32 dy = -radius; dy <= radius; ++dy)
                {
                    for (int32 dz = -radius; dz <= radius; ++dz)
                    {
                        if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) != radius)
                            continue;

                        GridNode const candidate{snapped.X + dx, snapped.Y + dy, snapped.Z + dz};
                        if (!grid.Contains(candidate))
                            continue;

                        NodeState& state = states[candidate];
                        if (IsNavigable(grid, candidate, state))
                        {
                            result = candidate;
                            return true;
                        }
                    }
                }
            }
        }

        return false;
    }

    bool BuildPath(Grid const& grid, std::unordered_map<GridNode, NodeState, GridNodeHash> const& states,
                   GridNode const& startNode, GridNode const& goalNode, Position const& start,
                   Position const& goal, bool partial, Movement::PointsArray& path, Nav3DResult& result)
    {
        Movement::PointsArray raw;
        GridNode current = goalNode;
        while (true)
        {
            Position const center = grid.Center(current);
            raw.push_back({center.GetPositionX(), center.GetPositionY(), center.GetPositionZ()});

            if (current == startNode)
                break;

            auto const it = states.find(current);
            if (it == states.end() || !it->second.HasParent)
                return false;

            current = it->second.Parent;
        }
        std::reverse(raw.begin(), raw.end());

        Movement::PointsArray points;
        auto const appendUnique = [&points](G3D::Vector3 const& point)
        {
            if (points.empty() || Distance(points.back(), point) > kDuplicateEpsilon)
                points.push_back(point);
        };

        appendUnique({start.GetPositionX(), start.GetPositionY(), start.GetPositionZ()});
        for (G3D::Vector3 const& point : raw)
            appendUnique(point);

        if (partial)
        {
            Position const partialCenter = grid.Center(goalNode);
            appendUnique({partialCenter.GetPositionX(), partialCenter.GetPositionY(), partialCenter.GetPositionZ()});
        }
        else
        {
            appendUnique({goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ()});
        }

        if (points.size() > kMaxPathPoints)
            return false;

        // Smooth with a single ray per candidate: the capsule test is far too expensive to run
        // inside the O(n^2) candidate scan. The smoothed path is validated once below; if the
        // swimmer's body still clips, fall back to the raw cell chain, which hugs the A* route.
        path.clear();
        path.push_back(points.front());
        size_t index = 0;
        while (index + 1 < points.size())
        {
            size_t best = index + 1;
            size_t const last = points.size() - 1;
            size_t const maxCandidate = std::min(last, index + kMaxSmoothSkip);
            for (size_t candidate = maxCandidate; candidate > index + 1; --candidate)
            {
                if (GridLineClear(grid, grid.MapPtr, grid.Phase, points[index], points[candidate]))
                {
                    best = candidate;
                    break;
                }
            }

            path.push_back(points[best]);
            index = best;
        }

        bool capsuleClear = path.size() >= 2;
        if (grid.Tile)
        {
            for (size_t i = 1; capsuleClear && i < path.size(); ++i)
                capsuleClear = TileLineClear(grid.Tile, path[i - 1], path[i], kTileBodyRadius);
        }
        else
        {
            for (size_t i = 1; capsuleClear && i < path.size(); ++i)
                capsuleClear = SegmentClear(grid.MapPtr, grid.Phase, path[i - 1], path[i], 0.5f);
        }

        if (!capsuleClear)
            path = points;

        RemoveCollinear(path);

        // The capsule is advisory for a hull window, but the floor and centreline are not.
        // A path that dives below the seabed or passes through a wall must never be followed.
        bool pathSafe = false;
        if (grid.Tile)
        {
            pathSafe = path.size() >= 2;
            for (size_t i = 1; pathSafe && i < path.size(); ++i)
                pathSafe = TileLineClear(grid.Tile, path[i - 1], path[i], kTileBodyRadius);
        }
        else
            pathSafe = PathFloorSafe(grid.MapPtr, grid.Phase, path, kHardFloorClearance) &&
                       PathCenterLineSafe(grid.MapPtr, grid.Phase, path);

        if (!pathSafe)
        {
            RemoveCollinear(points);
            bool rawSafe = false;
            if (grid.Tile)
            {
                rawSafe = points.size() >= 2;
                for (size_t i = 1; rawSafe && i < points.size(); ++i)
                    rawSafe = TileLineClear(grid.Tile, points[i - 1], points[i], kTileBodyRadius);
            }
            else
                rawSafe = PathFloorSafe(grid.MapPtr, grid.Phase, points, kHardFloorClearance) &&
                          PathCenterLineSafe(grid.MapPtr, grid.Phase, points);

            if (!rawSafe)
            {
                LOG_DEBUG("playerbots",
                          "nav3d path rejected: floor or centreline unsafe (path={}, raw={})",
                          path.size(), points.size());
                return false;
            }

            path = points;
        }

        if (path.size() < 2)
            return false;

        result.Length = 0.0f;
        for (size_t i = 1; i < path.size(); ++i)
            result.Length += Distance(path[i - 1], path[i]);

        return true;
    }
}

Nav3DManager& Nav3DManager::Instance()
{
    static Nav3DManager instance;
    return instance;
}

bool Nav3DManager::FindPathTile(Map* map, uint32 phase, Position const& start, Position const& goal,
                                Movement::PointsArray& path, Nav3DResult& result)
{
    if (!map || !sPlayerbotAIConfig.Nav3DEnabled)
        return false;

    constexpr float gridSize = 533.3333f;
    uint16 const tileX =
        uint16(std::max<int32>(0, int32(std::floor(32.0f - start.GetPositionX() / gridSize))));
    uint16 const tileY =
        uint16(std::max<int32>(0, int32(std::floor(32.0f - start.GetPositionY() / gridSize))));
    std::shared_ptr<Nav3DTile> tile = GetTile(map->GetId(), tileX, tileY, Nav3D::MediumWater);
    if (!tile)
        return false;

    Nav3DTileHeader const& header = tile->Header();
    if (!(header.Flags & Nav3D::FlagHasSolid))
        return false;  // live-baked tiles without solid data fall back to lazy
    if (header.GeneratorVersion < 3)
        return false;  // v1/v2 clearance was distance-to-water, not horizontal body clearance
    float const maxX = header.OriginX + float(header.SizeX) * header.CellSize;
    float const maxY = header.OriginY + float(header.SizeY) * header.CellSize;
    float const maxZ = header.OriginZ + float(header.SizeZ) * header.CellSize;
    auto const inside = [&header, maxX, maxY, maxZ](Position const& position)
    {
        return position.GetPositionX() >= header.OriginX && position.GetPositionX() <= maxX &&
               position.GetPositionY() >= header.OriginY && position.GetPositionY() <= maxY &&
               position.GetPositionZ() >= header.OriginZ && position.GetPositionZ() <= maxZ;
    };
    if (!inside(start) || !inside(goal))
        return false;

    Nav3DResult tileResult;
    if (!FindPathDirected(map, phase, start, goal, path, tileResult, tile.get()))
        return false;

    result = tileResult;
    result.CellSize = header.CellSize;
    return true;
}

bool Nav3DManager::FindPath(Map* map, uint32 phase, Position const& start, Position const& goal,
                            Movement::PointsArray& path, Nav3DResult& result)
{
    result = {};
    path.clear();

    if (!map || !sPlayerbotAIConfig.Nav3DEnabled)
        return false;

    if (!sPlayerbotAIConfig.Nav3DLazy)
    {
        if (FindPathTile(map, phase, start, goal, path, result))
        {
            LOG_DEBUG("playerbots",
                      "nav3d tile path {} start=({:.2f},{:.2f},{:.2f}) goal=({:.2f},{:.2f},{:.2f}) "
                      "direct={} partial={} points={} len={:.1f} ms={}",
                      map->GetId(), start.GetPositionX(), start.GetPositionY(), start.GetPositionZ(),
                      goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ(), result.Direct ? 1 : 0,
                      result.Partial ? 1 : 0, path.size(), result.Length, result.ElapsedMs);
            return true;
        }

        LOG_DEBUG("playerbots", "nav3d tile miss map {} start=({:.2f},{:.2f},{:.2f}); lazy fallback",
                  map->GetId(), start.GetPositionX(), start.GetPositionY(), start.GetPositionZ());
    }

    if (FindPathDirected(map, phase, start, goal, path, result))
        return true;

    // The tunnel is asymmetric for a best-first search: entering it often needs many more
    // expansions than leaving it. Search the reverse direction once and reverse the points.
    Nav3DResult const forwardResult = result;
    Movement::PointsArray reversePath;
    Nav3DResult reverseResult;
    if (!FindPathDirected(map, phase, goal, start, reversePath, reverseResult))
    {
        result = forwardResult;
        return false;
    }

    std::reverse(reversePath.begin(), reversePath.end());
    path = reversePath;
    result = reverseResult;
    result.Found = true;
    result.Reversed = true;
    return true;
}

bool Nav3DManager::FindPathDirected(Map* map, uint32 phase, Position const& start, Position const& goal,
                                    Movement::PointsArray& path, Nav3DResult& result,
                                    Nav3DTile const* tile)
{
    result = {};
    path.clear();

    if (!map || !sPlayerbotAIConfig.Nav3DEnabled)
        return false;

    bool const openWater = !tile && IsOpenWaterArea(map, phase, start) && IsOpenWaterArea(map, phase, goal);

    Grid grid;
    grid.MapPtr = map;
    grid.Phase = phase;

    if (tile)
    {
        Nav3DTileHeader const& header = tile->Header();
        float const tileMaxX = header.OriginX + float(header.SizeX) * header.CellSize;
        float const tileMaxY = header.OriginY + float(header.SizeY) * header.CellSize;
        float const tileMaxZ = header.OriginZ + float(header.SizeZ) * header.CellSize;

        grid.Tile = tile;
        grid.Cell = header.CellSize;

        // Align the search grid to the tile cell centres. Otherwise a node centre is offset
        // from the baked cell it samples, and a one-cell-wide hull window can fall between
        // the goal node and the tile, making a navigable window look unreachable.
        auto const alignDown = [&header](float value, float origin)
        {
            return origin + std::floor((value - origin) / header.CellSize) * header.CellSize;
        };
        grid.MinX = alignDown(std::max(header.OriginX,
                                       std::min(start.GetPositionX(), goal.GetPositionX()) - kXYPad),
                              header.OriginX);
        grid.MinY = alignDown(std::max(header.OriginY,
                                       std::min(start.GetPositionY(), goal.GetPositionY()) - kXYPad),
                              header.OriginY);
        grid.MinZ = alignDown(std::max(header.OriginZ,
                                       std::min(start.GetPositionZ(), goal.GetPositionZ()) - kZPad),
                              header.OriginZ);
        float const maxX = std::min(tileMaxX,
                                    std::max(start.GetPositionX(), goal.GetPositionX()) + kXYPad);
        float const maxY = std::min(tileMaxY,
                                    std::max(start.GetPositionY(), goal.GetPositionY()) + kXYPad);
        float const maxZ = std::min(tileMaxZ,
                                    std::max(start.GetPositionZ(), goal.GetPositionZ()) + kZPad);
        grid.SizeX = std::max(1, int32(std::ceil((maxX - grid.MinX) / header.CellSize)));
        grid.SizeY = std::max(1, int32(std::ceil((maxY - grid.MinY) / header.CellSize)));
        grid.SizeZ = std::max(1, int32(std::ceil((maxZ - grid.MinZ) / header.CellSize)));
        result.CellSize = header.CellSize;
    }
    else
    {
        bool const detailGrid =
            !openWater && (NeedsDetailGrid(map, phase, start) || NeedsDetailGrid(map, phase, goal));
        float const detailCellSize = std::clamp(sPlayerbotAIConfig.Nav3DCellSize, kMinCellSize, kMaxCellSize);
        float const cellSize = detailGrid ? detailCellSize : std::min(kMaxCellSize, detailCellSize * 2.0f);
        result.CellSize = cellSize;

        grid.Cell = cellSize;
        float const minX = std::min(start.GetPositionX(), goal.GetPositionX()) - kXYPad;
        float const minY = std::min(start.GetPositionY(), goal.GetPositionY()) - kXYPad;
        float const minZ = std::min(start.GetPositionZ(), goal.GetPositionZ()) - kZPad;
        float const maxX = std::max(start.GetPositionX(), goal.GetPositionX()) + kXYPad;
        float const maxY = std::max(start.GetPositionY(), goal.GetPositionY()) + kXYPad;
        float const maxZ = std::max(start.GetPositionZ(), goal.GetPositionZ()) + kZPad;

        grid.MinX = minX;
        grid.MinY = minY;
        grid.MinZ = minZ;
        grid.SizeX = std::max(1, std::min(kMaxCellsPerAxis, int32(std::ceil((maxX - minX) / cellSize))));
        grid.SizeY = std::max(1, std::min(kMaxCellsPerAxis, int32(std::ceil((maxY - minY) / cellSize))));
        grid.SizeZ = std::max(1, std::min(kMaxCellsPerAxis, int32(std::ceil((maxZ - minZ) / cellSize))));

        float const startLevel = map->GetWaterLevel(start.GetPositionX(), start.GetPositionY());
        bool const startInWater =
            map->IsUnderWater(phase, start.GetPositionX(), start.GetPositionY(), start.GetPositionZ(), 1.0f) ||
            map->IsInWater(phase, start.GetPositionX(), start.GetPositionY(), start.GetPositionZ(), 1.0f) ||
            (startLevel > INVALID_HEIGHT && start.GetPositionZ() <= startLevel + kSurfaceMargin);
        grid.RestrictSky = startInWater;
    }

    if (!tile)
    {
        grid.SolidTile = ResolveSolidTile(map, start);
        if (!grid.SolidTile)
            grid.SolidTile = ResolveSolidTile(map, goal);
    }

    auto const inside = [&grid](Position const& position)
    {
        return position.GetPositionX() >= grid.MinX &&
               position.GetPositionX() <= grid.MinX + grid.SizeX * grid.Cell &&
               position.GetPositionY() >= grid.MinY &&
               position.GetPositionY() <= grid.MinY + grid.SizeY * grid.Cell &&
               position.GetPositionZ() >= grid.MinZ &&
               position.GetPositionZ() <= grid.MinZ + grid.SizeZ * grid.Cell;
    };
    if (!inside(start) || !inside(goal))
        return false;

    uint32 const maxNodes = std::max<uint32>(1, std::min<uint32>(kHardMaxNodes, sPlayerbotAIConfig.Nav3DMaxNodes));
    uint32 const maxTimeMs = std::max<uint32>(1, std::min<uint32>(kHardMaxTimeMs, sPlayerbotAIConfig.Nav3DMaxSearchTimeMs));

    std::unordered_map<GridNode, NodeState, GridNodeHash> states;
    states.reserve(4096);

    GridNode startNode;
    GridNode goalNode;
    if (!FindNearestNavigable(grid, start, startNode, states) ||
        !FindNearestNavigable(grid, goal, goalNode, states))
        return false;

    G3D::Vector3 const startPoint{start.GetPositionX(), start.GetPositionY(), start.GetPositionZ()};
    G3D::Vector3 const goalPoint{goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ()};
    bool const directClear = tile ? TileLineClear(tile, startPoint, goalPoint, kTileBodyRadius)
                                  : SegmentClear(map, phase, startPoint, goalPoint, 0.5f);
    if (directClear)
    {
        path.push_back(startPoint);
        path.push_back(goalPoint);
        result.Found = true;
        result.Direct = true;
        result.Nodes = uint32(states.size());
        result.ElapsedMs = 0;
        result.Length = Distance(start, goal);
        return true;
    }

    auto const heuristic = [&grid, &goal](GridNode const& node)
    {
        return Distance(grid.Center(node), goal);
    };
    // The tile has narrow one-cell hull windows. A pure Dijkstra-like heuristic expands most
    // of the open-water volume before it finds the window; a greedy tile search is still safe
    // because every visited cell has been clearance-checked, and it is much cheaper.
    float const heuristicWeight = grid.Tile ? 2.0f : 1.0f;

    NodeState& startState = states[startNode];
    startState.G = 0.0f;
    startState.F = heuristic(startNode) * heuristicWeight;
    startState.HasParent = false;

    std::priority_queue<QueueNode, std::vector<QueueNode>, std::greater<QueueNode>> open;
    open.push({startState.F, startNode});

    uint32 const startedMs = getMSTime();
    uint32 expanded = 0;
    bool found = false;
    GridNode bestNode{};
    bool hasBest = false;
    result.BestDistance = std::numeric_limits<float>::max();
    std::vector<NeighborOffset> const neighborOffsets = BuildNeighborOffsets(Nav3DNeighborCount());

    while (!open.empty())
    {
        if ((expanded & kTimeCheckMask) == 0 && getMSTime() - startedMs >= maxTimeMs)
            break;

        QueueNode const current = open.top();
        open.pop();

        NodeState& currentState = states[current.Node];
        if (currentState.Closed)
            continue;

        currentState.Closed = true;
        ++expanded;

        float const currentDistance = heuristic(current.Node);
        if (currentDistance < result.BestDistance)
        {
            result.BestDistance = currentDistance;
            Position const best = grid.Center(current.Node);
            result.BestX = best.GetPositionX();
            result.BestY = best.GetPositionY();
            result.BestZ = best.GetPositionZ();
            bestNode = current.Node;
            hasBest = true;
        }

        if (current.Node == goalNode)
        {
            found = true;
            break;
        }

        // A chest can sit just under the navmesh surface. Exhausting the whole component to
        // prove the exact cell is unreachable is wasteful; if a reachable node is already
        // within interaction range, stop and let the caller interact from there.
        if (currentDistance <= kEarlyPartialDistance)
        {
            found = true;
            result.Partial = true;
            result.Direct = false;
            result.BestDistance = currentDistance;
            Position const best = grid.Center(current.Node);
            result.BestX = best.GetPositionX();
            result.BestY = best.GetPositionY();
            result.BestZ = best.GetPositionZ();
            goalNode = current.Node;
            break;
        }

        if (expanded >= maxNodes)
            break;

        Position const currentCenter = grid.Center(current.Node);
        for (NeighborOffset const& offset : neighborOffsets)
        {
            GridNode const neighbor{current.Node.X + offset.X, current.Node.Y + offset.Y,
                                    current.Node.Z + offset.Z};
            if (!grid.Contains(neighbor))
                continue;

            NodeState& neighborState = states[neighbor];
            if (neighborState.Closed)
                continue;

            if (!IsNavigable(grid, neighbor, neighborState))
                continue;

            Position const neighborCenter = grid.Center(neighbor);
            if (!GridLineClear(grid, map, phase, currentCenter, neighborCenter))
                continue;

            float const tentative = currentState.G + Distance(currentCenter, neighborCenter);
            if (tentative >= neighborState.G)
                continue;

            neighborState.G = tentative;
            neighborState.Parent = current.Node;
            neighborState.HasParent = true;
            neighborState.F = tentative + heuristic(neighbor) * heuristicWeight;
            open.push({neighborState.F, neighbor});
        }
    }

    result.Nodes = uint32(states.size());
    result.Expanded = expanded;
    result.ElapsedMs = getMSTime() - startedMs;
    if (result.BestDistance == std::numeric_limits<float>::max())
        result.BestDistance = 0.0f;

    // Accept a path that ends within interaction range of an unreachable exact cell (a chest
    // embedded in the hull/floor). The bot can interact from the best reachable node.
    if (!found && hasBest && result.BestDistance <= kPartialAcceptDistance)
    {
        found = true;
        result.Partial = true;
        result.Direct = false;
        goalNode = bestNode;
    }

    if (!found)
        return false;

    if (!BuildPath(grid, states, startNode, goalNode, start, goal, result.Partial, path, result))
        return false;

    result.Found = true;
    return true;
}

bool Nav3DManager::FindPathToSurface(Map* map, uint32 phase, Position const& start,
                                     Movement::PointsArray& path, Nav3DResult& result)
{
    result = {};
    path.clear();

    if (!map || !sPlayerbotAIConfig.Nav3DEnabled)
        return false;

    bool const openWater = IsOpenWaterArea(map, phase, start);
    bool const detailGrid = !openWater && NeedsDetailGrid(map, phase, start);
    float const detailCellSize = std::clamp(sPlayerbotAIConfig.Nav3DCellSize, kMinCellSize, kMaxCellSize);
    float const cellSize = detailGrid ? detailCellSize : std::min(kMaxCellSize, detailCellSize * 2.0f);
    result.CellSize = cellSize;

    Grid grid;
    grid.MapPtr = map;
    grid.Phase = phase;
    grid.Cell = cellSize;

    constexpr float kSurfaceRadiusXY = 120.0f;
    constexpr float kSurfaceRadiusZ = 100.0f;
    grid.MinX = start.GetPositionX() - kSurfaceRadiusXY;
    grid.MinY = start.GetPositionY() - kSurfaceRadiusXY;
    grid.MinZ = start.GetPositionZ() - kSurfaceRadiusZ;
    grid.SizeX = std::max(1, std::min(kMaxCellsPerAxis, int32(std::ceil(2.0f * kSurfaceRadiusXY / cellSize))));
    grid.SizeY = std::max(1, std::min(kMaxCellsPerAxis, int32(std::ceil(2.0f * kSurfaceRadiusXY / cellSize))));
    grid.SizeZ = std::max(1, std::min(kMaxCellsPerAxis, int32(std::ceil(2.0f * kSurfaceRadiusZ / cellSize))));
    grid.SolidTile = ResolveSolidTile(map, start);

    float const startLevel = map->GetWaterLevel(start.GetPositionX(), start.GetPositionY());
    bool const startInWater = map->IsUnderWater(phase, start.GetPositionX(), start.GetPositionY(),
                                                start.GetPositionZ(), 1.0f) ||
                              map->IsInWater(phase, start.GetPositionX(), start.GetPositionY(),
                                             start.GetPositionZ(), 1.0f) ||
                              (startLevel > INVALID_HEIGHT && start.GetPositionZ() <= startLevel + kSurfaceMargin);
    grid.RestrictSky = startInWater;

    uint32 const maxNodes = std::max<uint32>(1, std::min<uint32>(kHardMaxNodes, sPlayerbotAIConfig.Nav3DMaxNodes));
    uint32 const maxTimeMs =
        std::max<uint32>(1, std::min<uint32>(kHardMaxTimeMs, sPlayerbotAIConfig.Nav3DMaxSearchTimeMs));

    std::unordered_map<GridNode, NodeState, GridNodeHash> states;
    states.reserve(4096);

    GridNode startNode;
    if (!FindNearestNavigable(grid, start, startNode, states))
        return false;

    NodeState& startState = states[startNode];
    startState.G = 0.0f;
    startState.F = 0.0f;
    startState.HasParent = false;

    std::priority_queue<QueueNode, std::vector<QueueNode>, std::greater<QueueNode>> open;
    open.push({0.0f, startNode});

    constexpr float kSurfaceCheckHeight = 2.0f;
    uint32 const startedMs = getMSTime();
    uint32 expanded = 0;
    bool found = false;
    GridNode surfaceNode{};
    std::vector<NeighborOffset> const neighborOffsets = BuildNeighborOffsets(Nav3DNeighborCount());

    while (!open.empty())
    {
        if ((expanded & kTimeCheckMask) == 0 && getMSTime() - startedMs >= maxTimeMs)
            break;

        QueueNode const current = open.top();
        open.pop();

        NodeState& currentState = states[current.Node];
        if (currentState.Closed)
            continue;

        currentState.Closed = true;
        ++expanded;

        Position const currentCenter = grid.Center(current.Node);
        LiquidData const liquid = map->GetLiquidData(phase, currentCenter.GetPositionX(),
                                                     currentCenter.GetPositionY(),
                                                     currentCenter.GetPositionZ() + 0.5f,
                                                     kSurfaceCheckHeight, {});
        bool const atSurface =
            liquid.Level > INVALID_HEIGHT
                ? currentCenter.GetPositionZ() >= liquid.Level - 1.0f
                : !map->IsUnderWater(phase, currentCenter.GetPositionX(), currentCenter.GetPositionY(),
                                     currentCenter.GetPositionZ(), kSurfaceCheckHeight);
        if (atSurface)
        {
            found = true;
            surfaceNode = current.Node;
            break;
        }

        if (expanded >= maxNodes)
            break;

        for (NeighborOffset const& offset : neighborOffsets)
        {
            GridNode const neighbor{current.Node.X + offset.X, current.Node.Y + offset.Y,
                                    current.Node.Z + offset.Z};
            if (!grid.Contains(neighbor))
                continue;

            NodeState& neighborState = states[neighbor];
            if (neighborState.Closed)
                continue;

            if (!IsNavigable(grid, neighbor, neighborState))
                continue;

            Position const neighborCenter = grid.Center(neighbor);
            if (!GridLineClear(grid, map, phase, currentCenter, neighborCenter))
                continue;

            float const tentative = currentState.G + Distance(currentCenter, neighborCenter);
            if (tentative >= neighborState.G)
                continue;

            neighborState.G = tentative;
            neighborState.Parent = current.Node;
            neighborState.HasParent = true;
            neighborState.F = tentative;
            open.push({tentative, neighbor});
        }
    }

    result.Nodes = uint32(states.size());
    result.Expanded = expanded;
    result.ElapsedMs = getMSTime() - startedMs;

    if (!found)
        return false;

    Position const surface = grid.Center(surfaceNode);
    if (!BuildPath(grid, states, startNode, surfaceNode, start, surface, false, path, result))
        return false;

    result.Found = true;
    return true;
}

bool Nav3DManager::IsPathClear(Map* map, uint32 phase, Movement::PointsArray const& path, float radius) const
{
    if (!map || path.size() < 2)
        return false;

    for (size_t i = 1; i < path.size(); ++i)
        if (!SegmentClear(map, phase, path[i - 1], path[i], radius))
            return false;

    return true;
}

std::shared_ptr<Nav3DTile> Nav3DManager::GetTile(uint32 mapId, uint16 tileX, uint16 tileY, uint8 medium)
{
    uint64 const key = (uint64(mapId) << 32) | (uint64(tileX) << 16) | (uint64(tileY) << 8) | medium;

    std::scoped_lock lock(_tileCacheMutex);
    auto const cached = _tileCache.find(key);
    if (cached != _tileCache.end())
    {
        _tileLru.splice(_tileLru.begin(), _tileLru, cached->second);
        return cached->second->Tile;
    }

    std::string dataPath = sWorld ? sWorld->GetDataPath() : std::string();
    if (!dataPath.empty() && dataPath.back() != '/')
        dataPath += '/';
    dataPath += sPlayerbotAIConfig.Nav3DDataPath;
    if (!dataPath.empty() && dataPath.back() != '/')
        dataPath += '/';

    std::string const mediumName = medium == Nav3D::MediumWater ? "water" : "air";
    std::string const path = Acore::StringFormat("{}map{:04}/{:02}_{:02}_{}.n3t", dataPath, mapId,
                                                 tileX, tileY, mediumName);

    auto tile = std::make_shared<Nav3DTile>();
    if (!tile->Load(path))
    {
        LOG_DEBUG("playerbots", "nav3d tile missing or invalid: {}", path);
        return nullptr;
    }

    _tileLru.push_front({key, tile});
    _tileCache[key] = _tileLru.begin();

    uint32 const maxTiles = std::max<uint32>(1, sPlayerbotAIConfig.Nav3DTileCacheSize);
    while (_tileLru.size() > maxTiles)
    {
        TileCacheEntry const& last = _tileLru.back();
        _tileCache.erase(last.Key);
        _tileLru.pop_back();
    }

    return tile;
}

bool Nav3DManager::HasSolidTileAt(Map* map, Position const& position)
{
    std::shared_ptr<Nav3DTile> tile = ResolveSolidTile(map, position);
    if (!tile)
        return false;

    Nav3DTileHeader const& header = tile->Header();
    return position.GetPositionX() >= header.OriginX && position.GetPositionY() >= header.OriginY &&
           position.GetPositionZ() >= header.OriginZ &&
           position.GetPositionX() <= header.OriginX + float(header.SizeX) * header.CellSize &&
           position.GetPositionY() <= header.OriginY + float(header.SizeY) * header.CellSize &&
           position.GetPositionZ() <= header.OriginZ + float(header.SizeZ) * header.CellSize;
}

void Nav3DManager::ClearTileCache()
{
    std::scoped_lock lock(_tileCacheMutex);
    _tileCache.clear();
    _tileLru.clear();
}

bool Nav3DManager::BakeWaterTile(Map* map, uint32 phase, uint16 tileX, uint16 tileY, float cellSize,
                                 float waterLevel, float minZ, float boundsMinX, float boundsMaxX,
                                 float boundsMinY, float boundsMaxY, std::string const& outputPath,
                                 std::string& error)
{
    if (!map)
    {
        error = "no map";
        return false;
    }
    if (cellSize < 0.5f || cellSize > 4.0f)
    {
        error = "cell size must be in [0.5, 4.0]";
        return false;
    }
    if (waterLevel <= INVALID_HEIGHT || minZ >= waterLevel - 1.0f)
    {
        error = "invalid water level / minimum Z";
        return false;
    }

    constexpr float gridSize = 533.3333f;
    uint32 const mapId = map->GetId();

    float const tileOriginX = (32.0f - float(tileX)) * gridSize - gridSize;
    float const tileOriginY = (32.0f - float(tileY)) * gridSize - gridSize;
    float originX = tileOriginX;
    float originY = tileOriginY;
    uint32 sizeX = std::max<uint32>(1, uint32(std::ceil(gridSize / cellSize)));
    uint32 sizeY = std::max<uint32>(1, uint32(std::ceil(gridSize / cellSize)));
    uint32 const sizeZ = std::max<uint32>(1, uint32(std::ceil((waterLevel + 1.0f - minZ) / cellSize)));

    // Optional local box for fast iteration. The tile header describes only this box, so
    // FindPathTile falls back to the lazy search for points outside it.
    if (boundsMaxX > boundsMinX && boundsMaxY > boundsMinY)
    {
        float const tileMaxX = tileOriginX + gridSize;
        float const tileMaxY = tileOriginY + gridSize;
        float const clampMinX = std::clamp(boundsMinX, tileOriginX, tileMaxX - cellSize);
        float const clampMaxX = std::clamp(boundsMaxX, clampMinX + cellSize, tileMaxX);
        float const clampMinY = std::clamp(boundsMinY, tileOriginY, tileMaxY - cellSize);
        float const clampMaxY = std::clamp(boundsMaxY, clampMinY + cellSize, tileMaxY);
        originX = tileOriginX +
                  std::floor((clampMinX - tileOriginX) / cellSize) * cellSize;
        originY = tileOriginY +
                  std::floor((clampMinY - tileOriginY) / cellSize) * cellSize;
        uint32 const fullSizeX = sizeX;
        uint32 const fullSizeY = sizeY;
        sizeX = std::clamp<uint32>(uint32(std::ceil((clampMaxX - originX) / cellSize)), 1,
                                   fullSizeX);
        sizeY = std::clamp<uint32>(uint32(std::ceil((clampMaxY - originY) / cellSize)), 1,
                                   fullSizeY);
    }

    uint64 const cellCount64 = uint64(sizeX) * uint64(sizeY) * uint64(sizeZ);
    if (cellCount64 == 0 || cellCount64 > 512ull * 1024ull * 1024ull)
    {
        error = "voxel grid too large";
        return false;
    }
    size_t const cellCount = size_t(cellCount64);

    auto const cellIndex = [sizeX, sizeY](uint32 x, uint32 y, uint32 z) -> size_t
    {
        return (size_t(z) * size_t(sizeY) + size_t(y)) * size_t(sizeX) + size_t(x);
    };

    std::vector<uint8> water(cellCount, 0);
    size_t waterColumns = 0;
    size_t waterCells = 0;
    LOG_INFO("playerbots", "nav3dbake map {} tile {}:{} cell {:.2f} grid {}x{}x{}", mapId, tileX, tileY,
             cellSize, sizeX, sizeY, sizeZ);

    for (uint32 y = 0; y < sizeY; ++y)
    {
        for (uint32 x = 0; x < sizeX; ++x)
        {
            float const worldX = originX + (float(x) + 0.5f) * cellSize;
            float const worldY = originY + (float(y) + 0.5f) * cellSize;

            // Terrain seabed for this column. Water below it is the void under the map and is
            // excluded even when the liquid data still reports water.
            float const terrainZ = map->GetHeight(worldX, worldY, waterLevel, false);
            bool const hasTerrain = terrainZ > INVALID_HEIGHT;
            float const floorLimit = hasTerrain ? terrainZ + 0.25f : waterLevel - 40.0f;

            bool anyWater = false;
            int dryRun = 0;
            int maxDryRun = std::max<int>(2, int(std::ceil(4.0f / cellSize)));

            // Walk the water column downward from the surface. Wet cells are navigable water.
            // Thin solid decks are skipped (the hold below them is still water); a long dry
            // run means solid hull/terrain or the breathable void above the seabed, and the
            // column stops there.
            for (float z = waterLevel - 0.5f; z >= std::max(minZ, floorLimit); z -= cellSize)
            {
                int const zc = int(std::floor((z - minZ) / cellSize));
                if (zc < 0 || uint32(zc) >= sizeZ)
                    break;

                bool const wet = map->IsUnderWater(phase, worldX, worldY, z, 1.0f) ||
                                 map->IsInWater(phase, worldX, worldY, z, 1.0f);
                if (wet)
                {
                    size_t const index = cellIndex(uint32(x), uint32(y), uint32(zc));
                    if (!water[index])
                    {
                        water[index] = 1;
                        ++waterCells;
                    }
                    anyWater = true;
                    dryRun = 0;
                }
                else
                {
                    ++dryRun;
                    if (dryRun >= maxDryRun)
                        break;
                }
            }

            if (anyWater)
                ++waterColumns;
        }
    }

    // Body-box solid pass. The client collision volume is an axis-aligned box of half-width
    // 2 * radius in X/Y and full collision height, not a circle. A gap that clears a circular
    // probe can still be too small for the box corners. Probe the box boundary: axis rays at
    // the half-width, diagonal rays at half-width * sqrt(2), and short vertical rays.
    {
        constexpr float bodyRadius = 0.45f;  // covers the largest playable race (0.433) + margin
        constexpr float bodyDiagonal = bodyRadius * 1.41421356f;
        constexpr float bodyHalfHeight = 0.8f;
        struct BoxRay
        {
            float X;
            float Y;
            float Z;
            float Distance;
        };
        static constexpr BoxRay rays[10] = {
            {1.0f, 0.0f, 0.0f, bodyRadius},       {-1.0f, 0.0f, 0.0f, bodyRadius},
            {0.0f, 1.0f, 0.0f, bodyRadius},       {0.0f, -1.0f, 0.0f, bodyRadius},
            {0.7071f, 0.7071f, 0.0f, bodyDiagonal}, {-0.7071f, 0.7071f, 0.0f, bodyDiagonal},
            {0.7071f, -0.7071f, 0.0f, bodyDiagonal}, {-0.7071f, -0.7071f, 0.0f, bodyDiagonal},
            {0.0f, 0.0f, 1.0f, bodyHalfHeight},   {0.0f, 0.0f, -1.0f, bodyHalfHeight}};

        size_t blockedCells = 0;
        for (uint32 z = 0; z < sizeZ; ++z)
        {
            for (uint32 y = 0; y < sizeY; ++y)
            {
                for (uint32 x = 0; x < sizeX; ++x)
                {
                    size_t const index = cellIndex(x, y, z);
                    if (!water[index])
                        continue;

                    float const wx = originX + (float(x) + 0.5f) * cellSize;
                    float const wy = originY + (float(y) + 0.5f) * cellSize;
                    float const wz = minZ + (float(z) + 0.5f) * cellSize;

                    bool blocked = false;
                    for (BoxRay const& ray : rays)
                    {
                        if (!map->isInLineOfSight(wx, wy, wz, wx + ray.X * ray.Distance,
                                                  wy + ray.Y * ray.Distance, wz + ray.Z * ray.Distance,
                                                  phase, LINEOFSIGHT_ALL_CHECKS,
                                                  VMAP::ModelIgnoreFlags::Nothing))
                        {
                            blocked = true;
                            break;
                        }
                    }

                    if (blocked)
                    {
                        water[index] = 0;
                        ++blockedCells;
                    }
                }
            }
        }
        if (blockedCells)
            LOG_INFO("playerbots", "nav3dbake solid pass: {} blocked cells removed", blockedCells);
    }

    // Horizontal clearance: distance in the XY plane to the nearest non-water cell in the
    // same Z slice. The water surface/floor must not shrink this value; only walls and other
    // solid obstacles in the swimmer's horizontal plane matter for fitting through a gap.
    std::vector<uint8> distance(cellCount, 0);
    auto const relax2D = [&distance, sizeX, sizeY](uint8& value, int x, int y, uint32 z)
    {
        if (x < 0 || y < 0 || x >= int(sizeX) || y >= int(sizeY))
        {
            value = std::min<uint8>(value, 1);
            return;
        }
        uint8 const neighbor = distance[(size_t(z) * size_t(sizeY) + size_t(y)) * size_t(sizeX) + size_t(x)];
        if (neighbor < 255)
            value = std::min<uint8>(value, uint8(neighbor + 1));
    };

    for (uint32 z = 0; z < sizeZ; ++z)
    {
        for (uint32 y = 0; y < sizeY; ++y)
            for (uint32 x = 0; x < sizeX; ++x)
            {
                size_t const index = cellIndex(x, y, z);
                distance[index] = water[index] ? 255 : 0;
            }

        for (uint32 y = 0; y < sizeY; ++y)
            for (uint32 x = 0; x < sizeX; ++x)
            {
                size_t const index = cellIndex(x, y, z);
                if (!water[index])
                    continue;
                uint8 value = 255;
                relax2D(value, int(x) - 1, int(y), z);
                relax2D(value, int(x), int(y) - 1, z);
                distance[index] = value;
            }

        for (uint32 y = sizeY; y-- > 0;)
            for (uint32 x = sizeX; x-- > 0;)
            {
                size_t const index = cellIndex(x, y, z);
                if (!water[index])
                    continue;
                uint8 value = distance[index];
                relax2D(value, int(x) + 1, int(y), z);
                relax2D(value, int(x), int(y) + 1, z);
                distance[index] = value;
            }
    }

    Nav3DTile tile;
    Nav3DTileHeader& header = tile.Header();
    header.MapId = mapId;
    header.TileX = tileX;
    header.TileY = tileY;
    header.Medium = Nav3D::MediumWater;
    header.CellSize = cellSize;
    header.OriginX = originX;
    header.OriginY = originY;
    header.OriginZ = minZ;
    header.ExtentX = float(sizeX) * cellSize;
    header.ExtentY = float(sizeY) * cellSize;
    header.ExtentZ = float(sizeZ) * cellSize;
    header.SizeX = sizeX;
    header.SizeY = sizeY;
    header.SizeZ = sizeZ;
    header.GeneratorVersion = 3;
    header.GeometryHash = 0;
    header.Flags |= Nav3D::FlagHasSolid;

    tile.Columns().resize(size_t(sizeX) * size_t(sizeY));
    for (uint32 y = 0; y < sizeY; ++y)
    {
        for (uint32 x = 0; x < sizeX; ++x)
        {
            Nav3DTileColumn& column = tile.Columns()[size_t(y) * size_t(sizeX) + size_t(x)];
            int start = -1;
            uint8 currentClearance = 0;

            auto const flush = [&](int end)
            {
                if (start < 0)
                    return;
                Nav3DTileInterval interval;
                interval.ZMin = int16(start);
                interval.ZMax = int16(end);
                interval.Clearance = currentClearance;
                interval.Flags = 0;
                if (column.IntervalCount == 0)
                    column.IntervalOffset = uint32(tile.Intervals().size());
                column.IntervalCount = uint16(column.IntervalCount + 1);
                tile.Intervals().push_back(interval);
                start = -1;
                currentClearance = 0;
            };

            for (int z = 0; z < int(sizeZ); ++z)
            {
                size_t const index = cellIndex(x, y, uint32(z));
                if (!water[index])
                {
                    flush(z - 1);
                    continue;
                }

                // Clearance is horizontal, so it can change with depth (a hull window is wide
                // at chest height and narrow above/below). Split intervals instead of storing
                // the minimum over the whole column; a single minimum would let the seabed or
                // water surface close an otherwise usable window.
                uint32 const rawClearance = std::min<uint32>(
                    16, uint32(float(distance[index]) * cellSize / Nav3D::ClearanceUnit));
                uint8 const clearance = uint8((rawClearance / 2) * 2);  // 0.5-yd buckets
                if (start < 0)
                {
                    start = z;
                    currentClearance = clearance;
                }
                else if (clearance != currentClearance)
                {
                    flush(z - 1);
                    start = z;
                    currentClearance = clearance;
                }
            }
            flush(int(sizeZ) - 1);
        }
    }

    if (!tile.Save(outputPath))
    {
        error = "failed to write " + outputPath;
        return false;
    }

    LOG_INFO("playerbots",
             "nav3dbake map {} tile {}:{} wrote {} ({} water columns, {} cells, {} intervals)", mapId,
             tileX, tileY, outputPath, waterColumns, waterCells, tile.Intervals().size());
    return true;
}
