/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Nav3DManager.h"

#include "Log.h"
#include "Map.h"
#include "PlayerbotAIConfig.h"
#include "Timer.h"

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
        float const distance = Distance(from, to);
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
                if (LineClear(grid.MapPtr, grid.Phase, points[index], points[candidate]))
                {
                    best = candidate;
                    break;
                }
            }

            path.push_back(points[best]);
            index = best;
        }

        bool capsuleClear = path.size() >= 2;
        for (size_t i = 1; capsuleClear && i < path.size(); ++i)
            capsuleClear = SegmentClear(grid.MapPtr, grid.Phase, path[i - 1], path[i], 0.5f);

        if (!capsuleClear)
            path = points;

        RemoveCollinear(path);

        // The capsule is advisory for a hull window, but the floor and centreline are not.
        // A path that dives below the seabed or passes through a wall must never be followed.
        bool const pathSafe =
            PathFloorSafe(grid.MapPtr, grid.Phase, path, kHardFloorClearance) &&
            PathCenterLineSafe(grid.MapPtr, grid.Phase, path);
        if (!pathSafe)
        {
            RemoveCollinear(points);
            if (!PathFloorSafe(grid.MapPtr, grid.Phase, points, kHardFloorClearance) ||
                !PathCenterLineSafe(grid.MapPtr, grid.Phase, points))
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

bool Nav3DManager::FindPath(Map* map, uint32 phase, Position const& start, Position const& goal,
                            Movement::PointsArray& path, Nav3DResult& result)
{
    result = {};
    path.clear();

    if (!map || !sPlayerbotAIConfig.Nav3DEnabled || !sPlayerbotAIConfig.Nav3DLazy)
        return false;

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
                                    Movement::PointsArray& path, Nav3DResult& result)
{
    result = {};
    path.clear();

    if (!map || !sPlayerbotAIConfig.Nav3DEnabled || !sPlayerbotAIConfig.Nav3DLazy)
        return false;

    bool const openWater = IsOpenWaterArea(map, phase, start) && IsOpenWaterArea(map, phase, goal);
    bool const detailGrid = !openWater && (NeedsDetailGrid(map, phase, start) || NeedsDetailGrid(map, phase, goal));
    float const detailCellSize = std::clamp(sPlayerbotAIConfig.Nav3DCellSize, kMinCellSize, kMaxCellSize);
    float const cellSize = detailGrid ? detailCellSize : std::min(kMaxCellSize, detailCellSize * 2.0f);
    result.CellSize = cellSize;

    Grid grid;
    grid.MapPtr = map;
    grid.Phase = phase;
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
    bool const startInWater = map->IsUnderWater(phase, start.GetPositionX(), start.GetPositionY(),
                                                start.GetPositionZ(), 1.0f) ||
                              map->IsInWater(phase, start.GetPositionX(), start.GetPositionY(), start.GetPositionZ(),
                                             1.0f) ||
                              (startLevel > INVALID_HEIGHT && start.GetPositionZ() <= startLevel + kSurfaceMargin);
    grid.RestrictSky = startInWater;

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

    if (SegmentClear(map, phase, {start.GetPositionX(), start.GetPositionY(), start.GetPositionZ()},
                     {goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ()}, 0.5f))
    {
        path.push_back({start.GetPositionX(), start.GetPositionY(), start.GetPositionZ()});
        path.push_back({goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ()});
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

    NodeState& startState = states[startNode];
    startState.G = 0.0f;
    startState.F = heuristic(startNode);
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
            if (!LineClear(map, phase, currentCenter, neighborCenter))
                continue;

            float const tentative = currentState.G + Distance(currentCenter, neighborCenter);
            if (tentative >= neighborState.G)
                continue;

            neighborState.G = tentative;
            neighborState.Parent = current.Node;
            neighborState.HasParent = true;
            neighborState.F = tentative + heuristic(neighbor);
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

    if (!map || !sPlayerbotAIConfig.Nav3DEnabled || !sPlayerbotAIConfig.Nav3DLazy)
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
                ? currentCenter.GetPositionZ() >= liquid.Level - 0.5f
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
            if (!LineClear(map, phase, currentCenter, neighborCenter))
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
