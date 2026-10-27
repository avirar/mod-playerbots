/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NAV3DMANAGER_H
#define PLAYERBOTS_NAV3DMANAGER_H

#include "Define.h"
#include "MoveSplineInitArgs.h"
#include "Nav3DTile.h"
#include "Position.h"

#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

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
    bool FindPathToSurface(Map* map, uint32 phase, Position const& start, Movement::PointsArray& path,
                           Nav3DResult& result);
    bool IsPathClear(Map* map, uint32 phase, Movement::PointsArray const& path, float radius = 0.5f) const;

    // Precomputed .n3t tile cache. The lazy pathfinder remains the fallback until the
    // generator and tile search are wired in.
    std::shared_ptr<Nav3DTile> GetTile(uint32 mapId, uint16 tileX, uint16 tileY, uint8 medium);
    bool HasSolidTileAt(Map* map, Position const& position);
    void ClearTileCache();

    // Live-Map bake for WMO/dynamic interiors that the static VMAP extractor does not
    // reproduce. Writes a single water tile; intended for a one-time GM command.
    bool BakeWaterTile(Map* map, uint32 phase, uint16 tileX, uint16 tileY, float cellSize,
                       float waterLevel, float minZ, float boundsMinX, float boundsMaxX,
                       float boundsMinY, float boundsMaxY, std::string const& outputPath,
                       std::string& error);

private:
    Nav3DManager() = default;

    bool FindPathDirected(Map* map, uint32 phase, Position const& start, Position const& goal,
                          Movement::PointsArray& path, Nav3DResult& result,
                          Nav3DTile const* tile = nullptr);
    bool FindPathTile(Map* map, uint32 phase, Position const& start, Position const& goal,
                      Movement::PointsArray& path, Nav3DResult& result);

    struct TileCacheEntry
    {
        uint64 Key = 0;
        std::shared_ptr<Nav3DTile> Tile;
    };

    std::mutex _tileCacheMutex;
    std::list<TileCacheEntry> _tileLru;
    std::unordered_map<uint64, std::list<TileCacheEntry>::iterator> _tileCache;
};

#endif
