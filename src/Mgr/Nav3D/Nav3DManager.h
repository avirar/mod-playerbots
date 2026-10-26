/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NAV3DMANAGER_H
#define PLAYERBOTS_NAV3DMANAGER_H

#include "Define.h"
#include "MoveSplineInitArgs.h"
#include "Position.h"

class Map;

struct Nav3DResult
{
    bool Found = false;
    bool Partial = false;
    bool Direct = false;
    bool Reversed = false;
    uint32 Nodes = 0;
    uint32 Expanded = 0;
    uint32 ElapsedMs = 0;
    float CellSize = 0.0f;
    float Length = 0.0f;
    float BestDistance = 0.0f;
    float BestX = 0.0f;
    float BestY = 0.0f;
    float BestZ = 0.0f;
};

// Lazy volumetric pathfinder used by the Nav3D P0b spike. It samples the local water/air
// volume and validates every edge against the map collision mesh; it does not use .n3t data.
class Nav3DManager
{
public:
    static Nav3DManager& Instance();

    bool FindPath(Map* map, uint32 phase, Position const& start, Position const& goal,
                  Movement::PointsArray& path, Nav3DResult& result);

private:
    Nav3DManager() = default;

    bool FindPathDirected(Map* map, uint32 phase, Position const& start, Position const& goal,
                          Movement::PointsArray& path, Nav3DResult& result);
};

#endif
