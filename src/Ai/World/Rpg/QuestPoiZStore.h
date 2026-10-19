/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_QUESTPOIZSTORE_H
#define PLAYERBOTS_QUESTPOIZSTORE_H

#include "Define.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

// One baked walkable Z candidate for a quest POI point (see the destination-Z plan).
struct QuestPoiZCandidate
{
    float z{0.0f};
    uint8 rank{1};
    uint8 confidence{0};
    std::string evidence;
    bool navmeshOk{true};
    uint16 pointIdx{0};
    uint16 mapId{0};
    // Baked walkable sample points (x, y, z) for this POI point.
    std::vector<std::array<float, 3>> walkablePoints;
};

// Baked walkable Z candidates for quest POIs (`playerbots_quest_poi_z`), generated offline by
// acore-data/scripts/quest_poi_z_baker.py. Loaded once at startup on the world thread; read-only
// afterwards. Absent table/rows = destinations keep using the terrain-height fallback.
class QuestPoiZStore
{
public:
    static QuestPoiZStore& Instance();

    void Load();

    // Best (lowest rank) baked candidate for a quest_poi row, or nullptr when unmapped.
    QuestPoiZCandidate const* Lookup(uint32 questId, uint32 poiId) const;

    // Candidate whose Z is closest to referenceZ (different floors of the same POI).
    QuestPoiZCandidate const* LookupNearestZ(uint32 questId, uint32 poiId, float referenceZ) const;

    // All ranked candidates (rank order) - reserved for the attempt-variation runtime.
    std::vector<QuestPoiZCandidate> const* LookupAll(uint32 questId, uint32 poiId) const;

    // Nearest baked walkable sample point for a quest POI, used to approach interiors/mines
    // (exploration triggers, hive/focus objects). False when no sample exists on mapId.
    bool FindNearestWalkablePoint(uint32 questId, uint32 poiId, uint32 mapId, float x, float y,
                                  float& outX, float& outY, float& outZ) const;

private:
    static uint64_t Key(uint32 questId, uint32 poiId) { return (uint64_t(questId) << 32) | uint64_t(poiId); }

    std::unordered_map<uint64_t, std::vector<QuestPoiZCandidate>> candidates;
};

#endif
