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
    constexpr int32 kMaxCellsPerAxis = 512;
    constexpr float kSurfaceMargin = 0.5f;
    constexpr float kFloorMargin = 0.25f;
    constexpr uint32 kHardMaxNodes = 500000;
    constexpr uint32 kHardMaxTimeMs = 5000;
    constexpr uint32 kTimeCheckMask = 0x7F;
    constexpr float kDuplicateEpsilon = 0.05f;
    constexpr float kPartialAcceptDistance = 5.0f;  // interaction range
    constexpr float kEarlyPartialDistance = 2.0f;   // stop as soon as a chest is in reach

    struct GridNode
    {
        int32 X = 0;
        int32 Y = 0;
        int32 Z = 0;
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

    if (LineClear(map, phase, start, goal))
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
        static GridNode const offsets[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (GridNode const& offset : offsets)
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

    if (result.Partial)
    {
        Position const partial = grid.Center(goalNode);
        appendUnique({partial.GetPositionX(), partial.GetPositionY(), partial.GetPositionZ()});
    }
    else
    {
        appendUnique({goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ()});
    }

    path.push_back(points.front());
    size_t index = 0;
    while (index + 1 < points.size())
    {
        size_t best = index + 1;
        for (size_t candidate = points.size() - 1; candidate > index + 1; --candidate)
        {
            if (LineClear(map, phase, points[index], points[candidate]))
            {
                best = candidate;
                break;
            }
        }

        path.push_back(points[best]);
        index = best;
    }

    if (path.size() < 2)
    {
        path.clear();
        path.push_back({start.GetPositionX(), start.GetPositionY(), start.GetPositionZ()});
        path.push_back({goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ()});
    }

    result.Length = 0.0f;
    for (size_t i = 1; i < path.size(); ++i)
        result.Length += Distance(path[i - 1], path[i]);

    result.Found = true;
    return true;
}
