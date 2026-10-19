/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "QuestPoiZStore.h"

#include "DatabaseEnv.h"
#include "PlayerbotsDatabase.h"
#include "QueryResult.h"
#include "Log.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>

QuestPoiZStore& QuestPoiZStore::Instance()
{
    static QuestPoiZStore instance;
    return instance;
}

void QuestPoiZStore::Load()
{
    candidates.clear();

    // Raw query on purpose: installations without the baked data would fail a prepared-statement
    // prepare at startup; a missing table here just logs and leaves the fallback path active.
    QueryResult result = PlayerbotsDatabase.Query(
        "SELECT `QuestId`, `PoiIdx`, `PointIdx`, `MapId`, `Z`, `Rank`, `Confidence`, `Evidence`, "
        "`NavmeshOk`, `WalkablePoints` "
        "FROM `playerbots_quest_poi_z` ORDER BY `QuestId`, `PoiIdx`, `Rank`");

    if (!result)
    {
        LOG_INFO("playerbots", "Quest POI Z store: no baked data (playerbots_quest_poi_z absent or empty); "
                               "quest destinations use the terrain-height fallback.");
        return;
    }

    uint32 rows = 0;
    do
    {
        Field* fields = result->Fetch();

        QuestPoiZCandidate candidate;
        candidate.pointIdx = fields[2].Get<uint16>();
        candidate.mapId = fields[3].Get<uint16>();
        candidate.z = fields[4].Get<float>();
        candidate.rank = fields[5].Get<uint8>();
        candidate.confidence = fields[6].Get<uint8>();
        candidate.evidence = fields[7].Get<std::string>();
        candidate.navmeshOk = fields[8].Get<uint8>() != 0;

        if (!fields[9].IsNull())
        {
            std::string const& walkable = fields[9].Get<std::string>();
            size_t start = 0;
            while (start < walkable.size())
            {
                size_t const end = walkable.find(';', start);
                std::string const token =
                    walkable.substr(start, end == std::string::npos ? std::string::npos : end - start);

                float x = 0.0f, y = 0.0f, z = 0.0f;
                if (sscanf(token.c_str(), "%f,%f,%f", &x, &y, &z) == 3)
                    candidate.walkablePoints.push_back({x, y, z});

                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
        }

        candidates[Key(fields[0].Get<uint32>(), fields[1].Get<uint32>())].push_back(candidate);
        ++rows;
    } while (result->NextRow());

    for (auto& [key, list] : candidates)
        std::sort(list.begin(), list.end(),
                  [](QuestPoiZCandidate const& a, QuestPoiZCandidate const& b) { return a.rank < b.rank; });

    LOG_INFO("playerbots", "Quest POI Z store: loaded {} candidate rows for {} quest POIs.",
             rows, candidates.size());
}

QuestPoiZCandidate const* QuestPoiZStore::Lookup(uint32 questId, uint32 poiId) const
{
    auto itr = candidates.find(Key(questId, poiId));
    if (itr == candidates.end() || itr->second.empty())
        return nullptr;

    return &itr->second.front();
}

QuestPoiZCandidate const* QuestPoiZStore::LookupNearestZ(uint32 questId, uint32 poiId, float referenceZ) const
{
    auto itr = candidates.find(Key(questId, poiId));
    if (itr == candidates.end() || itr->second.empty())
        return nullptr;

    QuestPoiZCandidate const* best = nullptr;
    for (QuestPoiZCandidate const& candidate : itr->second)
    {
        if (!candidate.navmeshOk)
            continue;

        if (!best || std::fabs(candidate.z - referenceZ) < std::fabs(best->z - referenceZ))
            best = &candidate;
    }

    return best ? best : &itr->second.front();
}

std::vector<QuestPoiZCandidate> const* QuestPoiZStore::LookupAll(uint32 questId, uint32 poiId) const
{
    auto itr = candidates.find(Key(questId, poiId));
    if (itr == candidates.end() || itr->second.empty())
        return nullptr;

    return &itr->second;
}

bool QuestPoiZStore::FindNearestWalkablePoint(uint32 questId, uint32 poiId, uint32 mapId, float x, float y,
                                              float& outX, float& outY, float& outZ) const
{
    auto itr = candidates.find(Key(questId, poiId));
    if (itr == candidates.end())
        return false;

    bool found = false;
    float best = FLT_MAX;
    for (QuestPoiZCandidate const& candidate : itr->second)
    {
        if (candidate.mapId != mapId || !candidate.navmeshOk)
            continue;

        for (std::array<float, 3> const& point : candidate.walkablePoints)
        {
            float const dx = point[0] - x;
            float const dy = point[1] - y;
            float const dist = dx * dx + dy * dy;
            if (dist < best)
            {
                best = dist;
                outX = point[0];
                outY = point[1];
                outZ = point[2];
                found = true;
            }
        }
    }

    return found;
}
