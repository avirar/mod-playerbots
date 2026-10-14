/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "InteractProfiles.h"

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "GridTerrainData.h"
#include "IVMapMgr.h"
#include "NearestGameObjects.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include <cmath>
#include <list>

namespace
{
Unit* FindNearestCreatureByEntry(Player* bot, uint32 entry, float range)
{
    std::list<Unit*> targets;
    Acore::AnyUnitInObjectRangeCheck u_check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);

    Unit* best = nullptr;
    float bestDistance = range;

    for (Unit* unit : targets)
    {
        if (!unit || !unit->IsInWorld() || unit->IsPlayer() || !unit->IsAlive())
            continue;

        if (entry && unit->GetEntry() != entry)
            continue;

        if (!bot->IsWithinLOSInMap(unit))
            continue;

        float const distance = bot->GetDistance(unit);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = unit;
        }
    }

    return best;
}

GameObject* FindNearestGameObjectByEntry(Player* bot, uint32 entry, float range)
{
    std::list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck u_check(bot, range);
    Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);

    GameObject* best = nullptr;
    float bestDistance = range;

    for (GameObject* go : targets)
    {
        if (!go || !go->IsInWorld())
            continue;

        if (entry && go->GetEntry() != entry)
            continue;

        float const distance = bot->GetDistance(go);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = go;
        }
    }

    return best;
}

template <typename T>
void FillTarget(InteractTarget& out, QuestItemCandidate const& candidate, T* object, float distance)
{
    out.candidate = candidate;
    out.guid = object->GetGUID();
    out.mapId = object->GetMapId();
    out.x = object->GetPositionX();
    out.y = object->GetPositionY();
    out.z = object->GetPositionZ();
    out.distance = distance;
}
} // namespace

std::unique_ptr<InteractProfile> InteractProfile::Create(InteractKind kind)
{
    switch (kind)
    {
        case InteractKind::UseOnCreature:
            return std::make_unique<UseOnCreatureProfile>();
        case InteractKind::UseOnGameObject:
            return std::make_unique<UseOnGameObjectProfile>();
        case InteractKind::UseAtLocation:
            return std::make_unique<UseAtLocationProfile>();
        case InteractKind::SelfUse:
        default:
            return std::make_unique<SelfUseProfile>();
    }
}

bool SelfUseProfile::SelectTarget(Player* bot, QuestItemCandidate const& candidate, float /*range*/,
                                  InteractTarget& out) const
{
    out = InteractTarget();
    out.candidate = candidate;
    out.mapId = bot->GetMapId();
    out.x = bot->GetPositionX();
    out.y = bot->GetPositionY();
    out.z = bot->GetPositionZ();
    out.distance = 0.0f;
    return true;
}

bool UseOnCreatureProfile::SelectTarget(Player* bot, QuestItemCandidate const& candidate, float range,
                                        InteractTarget& out) const
{
    Unit* unit = FindNearestCreatureByEntry(bot, candidate.targetEntry, range);
    if (!unit)
        return false;

    FillTarget(out, candidate, unit, bot->GetDistance(unit));
    return true;
}

bool UseOnGameObjectProfile::SelectTarget(Player* bot, QuestItemCandidate const& candidate, float range,
                                          InteractTarget& out) const
{
    GameObject* go = FindNearestGameObjectByEntry(bot, candidate.targetEntry, range);
    if (!go)
        return false;

    FillTarget(out, candidate, go, bot->GetDistance(go));
    return true;
}

bool UseAtLocationProfile::SelectTarget(Player* bot, QuestItemCandidate const& candidate, float range,
                                        InteractTarget& out) const
{
    QuestPOIVector const* poiVector = sObjectMgr->GetQuestPOIVector(candidate.questId);
    if (!poiVector)
        return false;

    // Item objective idx maps to POI ObjectiveIndex 4 + idx (see the quest-item census).
    int32 const wantedIdx = int32(4 + candidate.objectiveIdx);

    bool found = false;
    InteractTarget best;
    float bestDistance = range;

    for (QuestPOI const& poi : *poiVector)
    {
        if (poi.MapId != bot->GetMapId())
            continue;

        if (poi.ObjectiveIndex != wantedIdx)
            continue;

        for (QuestPOIPoint const& point : poi.points)
        {
            float const dx = float(point.x);
            float const dy = float(point.y);
            // The POI carries x/y only; synthesize z like the rest of the module (surface floor).
            float const dz =
                std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(dx, dy));
            if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
                continue;

            float const distance = bot->GetDistance(dx, dy, dz);
            if (distance >= bestDistance)
                continue;

            bestDistance = distance;
            best = InteractTarget();
            best.candidate = candidate;
            best.mapId = bot->GetMapId();
            best.x = dx;
            best.y = dy;
            best.z = dz;
            best.distance = distance;
            found = true;
        }
    }

    if (!found)
        return false;

    out = best;
    return true;
}
