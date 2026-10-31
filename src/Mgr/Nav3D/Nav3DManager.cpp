/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Nav3DManager.h"

#include "AreaDefines.h"
#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "DBCStores.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "StringFormat.h"
#include "Timer.h"
#include "World.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
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
    constexpr int32 kMaxAirCellsPerAxis = 4096;
    constexpr float kAirCompositeCell = 4.0f;
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
    // The straight-line pre-check samples collision every 0.5 yd. Long air/water routes are
    // left to A* (which is time-sliced) instead of blocking the map thread on one huge LOS.
    constexpr float kMaxDirectCheckDistance = 300.0f;
    // Transit paths prefer legal air over a no-fly crossing. The multiplier only
    // biases A*; the post-path exposure check is the final gate.
    constexpr float kRestrictedAirPenalty = 8.0f;
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
        int8 Restricted = 0;
    };

    struct QueueNode
    {
        float F = 0.0f;
        float G = 0.0f;
        GridNode Node;
    };

    bool operator>(QueueNode const& left, QueueNode const& right)
    {
        // Prefer the deeper node on equal f: in a huge open volume most frontier nodes
        // share nearly the same f, and sinking toward the goal avoids expanding a shell.
        if (left.F != right.F)
            return left.F > right.F;
        return left.G < right.G;
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
        bool Air = false;
        bool AirLos = true;
        bool PreferUp = false;
        Nav3DTile const* Tile = nullptr;
        mutable std::unordered_map<uint64_t, std::shared_ptr<Nav3DTile>> AirTiles;
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

    // Exact 26-connected grid distance: face, edge-diagonal and corner-diagonal costs.
    // It is a tighter admissible bound than Euclidean distance and matches the edge costs
    // used by the A* (1 / sqrt(2) / sqrt(3)).
    float Octile3D(G3D::Vector3 const& from, Position const& to)
    {
        float const dx = std::fabs(from.x - to.GetPositionX());
        float const dy = std::fabs(from.y - to.GetPositionY());
        float const dz = std::fabs(from.z - to.GetPositionZ());
        float const high = std::max({dx, dy, dz});
        float const low = std::min({dx, dy, dz});
        float const mid = dx + dy + dz - high - low;
        return (high - mid) + (mid - low) * 1.41421356f + low * 1.73205081f;
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

    bool TileLineClearLive(Nav3DTile const* tile, Map* map, uint32 phase, G3D::Vector3 const& from,
                           G3D::Vector3 const& to)
    {
        if (!TileLineClear(tile, from, to, kTileBodyRadius))
            return false;
        // Local air-detail tiles are 4-yd WMO/cave boxes. Their baked air spans can leak
        // through thin WMO shells (Grizzlemaw's tree wall), so a detail edge must always
        // pass the live VMAP check as well, regardless of the tile flag.
        if ((tile->Header().Flags & Nav3D::FlagLiveLos) ||
            tile->Header().Medium == Nav3D::MediumAirDetail)
            return LineClear(map, phase, from, to);
        return true;
    }

    std::shared_ptr<Nav3DTile> GetAirTileAt(Grid const& grid, float x, float y)
    {
        constexpr float gridSize = 533.3333f;
        int32 const tileX = int32(std::floor(32.0f - x / gridSize));
        int32 const tileY = int32(std::floor(32.0f - y / gridSize));
        if (tileX < 0 || tileY < 0 || tileX >= 64 || tileY >= 64)
            return nullptr;
        uint64 const key = (uint64(grid.MapPtr->GetId()) << 42) |
                           (uint64(uint16(tileX)) << 21) | uint64(uint16(tileY));
        auto const cached = grid.AirTiles.find(key);
        if (cached != grid.AirTiles.end())
            return cached->second;
        std::shared_ptr<Nav3DTile> tile = Nav3DManager::Instance().GetTile(
            grid.MapPtr->GetId(), uint16(tileX), uint16(tileY), Nav3D::MediumAir);
        grid.AirTiles.emplace(key, tile);
        return tile;
    }

    bool AirTileUsable(Nav3DTile const* tile)
    {
        if (!tile)
            return false;
        Nav3DTileHeader const& header = tile->Header();
        return (header.Flags & Nav3D::FlagHasSolid) && header.GeneratorVersion >= 5;
    }

    bool AirNavigable(Grid const& grid, Position const& position)
    {
        std::shared_ptr<Nav3DTile> tile =
            GetAirTileAt(grid, position.GetPositionX(), position.GetPositionY());
        if (!AirTileUsable(tile.get()))
            return false;
        return tile->IsNavigable(position.GetPositionX(), position.GetPositionY(),
                                 position.GetPositionZ(), kTileBodyRadius);
    }

    // A multi-tile edge can cross several air tiles. Sample the same 0.5-yd line the single
    // tile search uses, choosing the tile by world position at every sample, then run one
    // VMAP centreline check for the whole edge.
    bool AirLineClearLive(Grid const& grid, G3D::Vector3 const& from, G3D::Vector3 const& to,
                          bool liveLos = true)
    {
        float const dx = to.x - from.x;
        float const dy = to.y - from.y;
        float const dz = to.z - from.z;
        float const distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        int const steps = std::max(1, int(std::ceil(distance / 1.0f)));
        for (int i = 0; i <= steps; ++i)
        {
            G3D::Vector3 const point = from + (to - from) * (float(i) / float(steps));
            std::shared_ptr<Nav3DTile> tile = GetAirTileAt(grid, point.x, point.y);
            if (!AirTileUsable(tile.get()) ||
                !tile->IsNavigable(point.x, point.y, point.z, kTileBodyRadius))
                return false;
        }
        return !liveLos || LineClear(grid.MapPtr, grid.Phase, from, to);
    }

    bool GridLineClear(Grid const& grid, Map* map, uint32 phase, G3D::Vector3 const& from,
                       G3D::Vector3 const& to)
    {
        if (grid.Air)
            return AirLineClearLive(grid, from, to, grid.AirLos);
        if (grid.Tile)
            return TileLineClearLive(grid.Tile, map, phase, from, to);
        return LineClear(map, phase, from, to) && SolidTileLineClear(grid, from, to);
    }

    bool GridLineClear(Grid const& grid, Map* map, uint32 phase, Position const& from,
                       Position const& to)
    {
        G3D::Vector3 const fromPoint{from.GetPositionX(), from.GetPositionY(), from.GetPositionZ()};
        G3D::Vector3 const toPoint{to.GetPositionX(), to.GetPositionY(), to.GetPositionZ()};
        if (grid.Air)
            return AirLineClearLive(grid, fromPoint, toPoint, grid.AirLos);
        if (grid.Tile)
            return TileLineClearLive(grid.Tile, map, phase, fromPoint, toPoint);
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
    // One capsule sample. Split out of SegmentClear so the cooperative search can yield
    // between samples instead of blocking the map thread on a long swim.
    bool SegmentPointClear(Map* map, uint32 phase, G3D::Vector3 const& point, float radius)
    {
        static G3D::Vector3 const offsets[] = {
            {0.5f, 0.0f, 0.0f},   {-0.5f, 0.0f, 0.0f},  {0.0f, 0.5f, 0.0f},
            {0.0f, -0.5f, 0.0f},  {0.3535f, 0.3535f, 0.0f}, {-0.3535f, 0.3535f, 0.0f},
            {0.3535f, -0.3535f, 0.0f}, {-0.3535f, -0.3535f, 0.0f},
            {0.0f, 0.0f, 1.0f},   {0.0f, 0.0f, -1.0f}};

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
        return true;
    }

    bool SegmentClear(Map* map, uint32 phase, G3D::Vector3 const& from, G3D::Vector3 const& to,
                      float radius)
    {
        float const dx = to.x - from.x;
        float const dy = to.y - from.y;
        float const dz = to.z - from.z;
        float const distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        int const steps = std::max(1, int(std::ceil(distance / 0.5f)));

        for (int i = 0; i <= steps; ++i)
        {
            float const t = float(i) / float(steps);
            G3D::Vector3 const point = from + (to - from) * t;
            if (!SegmentPointClear(map, phase, point, radius))
                return false;
        }

        return true;
    }

    bool IsNavigable(Grid const& grid, GridNode const& node, NodeState& state)
    {
        if (state.Nav != 0)
            return state.Nav > 0;

        Position const center = grid.Center(node);
        if (grid.Air)
        {
            bool const navigable = AirNavigable(grid, center);
            state.Nav = navigable ? 1 : -1;
            return navigable;
        }

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

        bool const airGrid =
            grid.Air || (grid.Tile && (grid.Tile->Header().Medium == Nav3D::MediumAir ||
                                       grid.Tile->Header().Medium == Nav3D::MediumAirDetail));
        if (airGrid)
            state.Restricted = Nav3DManager::IsRestrictedFlightAt(grid.MapPtr, grid.Phase, center) ? 1 : -1;

        state.Nav = navigable ? 1 : -1;
        return navigable;
    }

    bool FindNearestNavigable(Grid const& grid, Position const& position, GridNode& result,
                              std::unordered_map<GridNode, NodeState, GridNodeHash>& states)
    {
        GridNode const snapped = grid.Snap(position);
        // A flying bot on a bridge/deck should lift off, not snap down through the
        // deck/rail. Prefer the nearest air cell above before searching below.
        if (grid.PreferUp)
        {
            for (int32 zoff = 0; zoff <= 4; ++zoff)
            {
                for (int32 radius = 0; radius <= 2; ++radius)
                {
                    for (int32 dx = -radius; dx <= radius; ++dx)
                    {
                        for (int32 dy = -radius; dy <= radius; ++dy)
                        {
                            if (std::max(std::abs(dx), std::abs(dy)) != radius)
                                continue;
                            GridNode const candidate{snapped.X + dx, snapped.Y + dy,
                                                     snapped.Z + zoff};
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
        }

        static constexpr int32 preferUpZ[5] = {0, 1, -1, 2, -2};
        for (int32 radius = 0; radius <= 2; ++radius)
        {
            for (int32 dx = -radius; dx <= radius; ++dx)
            {
                for (int32 dy = -radius; dy <= radius; ++dy)
                {
                    for (int32 zi = 0; zi < 5; ++zi)
                    {
                        int32 const dz = grid.PreferUp ? preferUpZ[zi] : (zi - 2);
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

        // An air path starts and ends on the ground, which is below the first baked air cell
        // (the bake keeps the body half-height out of the floor). The lift off and the final
        // descent are vertical ground-contact edges, so validate those two edges with map LOS
        // instead of the tile body band. Longer endpoint edges still use the tile volume.
        auto const edgeClear = [&grid](G3D::Vector3 const& from, G3D::Vector3 const& to,
                                       bool endpointEdge)
        {
            bool const airPath =
                grid.Air || (grid.Tile && (grid.Tile->Header().Medium == Nav3D::MediumAir ||
                                            grid.Tile->Header().Medium == Nav3D::MediumAirDetail));
            if (endpointEdge)
            {
                float const dx = to.x - from.x;
                float const dy = to.y - from.y;
                if (dx * dx + dy * dy <= 64.0f)
                {
                    if (LineClear(grid.MapPtr, grid.Phase, from, to))
                        return true;
                    // A flying bot can lift off/land through the short solid band that the
                    // air tile keeps under its feet. Do not let a bridge rail/deck GO veto
                    // the first/last vertical contact edge; every other edge still gets
                    // full Map LOS.
                    if (airPath)
                        return true;
                }
            }
            if (grid.Air)
                return AirLineClearLive(grid, from, to);
            return TileLineClearLive(grid.Tile, grid.MapPtr, grid.Phase, from, to);
        };

        bool capsuleClear = path.size() >= 2;
        if (grid.Air || grid.Tile)
        {
            for (size_t i = 1; capsuleClear && i < path.size(); ++i)
            {
                bool const endpointEdge = i == 1 || i + 1 == path.size();
                capsuleClear = edgeClear(path[i - 1], path[i], endpointEdge);
            }
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
        if (grid.Air || grid.Tile)
        {
            pathSafe = path.size() >= 2;
            for (size_t i = 1; pathSafe && i < path.size(); ++i)
            {
                bool const endpointEdge = i == 1 || i + 1 == path.size();
                pathSafe = edgeClear(path[i - 1], path[i], endpointEdge);
            }
        }
        else
            pathSafe = PathFloorSafe(grid.MapPtr, grid.Phase, path, kHardFloorClearance) &&
                       PathCenterLineSafe(grid.MapPtr, grid.Phase, path);

        if (!pathSafe)
        {
            RemoveCollinear(points);
            bool rawSafe = false;
            if (grid.Air || grid.Tile)
            {
                rawSafe = points.size() >= 2;
                for (size_t i = 1; rawSafe && i < points.size(); ++i)
                {
                    bool const endpointEdge = i == 1 || i + 1 == points.size();
                    rawSafe = edgeClear(points[i - 1], points[i], endpointEdge);
                }
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

    // Candidate order mirrors FindNearestNavigable exactly, but the caller can iterate it
    // across ticks instead of resolving every candidate in one constructor call.
    std::vector<GridNode> BuildNearestCandidates(Grid const& grid, Position const& position)
    {
        std::vector<GridNode> candidates;
        GridNode const snapped = grid.Snap(position);
        auto const add = [&candidates, &grid](GridNode const& candidate)
        {
            if (grid.Contains(candidate))
                candidates.push_back(candidate);
        };

        if (grid.PreferUp)
        {
            for (int32 zoff = 0; zoff <= 4; ++zoff)
            {
                for (int32 radius = 0; radius <= 2; ++radius)
                {
                    for (int32 dx = -radius; dx <= radius; ++dx)
                    {
                        for (int32 dy = -radius; dy <= radius; ++dy)
                        {
                            if (std::max(std::abs(dx), std::abs(dy)) != radius)
                                continue;
                            add({snapped.X + dx, snapped.Y + dy, snapped.Z + zoff});
                        }
                    }
                }
            }
        }

        static constexpr int32 preferUpZ[5] = {0, 1, -1, 2, -2};
        for (int32 radius = 0; radius <= 2; ++radius)
        {
            for (int32 dx = -radius; dx <= radius; ++dx)
            {
                for (int32 dy = -radius; dy <= radius; ++dy)
                {
                    for (int32 zi = 0; zi < 5; ++zi)
                    {
                        int32 const dz = grid.PreferUp ? preferUpZ[zi] : (zi - 2);
                        if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) != radius)
                            continue;
                        add({snapped.X + dx, snapped.Y + dy, snapped.Z + dz});
                    }
                }
            }
        }

        return candidates;
    }

// Resumable A* core. The constructor performs the bounded grid/setup work; Step()
// expands whole nodes until the caller's microsecond budget is exhausted. The
// search never calls Map/vmap from a worker: it only runs on the requesting map
// thread (or on an immutable tile-only worker).
class Nav3DSearch
{
public:
    Nav3DSearch(Map* map, uint32 phase, Position const& start, Position const& goal,
                std::shared_ptr<Nav3DTile> tile, bool airComposite, float overrideCellSize,
                Nav3DSearchBounds const* bounds, bool penalizeRestricted = false)
        : _map(map), _phase(phase), _start(start), _goal(goal), _tile(std::move(tile)),
          _airComposite(airComposite), _overrideCellSize(overrideCellSize),
          _penalizeRestricted(penalizeRestricted)
    {
        struct SetupGuard
        {
            std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
            uint64& Out;
            explicit SetupGuard(uint64& out) : Out(out) {}
            ~SetupGuard()
            {
                Out += uint64(std::max<int64>(0, std::chrono::duration_cast<std::chrono::microseconds>(
                                                     std::chrono::steady_clock::now() - Start)
                                                     .count()));
            }
        } setupGuard{_setupUs};

        if (bounds)
            _bounds = *bounds;
        _result = {};
        if (!_map || !sPlayerbotAIConfig.Nav3DEnabled)
        {
            _done = true;
            return;
        }

        bool const openWater = !_tile && !_airComposite && IsOpenWaterArea(_map, _phase, _start) &&
                               IsOpenWaterArea(_map, _phase, _goal);

        _grid.MapPtr = _map;
        _grid.Phase = _phase;

        if (_tile)
        {
            Nav3DTileHeader const& header = _tile->Header();
            float const tileMaxX = header.OriginX + float(header.SizeX) * header.CellSize;
            float const tileMaxY = header.OriginY + float(header.SizeY) * header.CellSize;
            float const tileMaxZ = header.OriginZ + float(header.SizeZ) * header.CellSize;

            _grid.Tile = _tile.get();
            float const searchCell = _overrideCellSize > 0.0f ? _overrideCellSize : header.CellSize;
            _grid.Cell = searchCell;
            _grid.PreferUp = header.Medium == Nav3D::MediumAir || header.Medium == Nav3D::MediumAirDetail;

            auto const alignDown = [searchCell](float value, float origin)
            {
                return origin + std::floor((value - origin) / searchCell) * searchCell;
            };

            if (header.Medium == Nav3D::MediumAirDetail)
            {
                float const minX = _bounds ? std::max(header.OriginX, _bounds->MinX) : header.OriginX;
                float const minY = _bounds ? std::max(header.OriginY, _bounds->MinY) : header.OriginY;
                float const minZ = _bounds ? std::max(header.OriginZ, _bounds->MinZ) : header.OriginZ;
                float const maxX = _bounds ? std::min(tileMaxX, _bounds->MaxX) : tileMaxX;
                float const maxY = _bounds ? std::min(tileMaxY, _bounds->MaxY) : tileMaxY;
                float const maxZ = _bounds ? std::min(tileMaxZ, _bounds->MaxZ) : tileMaxZ;
                _grid.MinX = alignDown(minX, header.OriginX);
                _grid.MinY = alignDown(minY, header.OriginY);
                _grid.MinZ = alignDown(minZ, header.OriginZ);
                _grid.SizeX = std::max(1, int32(std::ceil((maxX - _grid.MinX) / searchCell)));
                _grid.SizeY = std::max(1, int32(std::ceil((maxY - _grid.MinY) / searchCell)));
                _grid.SizeZ = std::max(1, int32(std::ceil((maxZ - _grid.MinZ) / searchCell)));
            }
            else
            {
                _grid.MinX = alignDown(std::max(header.OriginX,
                                                std::min(_start.GetPositionX(), _goal.GetPositionX()) - kXYPad),
                                       header.OriginX);
                _grid.MinY = alignDown(std::max(header.OriginY,
                                                std::min(_start.GetPositionY(), _goal.GetPositionY()) - kXYPad),
                                       header.OriginY);
                _grid.MinZ = alignDown(std::max(header.OriginZ,
                                                std::min(_start.GetPositionZ(), _goal.GetPositionZ()) - kZPad),
                                       header.OriginZ);
                float const maxX = std::min(tileMaxX,
                                            std::max(_start.GetPositionX(), _goal.GetPositionX()) + kXYPad);
                float const maxY = std::min(tileMaxY,
                                            std::max(_start.GetPositionY(), _goal.GetPositionY()) + kXYPad);
                float const maxZ = std::min(tileMaxZ,
                                            std::max(_start.GetPositionZ(), _goal.GetPositionZ()) + kZPad);
                _grid.SizeX = std::max(1, int32(std::ceil((maxX - _grid.MinX) / searchCell)));
                _grid.SizeY = std::max(1, int32(std::ceil((maxY - _grid.MinY) / searchCell)));
                _grid.SizeZ = std::max(1, int32(std::ceil((maxZ - _grid.MinZ) / searchCell)));
            }
            _result.CellSize = searchCell;
        }
        else if (_airComposite)
        {
            _grid.Air = true;
            _grid.PreferUp = true;
            _grid.Cell = kAirCompositeCell;
            _result.CellSize = kAirCompositeCell;

            float const dx = _goal.GetPositionX() - _start.GetPositionX();
            float const dy = _goal.GetPositionY() - _start.GetPositionY();
            float const horizontal = std::sqrt(dx * dx + dy * dy);
            float const margin = std::clamp(horizontal * 0.05f, 40.0f, 250.0f);
            float const minX = std::min(_start.GetPositionX(), _goal.GetPositionX()) - margin;
            float const minY = std::min(_start.GetPositionY(), _goal.GetPositionY()) - margin;
            float const maxX = std::max(_start.GetPositionX(), _goal.GetPositionX()) + margin;
            float const maxY = std::max(_start.GetPositionY(), _goal.GetPositionY()) + margin;

            float minZ = std::min(_start.GetPositionZ(), _goal.GetPositionZ()) - 150.0f;
            float maxZ = std::max(_start.GetPositionZ(), _goal.GetPositionZ()) + 150.0f;
            // Cap the height sampling: a 12,000-yd route used 240 vmap GetHeight calls
            // in the constructor. 100-yd spacing and a hard 32-sample cap are enough to
            // bound the composite grid without turning the constructor into a stall.
            int const samples = std::max(2, std::min(32, int(std::ceil(horizontal / 100.0f))));
            for (int i = 0; i <= samples; ++i)
            {
                float const t = float(i) / float(samples);
                float const x = _start.GetPositionX() + dx * t;
                float const y = _start.GetPositionY() + dy * t;
                float const ground = _map->GetHeight(_phase, x, y, 4000.0f, true, 4000.0f);
                if (ground > -1000.0f)
                    maxZ = std::max(maxZ, ground + 150.0f);
            }
            minZ = std::max(-1000.0f, minZ);
            maxZ = std::min(4000.0f, maxZ);

            auto const alignDown = [](float value)
            { return std::floor(value / kAirCompositeCell) * kAirCompositeCell; };
            _grid.MinX = alignDown(minX);
            _grid.MinY = alignDown(minY);
            _grid.MinZ = alignDown(minZ);
            _grid.SizeX = std::max(1, int32(std::ceil((maxX - _grid.MinX) / kAirCompositeCell)));
            _grid.SizeY = std::max(1, int32(std::ceil((maxY - _grid.MinY) / kAirCompositeCell)));
            _grid.SizeZ = std::max(1, int32(std::ceil((maxZ - _grid.MinZ) / kAirCompositeCell)));
            if (_grid.SizeX > kMaxAirCellsPerAxis || _grid.SizeY > kMaxAirCellsPerAxis ||
                _grid.SizeZ > kMaxAirCellsPerAxis)
            {
                _done = true;
                return;
            }
        }
        else
        {
            bool const detailGrid = !openWater &&
                (NeedsDetailGrid(_map, _phase, _start) || NeedsDetailGrid(_map, _phase, _goal));
            float const detailCellSize = std::clamp(sPlayerbotAIConfig.Nav3DCellSize, kMinCellSize, kMaxCellSize);
            float const cellSize = detailGrid ? detailCellSize : std::min(kMaxCellSize, detailCellSize * 2.0f);
            _result.CellSize = cellSize;

            _grid.Cell = cellSize;
            float const minX = std::min(_start.GetPositionX(), _goal.GetPositionX()) - kXYPad;
            float const minY = std::min(_start.GetPositionY(), _goal.GetPositionY()) - kXYPad;
            float const minZ = std::min(_start.GetPositionZ(), _goal.GetPositionZ()) - kZPad;
            float const maxX = std::max(_start.GetPositionX(), _goal.GetPositionX()) + kXYPad;
            float const maxY = std::max(_start.GetPositionY(), _goal.GetPositionY()) + kXYPad;
            float const maxZ = std::max(_start.GetPositionZ(), _goal.GetPositionZ()) + kZPad;

            _grid.MinX = minX;
            _grid.MinY = minY;
            _grid.MinZ = minZ;
            _grid.SizeX = std::max(1, std::min(kMaxCellsPerAxis, int32(std::ceil((maxX - minX) / cellSize))));
            _grid.SizeY = std::max(1, std::min(kMaxCellsPerAxis, int32(std::ceil((maxY - minY) / cellSize))));
            _grid.SizeZ = std::max(1, std::min(kMaxCellsPerAxis, int32(std::ceil((maxZ - minZ) / cellSize))));

            float const startLevel = _map->GetWaterLevel(_start.GetPositionX(), _start.GetPositionY());
            bool const startInWater =
                _map->IsUnderWater(_phase, _start.GetPositionX(), _start.GetPositionY(),
                                   _start.GetPositionZ(), 1.0f) ||
                _map->IsInWater(_phase, _start.GetPositionX(), _start.GetPositionY(),
                                _start.GetPositionZ(), 1.0f) ||
                (startLevel > INVALID_HEIGHT && _start.GetPositionZ() <= startLevel + kSurfaceMargin);
            _grid.RestrictSky = startInWater;
        }

        if (!_tile && !_airComposite)
        {
            _grid.SolidTile = ResolveSolidTile(_map, _start);
            if (!_grid.SolidTile)
                _grid.SolidTile = ResolveSolidTile(_map, _goal);
        }

        auto const inside = [this](Position const& position)
        {
            return position.GetPositionX() >= _grid.MinX &&
                   position.GetPositionX() <= _grid.MinX + _grid.SizeX * _grid.Cell &&
                   position.GetPositionY() >= _grid.MinY &&
                   position.GetPositionY() <= _grid.MinY + _grid.SizeY * _grid.Cell &&
                   position.GetPositionZ() >= _grid.MinZ &&
                   position.GetPositionZ() <= _grid.MinZ + _grid.SizeZ * _grid.Cell;
        };
        if (!inside(_start) || !inside(_goal))
        {
            _done = true;
            return;
        }

        _maxNodes = std::max<uint32>(1, std::min<uint32>(kHardMaxNodes, sPlayerbotAIConfig.Nav3DMaxNodes));
        _maxTimeMs = std::max<uint32>(1, std::min<uint32>(kHardMaxTimeMs, sPlayerbotAIConfig.Nav3DMaxSearchTimeMs));
        if (_airComposite)
        {
            _maxTimeMs = std::min<uint32>(_maxTimeMs, 300);
            _maxNodes = std::min<uint32>(_maxNodes, 300000);
        }

        _states.reserve(4096);
        _startCandidates = BuildNearestCandidates(_grid, _start);
        _goalCandidates = BuildNearestCandidates(_grid, _goal);
        _stage = Stage::NearestStart;
    }

    using Clock = std::chrono::steady_clock;

    bool StepCandidates(std::vector<GridNode> const& candidates, size_t& cursor,
                        GridNode& result, bool& found, Clock::time_point deadline)
    {
        while (cursor < candidates.size())
        {
            GridNode const candidate = candidates[cursor];
            NodeState& state = _states[candidate];
            if (IsNavigable(_grid, candidate, state))
            {
                result = candidate;
                found = true;
                return true;
            }
            ++cursor;
            if (Clock::now() >= deadline)
                return false;
        }
        found = false;
        return true;
    }

    bool StepNearest(uint64 budgetUs)
    {
        Clock::time_point const deadline = Clock::now() + std::chrono::microseconds(budgetUs);
        if (_stage == Stage::NearestStart)
        {
            bool found = false;
            if (!StepCandidates(_startCandidates, _startCursor, _startNode, found, deadline))
                return false;
            if (!found)
            {
                FailNearest();
                return true;
            }
            _stage = Stage::NearestGoal;
        }

        if (_stage == Stage::NearestGoal)
        {
            bool found = false;
            if (!StepCandidates(_goalCandidates, _goalCursor, _goalNode, found, deadline))
                return false;
            if (!found)
            {
                FailNearest();
                return true;
            }
            _stage = Stage::Direct;
            InitDirect();
        }
        return true;
    }

    void FailNearest()
    {
        LOG_DEBUG("playerbots",
                  "nav3d nearest navigable failed map {} tile={} start=({:.1f},{:.1f},{:.1f}) "
                  "goal=({:.1f},{:.1f},{:.1f})",
                  _map->GetId(), _tile ? 1 : 0, _start.GetPositionX(), _start.GetPositionY(),
                  _start.GetPositionZ(), _goal.GetPositionX(), _goal.GetPositionY(), _goal.GetPositionZ());
        _done = true;
        _stage = Stage::Done;
    }

    void InitDirect()
    {
        _directFrom = {_start.GetPositionX(), _start.GetPositionY(), _start.GetPositionZ()};
        _directTo = {_goal.GetPositionX(), _goal.GetPositionY(), _goal.GetPositionZ()};
        float const distance = Distance(_start, _goal);
        if (distance > kMaxDirectCheckDistance)
        {
            InitAStar();
            return;
        }
        float const stepSize = _airComposite ? 1.0f : 0.5f;
        _directSteps = std::max(1, int(std::ceil(distance / stepSize)));
        _directIndex = 0;
    }

    bool StepDirect(uint64 budgetUs)
    {
        Clock::time_point const deadline = Clock::now() + std::chrono::microseconds(budgetUs);
        bool first = true;
        while (_directIndex <= _directSteps)
        {
            float const t = float(_directIndex) / float(_directSteps);
            G3D::Vector3 const point = _directFrom + (_directTo - _directFrom) * t;
            bool clear = true;
            if (_tile)
                clear = _tile->IsNavigable(point.x, point.y, point.z, kTileBodyRadius);
            else if (_airComposite)
            {
                std::shared_ptr<Nav3DTile> airTile = GetAirTileAt(_grid, point.x, point.y);
                clear = AirTileUsable(airTile.get()) &&
                        airTile->IsNavigable(point.x, point.y, point.z, kTileBodyRadius);
            }
            else
                clear = SegmentPointClear(_map, _phase, point, 0.5f);

            if (!clear)
            {
                InitAStar();
                return true;
            }
            ++_directIndex;
            if (!first && Clock::now() >= deadline)
                return false;
            first = false;
        }

        bool clear = true;
        if (_tile)
            clear = !(_tile->Header().Flags & Nav3D::FlagLiveLos) ||
                    LineClear(_map, _phase, _directFrom, _directTo);
        else if (_airComposite)
            clear = LineClear(_map, _phase, _directFrom, _directTo);

        if (!clear)
        {
            InitAStar();
            return true;
        }

        _path.push_back(_directFrom);
        _path.push_back(_directTo);
        _result.Found = true;
        _result.Direct = true;
        _result.Nodes = uint32(_states.size());
        _result.ElapsedMs = uint32(_workUs / 1000);
        _result.Length = Distance(_start, _goal);
        _done = true;
        _found = true;
        _stage = Stage::Done;
        return true;
    }

    void InitAStar()
    {
        if (_airComposite)
            _grid.AirLos = false;

        _heuristicWeight =
            (_grid.Tile && _grid.Tile->Header().Medium == Nav3D::MediumAirDetail) ? 1.0f
            : (_grid.Tile || _grid.Air)                                             ? 2.0f
                                                                                    : 1.0f;

        NodeState& startState = _states[_startNode];
        startState.G = 0.0f;
        startState.F = Heuristic(_startNode) * _heuristicWeight;
        startState.HasParent = false;
        _open.push({startState.F, 0.0f, _startNode});
        _neighbors = BuildNeighborOffsets(Nav3DNeighborCount());
        _startedMs = getMSTime();
        _result.BestDistance = std::numeric_limits<float>::max();
        _expanding = false;
        _expandingEdge = 0;
        _stage = Stage::AStar;
    }

    void SetTimeBudget(uint32 ms)
    {
        _maxTimeMs = std::min<uint32>(_maxTimeMs,
                                      std::max<uint32>(1, std::min<uint32>(kHardMaxTimeMs, ms)));
    }

    bool Step(uint64 budgetUs)
    {
        if (_done)
            return true;
        if (!budgetUs)
            return false;

        Clock::time_point const started = Clock::now();
        bool const finished = StepInternal(budgetUs);
        _workUs += uint64(std::max<int64>(
            0, std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count()));
        return finished;
    }

    // Expand one A* node's neighbour list, resuming from _expandingEdge. A single air
    // composite node can contain 26 live-LOS edges; checking the deadline only between
    // nodes let one node run for tens of milliseconds.
    bool ExpandCurrentNode(Clock::time_point deadline)
    {
        NodeState& currentState = _states[_expandingNode];
        Position const center = _expandingCenter;
        auto const timeUp = [&deadline]() { return Clock::now() >= deadline; };

        while (_expandingEdge < _neighbors.size())
        {
            NeighborOffset const offset = _neighbors[_expandingEdge++];
            GridNode const neighbor{_expandingNode.X + offset.X, _expandingNode.Y + offset.Y,
                                    _expandingNode.Z + offset.Z};
            if (!_grid.Contains(neighbor))
            {
                if (timeUp())
                    return false;
                continue;
            }

            auto const known = _states.find(neighbor);
            if (known != _states.end() && known->second.Closed)
            {
                if (timeUp())
                    return false;
                continue;
            }

            Position const neighborCenter = _grid.Center(neighbor);
            NodeState probe;
            if (known != _states.end())
                probe = known->second;
            if (!IsNavigable(_grid, neighbor, probe))
            {
                if (known != _states.end())
                    known->second.Nav = -1;
                if (timeUp())
                    return false;
                continue;
            }

            float edgeCost = Distance(center, neighborCenter);
            if (_penalizeRestricted && probe.Restricted > 0)
                edgeCost *= kRestrictedAirPenalty;
            float const tentative = currentState.G + edgeCost;
            if (known != _states.end() && tentative >= known->second.G)
            {
                if (timeUp())
                    return false;
                continue;
            }

            if (!GridLineClear(_grid, _map, _phase, center, neighborCenter))
            {
                if (timeUp())
                    return false;
                continue;
            }

            NodeState& neighborState = (known != _states.end()) ? known->second : _states[neighbor];
            neighborState.G = tentative;
            neighborState.Parent = _expandingNode;
            neighborState.HasParent = true;
            neighborState.Nav = 1;
            neighborState.F = tentative + Heuristic(neighbor) * _heuristicWeight;
            _open.push({neighborState.F, tentative, neighbor});

            if (timeUp())
                return false;
        }

        _expanding = false;
        _expandingEdge = 0;
        return true;
    }

    bool StepInternal(uint64 budgetUs)
    {
        if (_stage == Stage::NearestStart || _stage == Stage::NearestGoal)
        {
            auto const phaseStart = Clock::now();
            bool const ok = StepNearest(budgetUs);
            _nearestUs += uint64(std::max<int64>(0, std::chrono::duration_cast<std::chrono::microseconds>(
                                                         Clock::now() - phaseStart)
                                                         .count()));
            if (!ok)
                return false;
        }
        if (_stage == Stage::Direct)
        {
            auto const phaseStart = Clock::now();
            bool const ok = StepDirect(budgetUs);
            _directUs += uint64(std::max<int64>(0, std::chrono::duration_cast<std::chrono::microseconds>(
                                                        Clock::now() - phaseStart)
                                                        .count()));
            if (!ok)
                return false;
        }
        if (_stage != Stage::AStar)
            return _done;

        if (_open.empty())
        {
            Finish();
            return _done;
        }

        struct AStarGuard
        {
            std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
            uint64& Out;
            explicit AStarGuard(uint64& out) : Out(out) {}
            ~AStarGuard()
            {
                Out += uint64(std::max<int64>(0, std::chrono::duration_cast<std::chrono::microseconds>(
                                                     std::chrono::steady_clock::now() - Start)
                                                     .count()));
            }
        } astarGuard{_astarUs};

        auto const deadline = std::chrono::steady_clock::now() +
                              std::chrono::microseconds(budgetUs);
        // Always make at least one expansion: the caller's slice can be consumed by the
        // clock read itself, and a zero-progress step must never spin the scheduler.
        bool first = true;
        while (!_done && (first || std::chrono::steady_clock::now() < deadline))
        {
            first = false;
            if (_workUs >= uint64(_maxTimeMs) * 1000 || _expanded >= _maxNodes)
            {
                Finish();
                break;
            }

            if (_expanding)
            {
                if (!ExpandCurrentNode(deadline))
                    return false;
                continue;
            }

            if (_open.empty())
            {
                Finish();
                break;
            }

            QueueNode const current = _open.top();
            _open.pop();
            NodeState& currentState = _states[current.Node];
            if (currentState.Closed)
                continue;

            currentState.Closed = true;
            ++_expanded;

            float const currentDistance = Heuristic(current.Node);
            if (currentDistance < _result.BestDistance)
            {
                _result.BestDistance = currentDistance;
                Position const best = _grid.Center(current.Node);
                _result.BestX = best.GetPositionX();
                _result.BestY = best.GetPositionY();
                _result.BestZ = best.GetPositionZ();
                _bestNode = current.Node;
                _hasBest = true;
            }

            if (current.Node == _goalNode)
            {
                _found = true;
                Finish();
                break;
            }

            if (currentDistance <= kEarlyPartialDistance)
            {
                _found = true;
                _result.Partial = true;
                _result.Direct = false;
                _result.BestDistance = currentDistance;
                Position const best = _grid.Center(current.Node);
                _result.BestX = best.GetPositionX();
                _result.BestY = best.GetPositionY();
                _result.BestZ = best.GetPositionZ();
                _goalNode = current.Node;
                Finish();
                break;
            }

            _expanding = true;
            _expandingNode = current.Node;
            _expandingCenter = _grid.Center(current.Node);
            _expandingEdge = 0;
            if (!ExpandCurrentNode(deadline))
                return false;
        }

        if (!_done && _open.empty())
            Finish();
        return _done;
    }

    [[nodiscard]] bool Done() const { return _done; }
    [[nodiscard]] bool Found() const { return _found; }
    [[nodiscard]] uint32 Expanded() const { return _expanded; }
    [[nodiscard]] uint32 Nodes() const { return uint32(_states.size()); }
    [[nodiscard]] uint64 WorkUs() const { return _workUs; }
    [[nodiscard]] uint64 SetupUs() const { return _setupUs; }
    [[nodiscard]] uint64 NearestUs() const { return _nearestUs; }
    [[nodiscard]] uint64 DirectUs() const { return _directUs; }
    [[nodiscard]] uint64 AStarUs() const { return _astarUs; }
    [[nodiscard]] uint64 FinishUs() const { return _finishUs; }
    [[nodiscard]] Map* GetMap() const { return _map; }
    [[nodiscard]] uint32 CreatedMs() const { return _createdMs; }
    [[nodiscard]] Movement::PointsArray const& Path() const { return _path; }
    [[nodiscard]] Nav3DResult const& Result() const { return _result; }

private:
    float Heuristic(GridNode const& node) const
    {
        G3D::Vector3 const center{_grid.Center(node).GetPositionX(), _grid.Center(node).GetPositionY(),
                                  _grid.Center(node).GetPositionZ()};
        return Octile3D(center, _goal);
    }

    void Finish()
    {
        if (_done)
            return;

        _expanding = false;
        _expandingEdge = 0;

        struct FinishGuard
        {
            std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
            uint64& Out;
            explicit FinishGuard(uint64& out) : Out(out) {}
            ~FinishGuard()
            {
                Out += uint64(std::max<int64>(0, std::chrono::duration_cast<std::chrono::microseconds>(
                                                     std::chrono::steady_clock::now() - Start)
                                                     .count()));
            }
        } finishGuard{_finishUs};
        _result.Nodes = uint32(_states.size());
        _result.Expanded = _expanded;
        _result.ElapsedMs = uint32(_workUs / 1000);
        if (_result.BestDistance == std::numeric_limits<float>::max())
            _result.BestDistance = 0.0f;

        if (!_found && _hasBest && _result.BestDistance <= kPartialAcceptDistance)
        {
            _found = true;
            _result.Partial = true;
            _result.Direct = false;
            _goalNode = _bestNode;
        }

        if (!_found)
        {
            LOG_DEBUG("playerbots", "nav3d search failed map {} tile={} air={} expanded={} best={:.1f}",
                      _map->GetId(), _tile ? 1 : 0, _airComposite ? 1 : 0, _expanded,
                      _result.BestDistance);
            _done = true;
            return;
        }

        if (_airComposite)
            _grid.AirLos = true;

        if (!BuildPath(_grid, _states, _startNode, _goalNode, _start, _goal, _result.Partial, _path,
                       _result))
        {
            LOG_DEBUG("playerbots", "nav3d buildpath failed map {} tile={} air={} points={}",
                      _map->GetId(), _tile ? 1 : 0, _airComposite ? 1 : 0, _path.size());
            _found = false;
            _done = true;
            return;
        }

        _result.Found = true;
        _done = true;
    }

    Map* _map = nullptr;
    uint32 _phase = 0;
    Position _start;
    Position _goal;
    enum class Stage
    {
        None,
        NearestStart,
        NearestGoal,
        Direct,
        AStar,
        Done,
    };

    std::shared_ptr<Nav3DTile> _tile;
    bool _airComposite = false;
    bool _penalizeRestricted = false;
    float _overrideCellSize = 0.0f;
    std::optional<Nav3DSearchBounds> _bounds;
    Stage _stage = Stage::None;
    std::vector<GridNode> _startCandidates;
    std::vector<GridNode> _goalCandidates;
    size_t _startCursor = 0;
    size_t _goalCursor = 0;
    G3D::Vector3 _directFrom;
    G3D::Vector3 _directTo;
    int _directSteps = 0;
    int _directIndex = 0;
    Grid _grid;
    std::unordered_map<GridNode, NodeState, GridNodeHash> _states;
    std::priority_queue<QueueNode, std::vector<QueueNode>, std::greater<QueueNode>> _open;
    GridNode _startNode{};
    GridNode _goalNode{};
    GridNode _bestNode{};
    GridNode _expandingNode{};
    Position _expandingCenter;
    size_t _expandingEdge = 0;
    bool _hasBest = false;
    bool _expanding = false;
    bool _found = false;
    bool _done = false;
    uint32 _expanded = 0;
    uint32 _maxNodes = 0;
    uint32 _maxTimeMs = 0;
    uint32 _startedMs = 0;
    uint64 _workUs = 0;
    uint64 _setupUs = 0;
    uint64 _nearestUs = 0;
    uint64 _directUs = 0;
    uint64 _astarUs = 0;
    uint64 _finishUs = 0;
    uint32 _createdMs = getMSTime();
    float _heuristicWeight = 1.0f;
    std::vector<NeighborOffset> _neighbors;
    Movement::PointsArray _path;
    Nav3DResult _result;
};


}
struct Nav3DPathJob
{
    Nav3DPathJob(Map* map, uint32 phase, Position const& start, Position const& goal, bool air,
                 uint64 key)
        : MapPtr(map), Phase(phase), Start(start), Goal(goal), Air(air), Key(key),
          CreatedMs(getMSTime())
    {
        GoalRestricted = Nav3DManager::IsRestrictedFlightAt(MapPtr, Phase, Goal);
    }

    bool Step(uint32 budgetUs)
    {
        if (Done)
            return true;
        if (!budgetUs)
            return false;

        auto const deadline = std::chrono::steady_clock::now() +
                              std::chrono::microseconds(budgetUs);
        while (!Done)
        {
            if (Search)
            {
                auto const left = std::chrono::duration_cast<std::chrono::microseconds>(
                                      deadline - std::chrono::steady_clock::now())
                                      .count();
                if (left <= 0)
                    return false;
                bool const finished = Search->Step(uint64(left));
                if (!finished)
                    return false;
                OnSearchDone();
                continue;
            }

            auto const left = std::chrono::duration_cast<std::chrono::microseconds>(
                                  deadline - std::chrono::steady_clock::now())
                                  .count();
            if (left <= 0)
                return false;

            switch (StageNow)
            {
                case Stage::Tile: SetupTile(); break;
                case Stage::Lazy: Reverse = false; BeginSearch(nullptr); break;
                case Stage::ReverseLazy: Reverse = true; BeginSearch(nullptr); break;
                case Stage::AirDetail: SetupAirDetail(); break;
                case Stage::AirTile: SetupAirTile(); break;
                case Stage::AirComposite: Reverse = false; BeginSearch(nullptr, true); break;
                case Stage::DetailRefine: SetupRefineLeg(); break;
                case Stage::Cruise: StepCruise(); break;
                case Stage::Done: Done = true; break;
            }
        }
        return true;
    }

    [[nodiscard]] bool IsDone() const { return Done; }

    [[nodiscard]] char const* StageName() const
    {
        switch (StageNow)
        {
            case Stage::Tile: return "tile";
            case Stage::Lazy: return "lazy";
            case Stage::ReverseLazy: return "reverse-lazy";
            case Stage::AirDetail: return "air-detail";
            case Stage::AirTile: return "air-tile";
            case Stage::AirComposite: return "air-composite";
            case Stage::DetailRefine: return "detail-refine";
            case Stage::Cruise: return "cruise";
            case Stage::Done: return "done";
        }
        return "?";
    }

    [[nodiscard]] uint32 SearchExpanded() const { return Search ? Search->Expanded() : 0; }
    [[nodiscard]] uint64 SearchWorkUs() const { return Search ? Search->WorkUs() : 0; }
    [[nodiscard]] uint64 SearchSetupUs() const { return Search ? Search->SetupUs() : 0; }
    [[nodiscard]] uint64 SearchNearestUs() const { return Search ? Search->NearestUs() : 0; }
    [[nodiscard]] uint64 SearchDirectUs() const { return Search ? Search->DirectUs() : 0; }
    [[nodiscard]] uint64 SearchAStarUs() const { return Search ? Search->AStarUs() : 0; }
    [[nodiscard]] uint64 SearchFinishUs() const { return Search ? Search->FinishUs() : 0; }

    Map* MapPtr = nullptr;
    uint32 Phase = 0;
    Position Start;
    Position Goal;
    bool Air = false;
    bool GoalRestricted = false;
    uint64 Key = 0;
    uint32 CreatedMs = 0;
    bool Done = false;
    bool Cancelled = false;
    bool Reverse = false;
    Movement::PointsArray Path;
    Nav3DResult Result;

private:
    enum class Stage
    {
        Tile,
        Lazy,
        ReverseLazy,
        AirDetail,
        AirTile,
        AirComposite,
        DetailRefine,
        Cruise,
        Done,
    };

    static bool Inside(Nav3DTile const& tile, Position const& position)
    {
        Nav3DTileHeader const& header = tile.Header();
        return position.GetPositionX() >= header.OriginX &&
               position.GetPositionX() <= header.OriginX + float(header.SizeX) * header.CellSize &&
               position.GetPositionY() >= header.OriginY &&
               position.GetPositionY() <= header.OriginY + float(header.SizeY) * header.CellSize &&
               position.GetPositionZ() >= header.OriginZ &&
               position.GetPositionZ() <= header.OriginZ + float(header.SizeZ) * header.CellSize;
    }

    std::shared_ptr<Nav3DTile> TileAt(uint8 medium) const
    {
        constexpr float gridSize = 533.3333f;
        uint16 const tileX =
            uint16(std::max<int32>(0, int32(std::floor(32.0f - Start.GetPositionX() / gridSize))));
        uint16 const tileY =
            uint16(std::max<int32>(0, int32(std::floor(32.0f - Start.GetPositionY() / gridSize))));
        return Nav3DManager::Instance().GetTile(MapPtr->GetId(), tileX, tileY, medium);
    }

    void BeginSearch(std::shared_ptr<Nav3DTile> tile, bool airComposite = false,
                     float overrideCellSize = 0.0f, Nav3DSearchBounds const* bounds = nullptr)
    {
        Position const& from = Reverse ? Goal : Start;
        Position const& to = Reverse ? Start : Goal;
        Search = std::make_unique<Nav3DSearch>(MapPtr, Phase, from, to, std::move(tile), airComposite,
                                               overrideCellSize, bounds, !GoalRestricted);
        bool const detail = CoarseNeeded || StageNow == Stage::DetailRefine ||
                            StageNow == Stage::AirDetail;
        uint32 const cap = detail ? sPlayerbotAIConfig.Nav3DCoopDetailSearchTimeMs
                                  : sPlayerbotAIConfig.Nav3DCoopSearchTimeMs;
        Search->SetTimeBudget(std::min<uint32>(sPlayerbotAIConfig.Nav3DMaxSearchTimeMs, cap));
    }

    void SetupTile()
    {
        std::shared_ptr<Nav3DTile> tile = TileAt(Nav3D::MediumWater);
        if (tile)
        {
            Nav3DTileHeader const& header = tile->Header();
            if ((header.Flags & Nav3D::FlagHasSolid) && header.GeneratorVersion >= 3 &&
                Inside(*tile, Start) && Inside(*tile, Goal))
            {
                DetailCell = header.CellSize;
                BeginSearch(tile);
                return;
            }
        }
        StageNow = Stage::Lazy;
    }

    void SetupAirDetail()
    {
        std::shared_ptr<Nav3DTile> tile = TileAt(Nav3D::MediumAirDetail);
        if (tile)
        {
            Nav3DTileHeader const& header = tile->Header();
            if ((header.Flags & Nav3D::FlagHasSolid) && header.GeneratorVersion >= 6 &&
                Inside(*tile, Start) && Inside(*tile, Goal))
            {
                DetailTile = tile;
                DetailCell = header.CellSize;
                // A 1-yd detail tile is too large for one A* pass: run the validated 4-yd
                // coarse search first, then refine each coarse leg at the native size.
                CoarseNeeded = header.CellSize < 4.0f;
                BeginSearch(tile, false, CoarseNeeded ? 4.0f : 0.0f);
                return;
            }
        }
        StageNow = Stage::AirTile;
    }

    void SetupAirTile()
    {
        std::shared_ptr<Nav3DTile> tile = TileAt(Nav3D::MediumAir);
        if (tile)
        {
            Nav3DTileHeader const& header = tile->Header();
            if ((header.Flags & Nav3D::FlagHasSolid) && header.GeneratorVersion >= 3 &&
                Inside(*tile, Start) && Inside(*tile, Goal))
            {
                AirTile = tile;
                BeginSearch(tile);
                return;
            }
        }
        StageNow = Stage::AirComposite;
    }

    void SetupRefineLeg()
    {
        // Refinement is bounded like the old synchronous pass: if it cannot finish, the
        // validated coarse route is still a usable answer.
        if (RefineIndex >= CoarsePath.size() || getMSTime() - RefineStartMs >= kRefineBudgetMs)
        {
            FinishWithCoarse();
            return;
        }

        constexpr float kRefinePad = 16.0f;
        G3D::Vector3 const& from = CoarsePath[RefineIndex - 1];
        G3D::Vector3 const& to = CoarsePath[RefineIndex];
        RefineBounds.MinX = std::min(from.x, to.x) - kRefinePad;
        RefineBounds.MaxX = std::max(from.x, to.x) + kRefinePad;
        RefineBounds.MinY = std::min(from.y, to.y) - kRefinePad;
        RefineBounds.MaxY = std::max(from.y, to.y) + kRefinePad;
        RefineBounds.MinZ = std::min(from.z, to.z) - kRefinePad;
        RefineBounds.MaxZ = std::max(from.z, to.z) + kRefinePad;

        BeginSearch(DetailTile, false, 0.0f, &RefineBounds);
    }

    static void AppendUnique(Movement::PointsArray& points, G3D::Vector3 const& point)
    {
        if (points.empty() || Distance(points.back(), point) > kDuplicateEpsilon)
            points.push_back(point);
    }

    void FinishWithCoarse()
    {
        Path = CoarsePath;
        Result = CoarseResult;
        Result.CellSize = 4.0f;
        Done = true;
        StageNow = Stage::Done;
    }

    void OnSearchDone()
    {
        bool const found = Search->Found();
        Movement::PointsArray searchPath = Search->Path();
        Nav3DResult const searchResult = Search->Result();
        Search.reset();

        if (!found)
        {
            switch (StageNow)
            {
                case Stage::Tile: StageNow = Stage::Lazy; break;
                case Stage::Lazy: StageNow = Stage::ReverseLazy; break;
                case Stage::ReverseLazy: StageNow = Stage::Done; break;
                case Stage::AirDetail: StageNow = Stage::AirTile; break;
                case Stage::AirTile: StageNow = Stage::AirComposite; break;
                case Stage::AirComposite: StageNow = Stage::Cruise; break;
                case Stage::DetailRefine: FinishWithCoarse(); break;
                case Stage::Cruise:
                case Stage::Done: Done = true; break;
            }
            return;
        }

        Path = std::move(searchPath);
        Result = searchResult;
        if (Reverse)
        {
            std::reverse(Path.begin(), Path.end());
            Result.Reversed = true;
        }

        if (StageNow == Stage::DetailRefine)
        {
            for (G3D::Vector3 const& point : Path)
                AppendUnique(RefinedPath, point);
            RefineMs += Result.ElapsedMs;
            RefineNodes += Result.Nodes;
            RefineExpanded += Result.Expanded;
            ++RefineIndex;
            if (RefineIndex >= CoarsePath.size())
            {
                if (RefinedPath.size() < 2)
                {
                    FinishWithCoarse();
                    return;
                }
                Path = RefinedPath;
                Result = {};
                Result.Found = true;
                Result.CellSize = DetailCell;
                Result.ElapsedMs = RefineMs;
                Result.Nodes = RefineNodes;
                Result.Expanded = RefineExpanded;
                Result.Length = 0.0f;
                for (size_t i = 1; i < Path.size(); ++i)
                    Result.Length += Distance(Path[i - 1], Path[i]);
                Done = true;
                StageNow = Stage::Done;
            }
            return;
        }

        if (StageNow == Stage::Tile)
            Result.CellSize = DetailCell;
        else if (StageNow == Stage::AirTile)
            Result.CellSize = AirTile ? AirTile->Header().CellSize : Result.CellSize;
        else if (StageNow == Stage::AirDetail && CoarseNeeded)
        {
            CoarsePath = Path;
            CoarseResult = Result;
            RefinedPath.clear();
            RefinedPath.push_back(CoarsePath.front());
            RefineIndex = 1;
            RefineStartMs = getMSTime();
            RefineMs = 0;
            RefineNodes = 0;
            RefineExpanded = 0;
            StageNow = Stage::DetailRefine;
            return;
        }
        else if (StageNow == Stage::AirDetail)
            Result.CellSize = DetailCell;

        Done = true;
        StageNow = Stage::Done;
    }

    void StepCruise()
    {
        if (CruiseAttempt == 0)
        {
            CruiseAltitude = std::max(Start.GetPositionZ(), Goal.GetPositionZ());
            float const dx = Goal.GetPositionX() - Start.GetPositionX();
            float const dy = Goal.GetPositionY() - Start.GetPositionY();
            float const horizontal = std::sqrt(dx * dx + dy * dy);
            int const samples = std::max(2, int(std::ceil(horizontal / 25.0f)));
            for (int i = 0; i <= samples; ++i)
            {
                float const t = float(i) / float(samples);
                float const x = Start.GetPositionX() + dx * t;
                float const y = Start.GetPositionY() + dy * t;
                float const ground = MapPtr->GetHeight(Phase, x, y, 4000.0f, true, 4000.0f);
                if (ground > -1000.0f)
                    CruiseAltitude = std::max(CruiseAltitude, ground + 80.0f);
            }
        }

        if (CruiseAttempt >= 4)
        {
            Done = true;
            StageNow = Stage::Done;
            return;
        }

        float const cruise = CruiseAltitude + float(CruiseAttempt) * 150.0f;
        G3D::Vector3 const a{Start.GetPositionX(), Start.GetPositionY(), Start.GetPositionZ()};
        G3D::Vector3 const b{Start.GetPositionX(), Start.GetPositionY(), cruise};
        G3D::Vector3 const c{Goal.GetPositionX(), Goal.GetPositionY(), cruise};
        G3D::Vector3 const d{Goal.GetPositionX(), Goal.GetPositionY(), Goal.GetPositionZ()};
        ++CruiseAttempt;

        if (!LineClear(MapPtr, Phase, a, b) || !LineClear(MapPtr, Phase, b, c) ||
            !LineClear(MapPtr, Phase, c, d))
            return;

        Path = {a, b, c, d};
        RemoveCollinear(Path);
        Result = {};
        Result.Found = true;
        Result.Direct = true;
        Result.Length = 0.0f;
        for (size_t i = 1; i < Path.size(); ++i)
            Result.Length += Distance(Path[i - 1], Path[i]);
        Done = true;
        StageNow = Stage::Done;
    }

    static constexpr uint32 kRefineBudgetMs = 5000;

    Stage StageNow = Air ? Stage::AirDetail : Stage::Tile;
    std::unique_ptr<Nav3DSearch> Search;

    std::shared_ptr<Nav3DTile> DetailTile;
    float DetailCell = 0.0f;
    bool CoarseNeeded = false;
    Movement::PointsArray CoarsePath;
    Movement::PointsArray RefinedPath;
    Nav3DResult CoarseResult;
    Nav3DSearchBounds RefineBounds;
    size_t RefineIndex = 0;
    uint32 RefineStartMs = 0;
    uint32 RefineMs = 0;
    uint32 RefineNodes = 0;
    uint32 RefineExpanded = 0;

    std::shared_ptr<Nav3DTile> AirTile;
    float CruiseAltitude = 0.0f;
    uint32 CruiseAttempt = 0;
};

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

bool Nav3DManager::RefineDetailPath(Map* map, uint32 phase, Nav3DTile const* tile,
                                    Movement::PointsArray const& coarse, Movement::PointsArray& path,
                                    Nav3DResult& result)
{
    result = {};
    path.clear();
    if (!map || !tile || coarse.size() < 2)
        return false;

    constexpr float kRefinePad = 16.0f;
    constexpr uint32 kRefineBudgetMs = 5000;
    uint32 const startedMs = getMSTime();
    uint32 totalMs = 0;
    uint32 totalNodes = 0;
    uint32 totalExpanded = 0;

    auto const appendUnique = [&path](G3D::Vector3 const& point)
    {
        if (path.empty() || Distance(path.back(), point) > kDuplicateEpsilon)
            path.push_back(point);
    };

    for (size_t i = 1; i < coarse.size(); ++i)
    {
        if (getMSTime() - startedMs >= kRefineBudgetMs)
            return false;

        G3D::Vector3 const& from = coarse[i - 1];
        G3D::Vector3 const& to = coarse[i];
        Nav3DSearchBounds bounds;
        bounds.MinX = std::min(from.x, to.x) - kRefinePad;
        bounds.MaxX = std::max(from.x, to.x) + kRefinePad;
        bounds.MinY = std::min(from.y, to.y) - kRefinePad;
        bounds.MaxY = std::max(from.y, to.y) + kRefinePad;
        bounds.MinZ = std::min(from.z, to.z) - kRefinePad;
        bounds.MaxZ = std::max(from.z, to.z) + kRefinePad;

        Nav3DResult legResult;
        Movement::PointsArray legPath;
        if (!FindPathDirected(map, phase, Position(from.x, from.y, from.z),
                              Position(to.x, to.y, to.z), legPath, legResult, tile, false, 0.0f,
                              &bounds))
            return false;

        for (G3D::Vector3 const& point : legPath)
            appendUnique(point);
        totalMs += legResult.ElapsedMs;
        totalNodes += legResult.Nodes;
        totalExpanded += legResult.Expanded;
    }

    if (path.size() < 2)
        return false;

    result.Found = true;
    result.CellSize = tile->Header().CellSize;
    result.ElapsedMs = totalMs;
    result.Nodes = totalNodes;
    result.Expanded = totalExpanded;
    result.Length = 0.0f;
    for (size_t i = 1; i < path.size(); ++i)
        result.Length += Distance(path[i - 1], path[i]);
    return true;
}

bool Nav3DManager::FindPathAir(Map* map, uint32 phase, Position const& start, Position const& goal,
                               Movement::PointsArray& path, Nav3DResult& result)
{
    if (!map || !sPlayerbotAIConfig.Nav3DEnabled)
        return false;

    bool const goalRestricted = IsRestrictedFlightAt(map, phase, goal);
    bool const penalizeRestricted = !goalRestricted;

    constexpr float gridSize = 533.3333f;
    uint16 const tileX =
        uint16(std::max<int32>(0, int32(std::floor(32.0f - start.GetPositionX() / gridSize))));
    uint16 const tileY =
        uint16(std::max<int32>(0, int32(std::floor(32.0f - start.GetPositionY() / gridSize))));

    auto const insideTile = [](Nav3DTile const& tile, Position const& position)
    {
        Nav3DTileHeader const& header = tile.Header();
        return position.GetPositionX() >= header.OriginX &&
               position.GetPositionX() <= header.OriginX + float(header.SizeX) * header.CellSize &&
               position.GetPositionY() >= header.OriginY &&
               position.GetPositionY() <= header.OriginY + float(header.SizeY) * header.CellSize &&
               position.GetPositionZ() >= header.OriginZ &&
               position.GetPositionZ() <= header.OriginZ + float(header.SizeZ) * header.CellSize;
    };

    auto const tileOf = [gridSize](Position const& position)
    {
        int32 const tx = std::clamp(int32(std::floor(32.0f - position.GetPositionX() / gridSize)), 0, 63);
        int32 const ty = std::clamp(int32(std::floor(32.0f - position.GetPositionY() / gridSize)), 0, 63);
        return std::make_pair(tx, ty);
    };

    // A local detail tile may extend across ADT tile boundaries (the Dalaran core does
    // this for the sewer/Krasus route). Try both endpoint tiles and their neighbours and
    // prefer the finest cell size that contains both endpoints.
    std::vector<std::pair<int32, int32>> candidates;
    auto const addCandidate = [&candidates](int32 tx, int32 ty)
    {
        if (tx < 0 || ty < 0 || tx >= 64 || ty >= 64)
            return;
        std::pair<int32, int32> const key{tx, ty};
        if (std::find(candidates.begin(), candidates.end(), key) == candidates.end())
            candidates.push_back(key);
    };
    auto const startTile = tileOf(start);
    auto const goalTile = tileOf(goal);
    addCandidate(startTile.first, startTile.second);
    addCandidate(goalTile.first, goalTile.second);
    for (int32 dx = -1; dx <= 1; ++dx)
    {
        for (int32 dy = -1; dy <= 1; ++dy)
        {
            addCandidate(startTile.first + dx, startTile.second + dy);
            addCandidate(goalTile.first + dx, goalTile.second + dy);
        }
    }

    std::shared_ptr<Nav3DTile> detail;
    for (auto const& [tx, ty] : candidates)
    {
        std::shared_ptr<Nav3DTile> tile = GetTile(map->GetId(), uint16(tx), uint16(ty),
                                                 Nav3D::MediumAirDetail);
        if (!tile)
            continue;
        Nav3DTileHeader const& header = tile->Header();
        if (!(header.Flags & Nav3D::FlagHasSolid) || header.GeneratorVersion < 6)
            continue;
        if (!insideTile(*tile, start) || !insideTile(*tile, goal))
            continue;
        if (!detail || header.CellSize < detail->Header().CellSize)
            detail = tile;
    }

    if (detail)
    {
        Nav3DTileHeader const& header = detail->Header();
        {
                // Fine detail tiles (1-yd, 2-yd) are too large for a full cell A* within the
                // runtime budget. Run a 4-yd pass over the same fine tile first (the validated
                // Uldis route), then refine each coarse leg at the native cell size in a local
                // box so the final path stays inside the tunnel.
                float const coarseCell = std::max(4.0f, header.CellSize);
                bool const needsCoarse = header.CellSize < coarseCell;
                Nav3DResult detailResult;
                Movement::PointsArray detailPath;
                bool detailFound = false;
                if (needsCoarse)
                {
                    detailFound = FindPathDirected(map, phase, start, goal, detailPath, detailResult,
                                                   detail.get(), false, coarseCell, nullptr,
                                                   penalizeRestricted);
                    LOG_DEBUG("playerbots",
                              "nav3d detail coarse map {} found={} points={} ms={} nodes={} expanded={} best={:.1f}",
                              map->GetId(), detailFound ? 1 : 0, detailPath.size(),
                              detailResult.ElapsedMs, detailResult.Nodes, detailResult.Expanded,
                              detailResult.BestDistance);
                    if (detailFound)
                    {
                        Movement::PointsArray refined;
                        Nav3DResult refineResult;
                        if (RefineDetailPath(map, phase, detail.get(), detailPath, refined, refineResult))
                        {
                            detailPath = std::move(refined);
                            detailResult = refineResult;
                            detailResult.CellSize = header.CellSize;
                            LOG_DEBUG("playerbots",
                                      "nav3d detail refined map {} points={} len={:.1f} ms={} nodes={} expanded={}",
                                      map->GetId(), detailPath.size(), detailResult.Length,
                                      detailResult.ElapsedMs, detailResult.Nodes, detailResult.Expanded);
                        }
                        else
                        {
                            detailResult.CellSize = coarseCell;
                            LOG_DEBUG("playerbots",
                                      "nav3d detail refine failed map {}; using coarse path",
                                      map->GetId());
                        }
                    }
                }
                else
                {
                    detailFound = FindPathDirected(map, phase, start, goal, detailPath, detailResult,
                                                   detail.get(), false, 0.0f, nullptr,
                                                   penalizeRestricted);
                }
                LOG_DEBUG("playerbots",
                          "nav3d detail map {} start=({:.1f},{:.1f},{:.1f}) goal=({:.1f},{:.1f},{:.1f}) "
                          "found={} points={} ms={} nodes={} expanded={} best={:.1f}",
                          map->GetId(), start.GetPositionX(), start.GetPositionY(), start.GetPositionZ(),
                          goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ(),
                          detailFound ? 1 : 0, detailPath.size(), detailResult.ElapsedMs,
                          detailResult.Nodes, detailResult.Expanded, detailResult.BestDistance);
                LOG_DEBUG("playerbots", "nav3d detail best=({:.1f},{:.1f},{:.1f}) goal=({:.1f},{:.1f},{:.1f})",
                          detailResult.BestX, detailResult.BestY, detailResult.BestZ,
                          goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ());
                if (detailFound)
                {
                    path = std::move(detailPath);
                    result = detailResult;
                    result.CellSize = detailResult.CellSize > 0.0f ? detailResult.CellSize
                                                                   : header.CellSize;
                    return true;
                }
            }
        }

    std::shared_ptr<Nav3DTile> tile = GetTile(map->GetId(), tileX, tileY, Nav3D::MediumAir);

    if (tile)
    {
        Nav3DTileHeader const& header = tile->Header();
        if ((header.Flags & Nav3D::FlagHasSolid) && header.GeneratorVersion >= 3)
        {
            float const maxX = header.OriginX + float(header.SizeX) * header.CellSize;
            float const maxY = header.OriginY + float(header.SizeY) * header.CellSize;
            float const maxZ = header.OriginZ + float(header.SizeZ) * header.CellSize;
            auto const inside = [&header, maxX, maxY, maxZ](Position const& position)
            {
                return position.GetPositionX() >= header.OriginX && position.GetPositionX() <= maxX &&
                       position.GetPositionY() >= header.OriginY && position.GetPositionY() <= maxY &&
                       position.GetPositionZ() >= header.OriginZ && position.GetPositionZ() <= maxZ;
            };
            if (inside(start) && inside(goal))
            {
                Nav3DResult tileResult;
                if (FindPathDirected(map, phase, start, goal, path, tileResult, tile.get(), false,
                                     0.0f, nullptr, penalizeRestricted))
                {
                    result = tileResult;
                    result.CellSize = header.CellSize;
                    return true;
                }
            }
        }
    }

    // Cross-tile volumetric route: one A* over a composite grid spanning the ADT air tiles.
    // Each node samples the aligned air tile at its world position, so WMO floors/ceilings and
    // terrain stay in the search across tile boundaries.
    Nav3DResult airResult;
    if (FindPathDirected(map, phase, start, goal, path, airResult, nullptr, true, 0.0f, nullptr,
                         penalizeRestricted))
    {
        result = airResult;
        result.CellSize = kAirCompositeCell;
        return true;
    }

    // Fallback: climb to a cruise altitude, cross above terrain/WMO, then descend. The
    // cruise altitude is sampled just above the highest ground/WMO on the route.
    float cruise = std::max(start.GetPositionZ(), goal.GetPositionZ());
    float const dx = goal.GetPositionX() - start.GetPositionX();
    float const dy = goal.GetPositionY() - start.GetPositionY();
    float const horizontal = std::sqrt(dx * dx + dy * dy);
    int const samples = std::max(2, int(std::ceil(horizontal / 25.0f)));
    for (int i = 0; i <= samples; ++i)
    {
        float const t = float(i) / float(samples);
        float const x = start.GetPositionX() + dx * t;
        float const y = start.GetPositionY() + dy * t;
        float const ground = map->GetHeight(phase, x, y, 4000.0f, true, 4000.0f);
        if (ground > -1000.0f)
            cruise = std::max(cruise, ground + 80.0f);
    }
    for (int attempt = 0; attempt < 4; ++attempt, cruise += 150.0f)
    {
        G3D::Vector3 const a{start.GetPositionX(), start.GetPositionY(), start.GetPositionZ()};
        G3D::Vector3 const b{start.GetPositionX(), start.GetPositionY(), cruise};
        G3D::Vector3 const c{goal.GetPositionX(), goal.GetPositionY(), cruise};
        G3D::Vector3 const d{goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ()};
        if (!LineClear(map, phase, a, b) || !LineClear(map, phase, b, c) ||
            !LineClear(map, phase, c, d))
            continue;

        path = {a, b, c, d};
        RemoveCollinear(path);
        result = {};
        result.Found = true;
        result.Direct = true;
        result.Length = 0.0f;
        for (size_t i = 1; i < path.size(); ++i)
            result.Length += Distance(path[i - 1], path[i]);
        return true;
    }

    return false;
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


namespace
{
    uint64 PathKey(Map* map, Position const& position, bool air)
    {
        constexpr float gridSize = 533.3333f;
        int32 const tileX = std::clamp(
            int32(std::floor(32.0f - position.GetPositionX() / gridSize)), 0, 63);
        int32 const tileY = std::clamp(
            int32(std::floor(32.0f - position.GetPositionY() / gridSize)), 0, 63);
        return (uint64(map->GetId()) << 32) | (uint64(uint32(tileX)) << 8) |
               (uint64(uint32(tileY)) << 1) | (air ? 1u : 0u);
    }
}

void Nav3DManager::RecordNegativeLocked(uint64 key, uint32 now)
{
    if (_coopNegative.size() > 4096)
    {
        for (auto it = _coopNegative.begin(); it != _coopNegative.end();)
        {
            if (it->second <= now)
                it = _coopNegative.erase(it);
            else
                ++it;
        }
    }
    _coopNegative[key] = now + sPlayerbotAIConfig.Nav3DNegativeTtlMs;
}

Nav3DPathHandle Nav3DManager::RequestPath(Map* map, uint32 phase, Position const& start,
                                          Position const& goal, bool air, uint8 priority)
{
    if (!map || !sPlayerbotAIConfig.Nav3DEnabled || !sPlayerbotAIConfig.Nav3DCooperative)
        return 0;

    uint64 const key = PathKey(map, start, air);
    QueuePrefetch(map, start, goal, air);
    std::scoped_lock lock(_pathJobsMutex);
    if (_pathJobs.size() >= sPlayerbotAIConfig.Nav3DPathMaxPending)
        return 0;

    uint32 const now = getMSTime();
    auto const negative = _coopNegative.find(key);
    if (negative != _coopNegative.end())
    {
        if (negative->second > now)
            return 0;
        _coopNegative.erase(negative);
    }

    PathScheduler& scheduler = _pathSchedulers[map];
    if (scheduler.Queue.size() >= uint32(sPlayerbotAIConfig.Nav3DPathMaxPerMap))
        return 0;

    auto job = std::make_shared<Nav3DPathJob>(map, phase, start, goal, air, key);
    uint64 const handle = _nextPathHandle++;
    _pathJobs.emplace(handle, std::move(job));

    PathQueueEntry entry{handle, priority, now};
    if (priority >= 3)
    {
        // Combat/forced paths go before travel/loot, but keep FIFO within the high tier.
        auto const it = std::find_if(scheduler.Queue.begin(), scheduler.Queue.end(),
                                     [](PathQueueEntry const& queued) { return queued.Priority < 3; });
        scheduler.Queue.insert(it, entry);
    }
    else
        scheduler.Queue.push_back(entry);

    ++_coopRequested;
    return handle;
}

uint64 Nav3DManager::ClaimGlobalPathBudget(uint32 now)
{
    std::scoped_lock lock(_globalPathMutex);
    if (!_globalPathLastMs)
        _globalPathLastMs = now;
    if (now > _globalPathLastMs)
    {
        uint64 const cap = uint64(sPlayerbotAIConfig.Nav3DPathGlobalBudgetMs) * 1000;
        uint64 const refill = uint64(now - _globalPathLastMs) * cap / 1000;
        _globalPathBudgetUs = std::min<uint64>(cap, _globalPathBudgetUs + refill);
        _globalPathLastMs = now;
    }
    return _globalPathBudgetUs;
}

void Nav3DManager::ConsumeGlobalPathBudget(uint64 usedUs)
{
    std::scoped_lock lock(_globalPathMutex);
    _globalPathBudgetUs -= std::min(_globalPathBudgetUs, usedUs);
}

void Nav3DManager::PumpPaths(Map* map, PathScheduler& scheduler, uint32 now)
{
    // Only the map thread that owns this scheduler touches its queue and budget, so the
    // per-map state needs no lock. The global token bucket is shared, but is held only for
    // the accounting calls, never while a search step runs.
    if (!scheduler.LastMs)
        scheduler.LastMs = now;
    if (now > scheduler.LastMs)
    {
        uint64 const cap = uint64(sPlayerbotAIConfig.Nav3DPathBudgetMs) * 1000;
        uint64 const refill = uint64(now - scheduler.LastMs) * cap / 50;
        scheduler.BudgetUs = std::min<uint64>(cap, scheduler.BudgetUs + refill);
        scheduler.LastMs = now;
    }

    // Anti-starvation: promote one low-priority entry that has waited too long.
    if (scheduler.Queue.size() > 1)
    {
        auto const boost = std::find_if(
            scheduler.Queue.begin(), scheduler.Queue.end(),
            [now](PathQueueEntry const& entry)
            {
                return entry.Priority < 3 &&
                       now - entry.CreatedMs >= uint32(sPlayerbotAIConfig.Nav3DPathAgeBoostMs);
            });
        if (boost != scheduler.Queue.end() && boost != scheduler.Queue.begin())
        {
            PathQueueEntry const promoted = *boost;
            scheduler.Queue.erase(boost);
            scheduler.Queue.push_front(promoted);
        }
    }

    uint64 const slice = std::max<uint64>(50, sPlayerbotAIConfig.Nav3DPathSliceUs);
    uint32 iterations = 0;
    while (!scheduler.Queue.empty() && iterations++ < 16)
    {
        uint64 const handle = scheduler.Queue.front().Handle;
        std::shared_ptr<Nav3DPathJob> job;
        {
            std::scoped_lock lock(_pathJobsMutex);
            auto const it = _pathJobs.find(handle);
            if (it == _pathJobs.end())
            {
                scheduler.Queue.pop_front();
                continue;
            }
            job = it->second;
        }

        if (now - job->CreatedMs > sPlayerbotAIConfig.Nav3DPathMaxAgeMs)
        {
            std::scoped_lock lock(_pathJobsMutex);
            _pathJobs.erase(handle);
            ++_coopExpired;
            scheduler.Queue.pop_front();
            continue;
        }
        if (job->IsDone())
        {
            // The owner has not collected the result yet; leave it in the job map.
            scheduler.Queue.pop_front();
            continue;
        }

        uint64 const budget = std::min(scheduler.BudgetUs, ClaimGlobalPathBudget(now));
        if (budget < 50)
            break;

        uint32 const stepUs = uint32(std::min<uint64>(budget, slice));
        char const* const stage = job->StageName();
        uint32 const expanded = job->SearchExpanded();
        auto const started = std::chrono::steady_clock::now();
        bool const finished = job->Step(stepUs);
        uint64 const used = uint64(std::max<int64>(0, std::chrono::duration_cast<std::chrono::microseconds>(
                                                          std::chrono::steady_clock::now() - started)
                                                          .count()));
        if (used > 10000)
        {
            LOG_DEBUG("playerbots",
                      "nav3d slow-step map={} stage={} expanded={} step_us={} used_us={} finished={} "
                      "queue={} work_us={} setup_us={} nearest_us={} direct_us={} astar_us={} finish_us={}",
                      map->GetId(), stage, expanded, stepUs, used, finished ? 1 : 0,
                      scheduler.Queue.size(), job->SearchWorkUs(), job->SearchSetupUs(),
                      job->SearchNearestUs(), job->SearchDirectUs(), job->SearchAStarUs(),
                      job->SearchFinishUs());
        }
        scheduler.BudgetUs -= std::min(scheduler.BudgetUs, used);
        ConsumeGlobalPathBudget(used);
        _coopStepUs.fetch_add(used, std::memory_order_relaxed);
        uint64 previous = _coopStepMaxUs.load(std::memory_order_relaxed);
        while (used > previous &&
               !_coopStepMaxUs.compare_exchange_weak(previous, used, std::memory_order_relaxed))
        {
        }

        if (finished)
        {
            if (!job->Result.Found)
            {
                std::scoped_lock lock(_pathJobsMutex);
                RecordNegativeLocked(job->Key, now);
            }
            if (job->Cancelled)
            {
                std::scoped_lock lock(_pathJobsMutex);
                _pathJobs.erase(handle);
            }
            scheduler.Queue.pop_front();
        }
        else if (used >= budget || used < 20)
            break;
    }
}

Nav3DPathPoll Nav3DManager::PollPath(Nav3DPathHandle handle, Map* map, Movement::PointsArray& path,
                                     Nav3DResult& result)
{
    if (!handle || !map)
        return Nav3DPathPoll::Failed;

    PathScheduler* scheduler = nullptr;
    {
        std::scoped_lock lock(_pathJobsMutex);
        auto const it = _pathJobs.find(handle);
        if (it == _pathJobs.end())
            return Nav3DPathPoll::Failed;

        std::shared_ptr<Nav3DPathJob> const& job = it->second;
        uint32 const now = getMSTime();
        if (job->Cancelled)
        {
            _pathJobs.erase(it);
            return Nav3DPathPoll::Failed;
        }
        if (job->MapPtr != map || now - job->CreatedMs > sPlayerbotAIConfig.Nav3DPathMaxAgeMs)
        {
            _pathJobs.erase(it);
            ++_coopExpired;
            return Nav3DPathPoll::Failed;
        }
        scheduler = &_pathSchedulers[map];
    }

    PumpPaths(map, *scheduler, getMSTime());

    std::scoped_lock lock(_pathJobsMutex);
    auto const it = _pathJobs.find(handle);
    if (it == _pathJobs.end())
        return Nav3DPathPoll::Failed;
    if (!it->second->IsDone())
        return Nav3DPathPoll::Pending;

    std::shared_ptr<Nav3DPathJob> const done = it->second;
    path = done->Path;
    result = done->Result;
    _pathJobs.erase(it);
    if (done->Result.Found)
        ++_coopFound;
    else
    {
        ++_coopFailed;
        RecordNegativeLocked(done->Key, getMSTime());
    }

    uint32 const now = getMSTime();
    if (!_coopLastLogMs)
        _coopLastLogMs = now;
    if (now - _coopLastLogMs >= 10000)
    {
        LOG_DEBUG("playerbots",
                 "nav3d coop: req={} pending={} found={} failed={} expired={} step_ms={} max_step_ms={}",
                 _coopRequested, _pathJobs.size(), _coopFound, _coopFailed, _coopExpired,
                 _coopStepUs.exchange(0, std::memory_order_relaxed) / 1000,
                 _coopStepMaxUs.exchange(0, std::memory_order_relaxed) / 1000);
        if (auto const mapIt = _pathSchedulers.find(map); mapIt != _pathSchedulers.end())
            LOG_DEBUG("playerbots", "nav3d sched map={} queue={} budget_us={}",
                      map->GetId(), mapIt->second.Queue.size(), mapIt->second.BudgetUs);
        _coopLastLogMs = now;
    }
    return done->Result.Found ? Nav3DPathPoll::Found : Nav3DPathPoll::Failed;
}

void Nav3DManager::CancelPath(Nav3DPathHandle handle)
{
    if (!handle)
        return;
    std::scoped_lock lock(_pathJobsMutex);
    auto const it = _pathJobs.find(handle);
    if (it != _pathJobs.end())
        it->second->Cancelled = true;
}

bool Nav3DManager::FindPathDirected(Map* map, uint32 phase, Position const& start, Position const& goal,
                                    Movement::PointsArray& path, Nav3DResult& result,
                                    Nav3DTile const* tile, bool airComposite,
                                    float overrideCellSize, Nav3DSearchBounds const* bounds,
                                    bool penalizeRestricted)
{
    std::shared_ptr<Nav3DTile> tileRef;
    if (tile)
        tileRef = std::shared_ptr<Nav3DTile>(const_cast<Nav3DTile*>(tile), [](Nav3DTile*) {});
    Nav3DSearch search(map, phase, start, goal, tileRef, airComposite, overrideCellSize, bounds,
                       penalizeRestricted);
    while (!search.Step(std::numeric_limits<uint64>::max()))
    {
    }
    path = search.Path();
    result = search.Result();
    return search.Found();
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
    // Emergency ascent: this path is issued synchronously from the dive action, so keep the
    // one-shot budget short. The bot re-samples the surface column on the next attempt.
    uint32 const maxTimeMs = std::max<uint32>(
        25, std::min<uint32>(250, sPlayerbotAIConfig.Nav3DMaxSearchTimeMs));

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
    open.push({0.0f, 0.0f, startNode});

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
            open.push({tentative, tentative, neighbor});
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

Nav3DManager::~Nav3DManager()
{
    {
        std::scoped_lock lock(_prefetchMutex);
        _prefetchStop = true;
    }
    _prefetchCv.notify_all();
    if (_prefetchThread.joinable())
        _prefetchThread.join();
}

void Nav3DManager::QueuePrefetch(Map* map, Position const& start, Position const& goal, bool air)
{
    if (!map)
        return;

    constexpr float gridSize = 533.3333f;
    auto const tileOf = [](Position const& position)
    {
        int32 const tx = std::clamp(int32(std::floor(32.0f - position.GetPositionX() / gridSize)), 0, 63);
        int32 const ty = std::clamp(int32(std::floor(32.0f - position.GetPositionY() / gridSize)), 0, 63);
        return std::make_pair(tx, ty);
    };

    std::vector<PrefetchRequest> requests;
    auto const add = [&](Position const& position, uint8 medium)
    {
        auto const [tx, ty] = tileOf(position);
        for (int32 dx = -1; dx <= 1; ++dx)
        {
            for (int32 dy = -1; dy <= 1; ++dy)
            {
                int32 const x = tx + dx;
                int32 const y = ty + dy;
                if (x < 0 || y < 0 || x >= 64 || y >= 64)
                    continue;
                requests.push_back({map->GetId(), uint16(x), uint16(y), medium});
            }
        }
    };

    if (air)
    {
        add(start, Nav3D::MediumAir);
        add(goal, Nav3D::MediumAir);
        add(start, Nav3D::MediumAirDetail);
        add(goal, Nav3D::MediumAirDetail);
    }
    else
    {
        add(start, Nav3D::MediumWater);
        add(goal, Nav3D::MediumWater);
    }

    std::scoped_lock lock(_prefetchMutex);
    if (!_prefetchStarted)
    {
        _prefetchStarted = true;
        _prefetchThread = std::thread([this]() { PrefetchLoop(); });
    }

    for (PrefetchRequest const& request : requests)
    {
        uint64 const key = (uint64(request.MapId) << 32) | (uint64(request.TileX) << 16) |
                           (uint64(request.TileY) << 8) | request.Medium;
        if (_prefetchQueue.size() >= 4096)
            break;
        if (_prefetchQueued.insert(key).second)
            _prefetchQueue.push_back(request);
    }
    _prefetchCv.notify_one();
}

void Nav3DManager::PrefetchLoop()
{
    while (true)
    {
        PrefetchRequest request;
        {
            std::unique_lock lock(_prefetchMutex);
            _prefetchCv.wait(lock, [this]() { return _prefetchStop || !_prefetchQueue.empty(); });
            if (_prefetchStop)
                return;

            request = _prefetchQueue.front();
            _prefetchQueue.pop_front();
            uint64 const key = (uint64(request.MapId) << 32) | (uint64(request.TileX) << 16) |
                               (uint64(request.TileY) << 8) | request.Medium;
            _prefetchQueued.erase(key);
        }

        // File I/O and decompression only; no Map/vmap access.
        GetTile(request.MapId, request.TileX, request.TileY, request.Medium);
    }
}

std::shared_ptr<Nav3DTile> Nav3DManager::GetTile(uint32 mapId, uint16 tileX, uint16 tileY, uint8 medium)
{
    uint64 const key = (uint64(mapId) << 32) | (uint64(tileX) << 16) | (uint64(tileY) << 8) | medium;

    {
        std::scoped_lock lock(_tileCacheMutex);
        auto const cached = _tileCache.find(key);
        if (cached != _tileCache.end())
        {
            _tileLru.splice(_tileLru.begin(), _tileLru, cached->second);
            return cached->second->Tile;
        }
    }

    std::string dataPath = sWorld ? sWorld->GetDataPath() : std::string();
    if (!dataPath.empty() && dataPath.back() != '/')
        dataPath += '/';
    dataPath += sPlayerbotAIConfig.Nav3DDataPath;
    if (!dataPath.empty() && dataPath.back() != '/')
        dataPath += '/';

    std::string mediumName = "air";
    if (medium == Nav3D::MediumWater)
        mediumName = "water";
    else if (medium == Nav3D::MediumAirDetail)
        mediumName = "airdetail";
    std::string const path = Acore::StringFormat("{}map{:04}/{:02}_{:02}_{}.n3t", dataPath, mapId,
                                                 tileX, tileY, mediumName);

    auto tile = std::make_shared<Nav3DTile>();
    if (!tile->Load(path))
    {
        LOG_DEBUG("playerbots", "nav3d tile missing or invalid: {}", path);
        return nullptr;
    }

    // Another map thread may have loaded the same tile while this one was reading it.
    std::scoped_lock lock(_tileCacheMutex);
    auto const cached = _tileCache.find(key);
    if (cached != _tileCache.end())
    {
        _tileLru.splice(_tileLru.begin(), _tileLru, cached->second);
        return cached->second->Tile;
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

bool Nav3DManager::CanUseFlight(Player const* bot)
{
    if (!bot)
        return false;
    return CanUseFlightAt(bot, Position(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()));
}

bool Nav3DManager::IsRestrictedFlightAt(Map* map, uint32 phase, Position const& position)
{
    if (!sPlayerbotAIConfig.Nav3DRespectNoFlyZones || !map)
        return false;

    uint32 const areaId = map->GetAreaId(phase, position.GetPositionX(), position.GetPositionY(),
                                         position.GetPositionZ());
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
    if (area && (area->flags & AREA_FLAG_NO_FLY_ZONE))
        return true;

    // Wintergrasp is flyable between battles. Spell 58730 applies the restricted
    // flight aura only while the battlefield is active.
    uint32 const zoneId = map->GetZoneId(phase, position.GetPositionX(), position.GetPositionY(),
                                         position.GetPositionZ());
    if (Battlefield* battlefield = sBattlefieldMgr->GetBattlefieldToZoneId(zoneId))
        if (!battlefield->CanFlyIn())
            return true;

    return false;
}

bool Nav3DManager::CanLandAt(Player const* bot, Position const& goal, bool& restricted) const
{
    restricted = false;
    if (!bot || !bot->GetMap())
        return false;

    restricted = IsRestrictedFlightAt(bot->GetMap(), bot->GetPhaseMask(), goal);
    if (!restricted)
        return CanUseFlightAt(bot, goal);

    // One-way entry: allow a goal inside a restricted area only if it is a real
    // ground landing point. The parachute handles a timer expiry on the way down.
    PositionFullTerrainStatus status;
    bot->GetMap()->GetFullTerrainStatusForPosition(bot->GetPhaseMask(), goal.GetPositionX(),
                                                   goal.GetPositionY(), goal.GetPositionZ(), 1.0f,
                                                   status, {});
    if (status.floorZ <= INVALID_HEIGHT)
        return false;

    float const clearance = goal.GetPositionZ() - status.floorZ;
    return clearance >= -1.0f && clearance <= 5.0f;
}

uint32 Nav3DManager::RestrictedFlightMs(Map* map, uint32 phase, Movement::PointsArray const& path,
                                        float speed) const
{
    if (!map || path.size() < 2 || !sPlayerbotAIConfig.Nav3DRespectNoFlyZones)
        return 0;

    constexpr float kSampleStep = 4.0f;
    float const safeSpeed = std::max(7.0f, speed);
    uint32 totalMs = 0;
    for (size_t i = 1; i < path.size(); ++i)
    {
        G3D::Vector3 const& from = path[i - 1];
        G3D::Vector3 const& to = path[i];
        float const dx = to.x - from.x;
        float const dy = to.y - from.y;
        float const dz = to.z - from.z;
        float const distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        int const steps = std::max(1, int(std::ceil(distance / kSampleStep)));
        float const stepDistance = distance / float(steps);
        for (int step = 0; step < steps; ++step)
        {
            float const t = (float(step) + 0.5f) / float(steps);
            G3D::Vector3 const point = from + (to - from) * t;
            if (IsRestrictedFlightAt(map, phase, Position(point.x, point.y, point.z)))
                totalMs += uint32(stepDistance / safeSpeed * 1000.0f + 0.5f);
        }
    }
    return totalMs;
}

bool Nav3DManager::CanUseFlightAt(Player const* bot, Position const& position)
{
    if (!bot || !bot->GetMap())
        return false;
    if (bot->GetPureSkillValue(SKILL_RIDING) < 225)
        return false;

    uint32 const areaId = bot->GetMap()->GetAreaId(bot->GetPhaseMask(), position.GetPositionX(),
                                                   position.GetPositionY(), position.GetPositionZ());
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
    if (!area || !area->IsFlyable())
        return false;
    if (IsRestrictedFlightAt(bot->GetMap(), bot->GetPhaseMask(), position))
        return false;

    uint32 const vmap = GetVirtualMapForMapAndZone(bot->GetMapId(), bot->GetZoneId());
    // Player free flight is an Outland/Northrend feature. Eastern Kingdoms and Kalimdor
    // (maps 0/1) are taxi-only even if a sub-area carries the expansion flag.
    if (vmap != MAP_OUTLAND && vmap != MAP_NORTHREND)
        return false;
    if (vmap == MAP_NORTHREND && !bot->HasSpell(54197))
        return false;

    return true;
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
    {
        std::scoped_lock lock(_prefetchMutex);
        _prefetchQueue.clear();
        _prefetchQueued.clear();
    }
    std::scoped_lock lock(_tileCacheMutex);
    _tileCache.clear();
    _tileLru.clear();
}

namespace
{
    bool IsLiveAirCell(Map* map, uint32 phase, float x, float y, float z)
    {
        PositionFullTerrainStatus status;
        map->GetFullTerrainStatusForPosition(phase, x, y, z, 1.0f, status, {});
        if (status.floorZ > INVALID_HEIGHT && status.floorZ > z + 0.5f)
            return false;

        // Below the outdoor terrain the only valid air is a WMO cave volume. A point
        // in rock above a cave has the same WMO floor below it, but the shaft down to
        // that floor hits the cave ceiling. Require both the WMO area and a clear
        // vertical shaft; otherwise the live bake turns the mountain into air.
        float const terrainZ = map->GetHeight(phase, x, y, z, false);
        if (terrainZ > INVALID_HEIGHT && z < terrainZ - 1.0f)
        {
            uint32 flags = 0;
            int32 adtId = -1;
            int32 rootId = -1;
            int32 groupId = -1;
            if (!map->GetAreaInfo(phase, x, y, z, flags, adtId, rootId, groupId))
                return false;
            if (status.floorZ <= INVALID_HEIGHT || status.floorZ >= z - 0.5f)
                return false;
            if (!map->isInLineOfSight(x, y, z, x, y, status.floorZ + 0.5f, phase,
                                      LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
                return false;
        }

        constexpr float bodyRadius = 0.45f;
        constexpr float bodyDiagonal = 0.6364f;
        constexpr float bodyHalfHeight = 0.8f;
        struct Ray
        {
            float X;
            float Y;
            float Z;
            float Distance;
        };
        static constexpr Ray rays[10] = {
            {1.0f, 0.0f, 0.0f, bodyRadius},         {-1.0f, 0.0f, 0.0f, bodyRadius},
            {0.0f, 1.0f, 0.0f, bodyRadius},         {0.0f, -1.0f, 0.0f, bodyRadius},
            {0.7071f, 0.7071f, 0.0f, bodyDiagonal}, {-0.7071f, 0.7071f, 0.0f, bodyDiagonal},
            {0.7071f, -0.7071f, 0.0f, bodyDiagonal}, {-0.7071f, -0.7071f, 0.0f, bodyDiagonal},
            {0.0f, 0.0f, 1.0f, bodyHalfHeight},     {0.0f, 0.0f, -1.0f, bodyHalfHeight}};

        for (Ray const& ray : rays)
        {
            if (!map->isInLineOfSight(x, y, z, x + ray.X * ray.Distance, y + ray.Y * ray.Distance,
                                      z + ray.Z * ray.Distance, phase, LINEOFSIGHT_ALL_CHECKS,
                                      VMAP::ModelIgnoreFlags::Nothing))
                return false;
        }
        return true;
    }
}

bool Nav3DManager::BakeAirTile(Map* map, uint32 phase, uint16 tileX, uint16 tileY, float cellSize,
                               float minZ, float maxZ, float boundsMinX, float boundsMaxX,
                               float boundsMinY, float boundsMaxY, std::string const& sourcePath,
                               std::string const& outputPath, std::vector<Position> const& seeds,
                               float corridorRadius, std::string& error)
{
    if (!map)
    {
        error = "no map";
        return false;
    }
    if (cellSize < 0.5f || cellSize > 4.0f || minZ >= maxZ || boundsMinX >= boundsMaxX ||
        boundsMinY >= boundsMaxY)
    {
        error = "invalid bake bounds";
        return false;
    }
    if (seeds.empty())
    {
        error = "at least one air seed is required";
        return false;
    }

    Nav3DTile tile;
    if (!tile.Load(sourcePath))
    {
        error = "cannot load source tile";
        return false;
    }

    Nav3DTileHeader& header = tile.Header();
    if ((header.Medium != Nav3D::MediumAir && header.Medium != Nav3D::MediumAirDetail) ||
        header.CellSize != cellSize)
    {
        error = "source tile is not a matching air tile";
        return false;
    }

    float const originX = header.OriginX;
    float const originY = header.OriginY;
    float const originZ = header.OriginZ;
    int32 const sizeX = int32(header.SizeX);
    int32 const sizeY = int32(header.SizeY);
    int32 const sizeZ = int32(header.SizeZ);

    int32 const x0 = std::clamp(int32(std::floor((boundsMinX - originX) / cellSize)), 0, sizeX - 1);
    int32 const x1 = std::clamp(int32(std::ceil((boundsMaxX - originX) / cellSize)), 0, sizeX);
    int32 const y0 = std::clamp(int32(std::floor((boundsMinY - originY) / cellSize)), 0, sizeY - 1);
    int32 const y1 = std::clamp(int32(std::ceil((boundsMaxY - originY) / cellSize)), 0, sizeY);
    int32 const z0 = std::clamp(int32(std::floor((minZ - originZ) / cellSize)), 0, sizeZ - 1);
    int32 const z1 = std::clamp(int32(std::ceil((maxZ - originZ) / cellSize)), 0, sizeZ);
    if (x0 >= x1 || y0 >= y1 || z0 >= z1)
    {
        error = "bake bounds do not intersect the tile";
        return false;
    }

    int32 const boxX = x1 - x0;
    int32 const boxY = y1 - y0;
    int32 const boxZ = z1 - z0;
    size_t const boxCount = size_t(boxX) * size_t(boxY) * size_t(boxZ);
    if (boxCount == 0 || boxCount > 64ull * 1024ull * 1024ull)
    {
        error = "bake box too large";
        return false;
    }

    auto const boxIndex = [boxX, boxY](int32 x, int32 y, int32 z) -> size_t
    {
        return (size_t(z) * size_t(boxY) + size_t(y)) * size_t(boxX) + size_t(x);
    };
    auto const center = [&](int32 x, int32 y, int32 z) -> G3D::Vector3
    {
        return G3D::Vector3(originX + (float(x) + 0.5f) * cellSize,
                            originY + (float(y) + 0.5f) * cellSize,
                            originZ + (float(z) + 0.5f) * cellSize);
    };

    std::vector<uint8_t> corridor(size_t(boxX) * size_t(boxY), 1);
    if (corridorRadius > 0.0f)
    {
        float const radiusSq = corridorRadius * corridorRadius;
        auto const segmentDistanceSq = [](float x, float y, Position const& a, Position const& b) -> float
        {
            float const ax = a.GetPositionX();
            float const ay = a.GetPositionY();
            float const bx = b.GetPositionX();
            float const by = b.GetPositionY();
            float const dx = bx - ax;
            float const dy = by - ay;
            float const lengthSq = dx * dx + dy * dy;
            float t = 0.0f;
            if (lengthSq > 1.0e-6f)
                t = std::clamp(((x - ax) * dx + (y - ay) * dy) / lengthSq, 0.0f, 1.0f);
            float const px = ax + dx * t;
            float const py = ay + dy * t;
            return (x - px) * (x - px) + (y - py) * (y - py);
        };

        for (int32 y = y0; y < y1; ++y)
        {
            for (int32 x = x0; x < x1; ++x)
            {
                float const worldX = originX + (float(x) + 0.5f) * cellSize;
                float const worldY = originY + (float(y) + 0.5f) * cellSize;
                float best = std::numeric_limits<float>::max();
                for (size_t i = 0; i < seeds.size(); ++i)
                {
                    if (i + 1 < seeds.size())
                        best = std::min(best, segmentDistanceSq(worldX, worldY, seeds[i], seeds[i + 1]));
                    else
                    {
                        float const dx = worldX - seeds[i].GetPositionX();
                        float const dy = worldY - seeds[i].GetPositionY();
                        best = std::min(best, dx * dx + dy * dy);
                    }
                }
                corridor[size_t(y - y0) * size_t(boxX) + size_t(x - x0)] =
                    best <= radiusSq ? 1 : 0;
            }
        }
    }

    std::vector<uint8_t> visited(boxCount, 0);
    std::vector<int8_t> airCache(boxCount, 0);
    auto const isAir = [&](int32 x, int32 y, int32 z) -> bool
    {
        if (!corridor[size_t(y - y0) * size_t(boxX) + size_t(x - x0)])
            return false;
        size_t const index = boxIndex(x - x0, y - y0, z - z0);
        if (airCache[index] != 0)
            return airCache[index] > 0;
        G3D::Vector3 const point = center(x, y, z);
        bool const air = IsLiveAirCell(map, phase, point.x, point.y, point.z);
        airCache[index] = air ? 1 : -1;
        return air;
    };

    std::deque<GridNode> queue;
    size_t liveCells = 0;
    size_t liveColumns = 0;
    for (Position const& seed : seeds)
    {
        int32 const sx = std::clamp(int32(std::floor((seed.GetPositionX() - originX) / cellSize)), x0,
                                    x1 - 1);
        int32 const sy = std::clamp(int32(std::floor((seed.GetPositionY() - originY) / cellSize)), y0,
                                    y1 - 1);
        int32 const sz = std::clamp(int32(std::floor((seed.GetPositionZ() - originZ) / cellSize)), z0,
                                    z1 - 1);
        bool found = false;
        for (int32 radius = 0; radius <= 2 && !found; ++radius)
        {
            for (int32 dx = -radius; dx <= radius && !found; ++dx)
            {
                for (int32 dy = -radius; dy <= radius && !found; ++dy)
                {
                    for (int32 dz = -radius; dz <= radius && !found; ++dz)
                    {
                        int32 const x = sx + dx;
                        int32 const y = sy + dy;
                        int32 const z = sz + dz;
                        if (x < x0 || x >= x1 || y < y0 || y >= y1 || z < z0 || z >= z1)
                            continue;
                        if (!isAir(x, y, z))
                            continue;
                        size_t const index = boxIndex(x - x0, y - y0, z - z0);
                        if (visited[index])
                            continue;
                        visited[index] = 1;
                        ++liveCells;
                        queue.push_back({x, y, z});
                        found = true;
                    }
                }
            }
        }
        if (!found)
        {
            error = "seed has no live air cell nearby";
            return false;
        }
    }

    // Flood-fill only through cells with a clear face edge. Rock that happens to
    // have no collision surface is not connected to the tunnel/cave seed and is
    // therefore removed when the box is replaced.
    static constexpr int32 neighbors[6][3] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    while (!queue.empty())
    {
        GridNode const current = queue.front();
        queue.pop_front();
        G3D::Vector3 const from = center(current.X, current.Y, current.Z);
        for (auto const& offset : neighbors)
        {
            int32 const x = current.X + offset[0];
            int32 const y = current.Y + offset[1];
            int32 const z = current.Z + offset[2];
            if (x < x0 || x >= x1 || y < y0 || y >= y1 || z < z0 || z >= z1)
                continue;
            size_t const index = boxIndex(x - x0, y - y0, z - z0);
            if (visited[index] || !isAir(x, y, z))
                continue;
            G3D::Vector3 const to = center(x, y, z);
            if (!map->isInLineOfSight(from.x, from.y, from.z, to.x, to.y, to.z, phase,
                                      LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
                continue;
            visited[index] = 1;
            ++liveCells;
            queue.push_back({x, y, z});
        }
    }

    for (int32 y = 0; y < sizeY; ++y)
    {
        for (int32 x = 0; x < sizeX; ++x)
        {
            bool const inBox = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (!inBox)
                continue;
            bool any = false;
            for (int32 z = z0; z < z1; ++z)
                if (visited[boxIndex(x - x0, y - y0, z - z0)])
                {
                    any = true;
                    break;
                }
            if (any)
                ++liveColumns;
        }
    }

    std::vector<Nav3DTileInterval> const oldIntervals = tile.Intervals();
    std::vector<Nav3DTileColumn> const oldColumns = tile.Columns();
    std::vector<Nav3DTileInterval> newIntervals;
    newIntervals.reserve(oldIntervals.size() + size_t((x1 - x0) * (y1 - y0)));

    for (int32 y = 0; y < sizeY; ++y)
    {
        for (int32 x = 0; x < sizeX; ++x)
        {
            size_t const columnIndex = size_t(y) * size_t(sizeX) + size_t(x);
            Nav3DTileColumn& column = tile.Columns()[columnIndex];
            Nav3DTileColumn const& oldColumn = oldColumns[columnIndex];

            std::vector<std::pair<int32, int32>> spans;
            bool const inBox = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (inBox)
            {
                for (int32 z = z0; z < z1;)
                {
                    if (!visited[boxIndex(x - x0, y - y0, z - z0)])
                    {
                        ++z;
                        continue;
                    }
                    int32 const start = z;
                    while (z < z1 && visited[boxIndex(x - x0, y - y0, z - z0)])
                        ++z;
                    spans.emplace_back(start, z - 1);
                }
            }
            else
            {
                for (uint32 i = 0; i < oldColumn.IntervalCount; ++i)
                {
                    Nav3DTileInterval const& interval = oldIntervals[oldColumn.IntervalOffset + i];
                    spans.emplace_back(interval.ZMin, interval.ZMax);
                }
            }

            std::sort(spans.begin(), spans.end());
            std::vector<std::pair<int32, int32>> merged;
            for (auto const& span : spans)
            {
                if (merged.empty() || span.first > merged.back().second + 1)
                    merged.push_back(span);
                else
                    merged.back().second = std::max(merged.back().second, span.second);
            }

            column.IntervalOffset = uint32(newIntervals.size());
            column.IntervalCount = uint16(merged.size());
            for (auto const& span : merged)
            {
                Nav3DTileInterval interval;
                interval.ZMin = int16(span.first);
                interval.ZMax = int16(span.second);
                interval.Clearance = 16;
                interval.Flags = 0;
                newIntervals.push_back(interval);
            }
        }
    }

    tile.Intervals() = std::move(newIntervals);
    header.Flags |= Nav3D::FlagHasSolid | Nav3D::FlagLiveLos;
    header.GeneratorVersion = 6;

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path(), ec);
    if (ec)
    {
        error = "cannot create output directory: " + ec.message();
        return false;
    }
    if (!tile.Save(outputPath))
    {
        error = "cannot save air tile";
        return false;
    }

    LOG_INFO("playerbots",
             "nav3dairbake map {} tile {}:{} seeds={} corridor={:.1f} box x{}..{} y{}..{} z{}..{} "
             "liveColumns={} liveCells={}",
             map->GetId(), tileX, tileY, seeds.size(), corridorRadius, x0, x1, y0, y1, z0, z1,
             liveColumns, liveCells);
    return true;
}

bool Nav3DManager::BakeAirDetailTile(Map* map, uint32 phase, uint16 tileX, uint16 tileY, float cellSize,
                                     float minZ, float maxZ, float boundsMinX, float boundsMaxX,
                                     float boundsMinY, float boundsMaxY, float corridorRadius,
                                     std::vector<Position> const& seeds, std::string const& outputPath,
                                     std::string& error)
{
    if (!map || cellSize <= 0.0f || cellSize > 2.0f || minZ >= maxZ ||
        boundsMinX >= boundsMaxX || boundsMinY >= boundsMaxY || seeds.empty())
    {
        error = "invalid detail bake request";
        return false;
    }

    float const originX = std::floor(boundsMinX / cellSize) * cellSize;
    float const originY = std::floor(boundsMinY / cellSize) * cellSize;
    float const originZ = std::floor(minZ / cellSize) * cellSize;
    uint32 const sizeX = std::max<uint32>(
        1, uint32(std::ceil((boundsMaxX - originX) / cellSize)));
    uint32 const sizeY = std::max<uint32>(
        1, uint32(std::ceil((boundsMaxY - originY) / cellSize)));
    uint32 const sizeZ = std::max<uint32>(
        1, uint32(std::ceil((maxZ - originZ) / cellSize)));
    uint64 const cellCount = uint64(sizeX) * uint64(sizeY) * uint64(sizeZ);
    if (sizeZ > 32767 || cellCount > 64ull * 1024ull * 1024ull)
    {
        error = "detail tile too large";
        return false;
    }

    Nav3DTile tile;
    Nav3DTileHeader& header = tile.Header();
    header.MapId = map->GetId();
    header.TileX = tileX;
    header.TileY = tileY;
    header.Medium = Nav3D::MediumAirDetail;
    header.CellSize = cellSize;
    header.OriginX = originX;
    header.OriginY = originY;
    header.OriginZ = originZ;
    header.ExtentX = float(sizeX) * cellSize;
    header.ExtentY = float(sizeY) * cellSize;
    header.ExtentZ = float(sizeZ) * cellSize;
    header.SizeX = sizeX;
    header.SizeY = sizeY;
    header.SizeZ = sizeZ;
    header.Flags |= Nav3D::FlagHasSolid | Nav3D::FlagLiveLos;
    header.GeneratorVersion = 6;
    header.GeometryHash = 0;
    tile.Columns().resize(size_t(sizeX) * size_t(sizeY));

    std::string const templatePath = outputPath + ".template";
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path(), ec);
    if (ec || !tile.Save(templatePath))
    {
        error = "cannot write detail template";
        return false;
    }

    bool const baked = BakeAirTile(map, phase, tileX, tileY, cellSize, minZ, maxZ, boundsMinX,
                                   boundsMaxX, boundsMinY, boundsMaxY, templatePath, outputPath,
                                   seeds, corridorRadius, error);
    std::filesystem::remove(templatePath, ec);
    return baked;
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
