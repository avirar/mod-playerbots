/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "PossibleRpgTargetsValue.h"

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "ServerFacade.h"
#include "SharedDefines.h"
#include "NearestGameObjects.h"

// Static member initialization
std::vector<uint32> RpgNpcFlags::standardFlags;

const std::vector<uint32>& RpgNpcFlags::GetStandardRpgFlags()
{
    if (standardFlags.empty())
        InitializeFlags();
    return standardFlags;
}

void RpgNpcFlags::InitializeFlags()
{
    standardFlags = {
        static_cast<uint32>(RpgNpcType::INNKEEPER),
        // Removed GOSSIP - prevents targeting guards and other flavor NPCs
        static_cast<uint32>(RpgNpcType::QUESTGIVER),
        static_cast<uint32>(RpgNpcType::FLIGHTMASTER),
        static_cast<uint32>(RpgNpcType::BANKER),
        static_cast<uint32>(RpgNpcType::GUILD_BANKER),
        static_cast<uint32>(RpgNpcType::TRAINER_CLASS),
        static_cast<uint32>(RpgNpcType::TRAINER_PROFESSION),
        static_cast<uint32>(RpgNpcType::VENDOR_AMMO),
        static_cast<uint32>(RpgNpcType::VENDOR_FOOD),
        static_cast<uint32>(RpgNpcType::VENDOR_POISON),
        static_cast<uint32>(RpgNpcType::VENDOR_REAGENT),
        static_cast<uint32>(RpgNpcType::AUCTIONEER),
        static_cast<uint32>(RpgNpcType::STABLEMASTER),
        static_cast<uint32>(RpgNpcType::PETITIONER),
        static_cast<uint32>(RpgNpcType::TABARDDESIGNER),
        static_cast<uint32>(RpgNpcType::BATTLEMASTER),
        static_cast<uint32>(RpgNpcType::TRAINER),
        static_cast<uint32>(RpgNpcType::VENDOR),
        static_cast<uint32>(RpgNpcType::REPAIR)
    };
}

PossibleRpgTargetsValue::PossibleRpgTargetsValue(PlayerbotAI* botAI, float range)
    : NearestUnitsValue(botAI, "possible rpg targets", range, true)
{
    // No initialization needed - using centralized RpgNpcFlags helper
}

void PossibleRpgTargetsValue::FindUnits(std::list<Unit*>& targets)
{
    Acore::AnyUnitInObjectRangeCheck u_check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);
}

bool PossibleRpgTargetsValue::AcceptUnit(Unit* unit)
{
    if (unit->IsHostileTo(bot) || unit->GetTypeId() == TYPEID_PLAYER)
        return false;

    if (sServerFacade->GetDistance2d(bot, unit) <= sPlayerbotAIConfig->tooCloseDistance)
        return false;

    if (unit->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITHEALER))
        return false;

    for (uint32 npcFlag : RpgNpcFlags::GetStandardRpgFlags())
    {
        if (unit->HasFlag(UNIT_NPC_FLAGS, npcFlag))
            return true;
    }

    TravelTarget* travelTarget = context->GetValue<TravelTarget*>("travel target")->Get();
    if (travelTarget->getDestination() && travelTarget->getDestination()->getEntry() == unit->GetEntry())
        return true;

    if (urand(1, 100) < 25 && unit->IsFriendlyTo(bot))
        return true;

    if (urand(1, 100) < 5)
        return true;

    return false;
}


PossibleNewRpgTargetsValue::PossibleNewRpgTargetsValue(PlayerbotAI* botAI, float range)
    : NearestUnitsValue(botAI, "possible new rpg targets", range, true)
{
    // No initialization needed - using centralized RpgNpcFlags helper
}

GuidVector PossibleNewRpgTargetsValue::Calculate()
{
    std::list<Unit*> targets;
    FindUnits(targets);

    GuidVector results;
    std::vector<std::pair<ObjectGuid, float>> guidDistancePairs;
    for (Unit* unit : targets)
    {
        if (AcceptUnit(unit) && (ignoreLos || bot->IsWithinLOSInMap(unit)))
            guidDistancePairs.push_back({unit->GetGUID(), bot->GetExactDist(unit)});
    }
    // Override to sort by distance
    std::sort(guidDistancePairs.begin(), guidDistancePairs.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });
    
    for (const auto& pair : guidDistancePairs) {
        results.push_back(pair.first);
    }
    return results;
}

void PossibleNewRpgTargetsValue::FindUnits(std::list<Unit*>& targets)
{
    Acore::AnyUnitInObjectRangeCheck u_check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);
}

bool PossibleNewRpgTargetsValue::AcceptUnit(Unit* unit)
{
    if (unit->IsHostileTo(bot) || unit->GetTypeId() == TYPEID_PLAYER)
        return false;

    if (unit->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITHEALER))
        return false;

    for (uint32 npcFlag : RpgNpcFlags::GetStandardRpgFlags())
    {
        if (unit->HasFlag(UNIT_NPC_FLAGS, npcFlag))
            return true;
    }

    return false;
}

PossibleNewRpgTargetsNoLosValue::PossibleNewRpgTargetsNoLosValue(PlayerbotAI* botAI, float range)
    : NearestUnitsValue(botAI, "possible new rpg targets no los", range, true)
{
    // No initialization needed - using centralized RpgNpcFlags helper
}

GuidVector PossibleNewRpgTargetsNoLosValue::Calculate()
{
    std::list<Unit*> targets;
    FindUnits(targets);

    GuidVector results;
    std::vector<std::pair<ObjectGuid, float>> guidDistancePairs;
    for (Unit* unit : targets)
    {
        // No LOS check - accept all units that pass AcceptUnit test
        if (AcceptUnit(unit))
        {
            float distance = bot->GetExactDist(unit);
            guidDistancePairs.push_back({unit->GetGUID(), distance});
        }
    }
    
    // Sort by 3D distance to prioritize same-elevation NPCs
    std::sort(guidDistancePairs.begin(), guidDistancePairs.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });
    
    for (const auto& pair : guidDistancePairs) {
        results.push_back(pair.first);
    }
    return results;
}

void PossibleNewRpgTargetsNoLosValue::FindUnits(std::list<Unit*>& targets)
{
    Acore::AnyUnitInObjectRangeCheck u_check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);
}

bool PossibleNewRpgTargetsNoLosValue::AcceptUnit(Unit* unit)
{
    if (unit->IsHostileTo(bot) || unit->GetTypeId() == TYPEID_PLAYER)
        return false;

    if (unit->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITHEALER))
        return false;

    for (uint32 npcFlag : RpgNpcFlags::GetStandardRpgFlags())
    {
        if (unit->HasFlag(UNIT_NPC_FLAGS, npcFlag))
            return true;
    }

    return false;
}

std::vector<GameobjectTypes> PossibleNewRpgGameObjectsValue::allowedGOFlags;

GuidVector PossibleNewRpgGameObjectsValue::Calculate()
{
    std::list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck u_check(bot, range);
    Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);

    
    std::vector<std::pair<ObjectGuid, float>> guidDistancePairs;
    for (GameObject* go : targets)
    {
        bool flagCheck = false;
        for (uint32 goFlag : allowedGOFlags)
        {
            if (go->GetGoType() == goFlag)
            {
                flagCheck = true;
                break;
            }
        }
        if (!flagCheck)
            continue;
        
        if (!ignoreLos && !bot->IsWithinLOSInMap(go))
            continue;
        
        // For GOOBER type gameobjects, check if they're required by an active quest
        if (go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
        {
            bool isQuestObjective = false;
            int32 goEntry = go->GetEntry();
            
            // Check if this gameobject is required by any active quest
            for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 questId = bot->GetQuestSlotQuestId(slot);
                if (!questId)
                    continue;
                    
                Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                if (!quest)
                    continue;
                
                for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                {
                    // Handle negative gameobject IDs (quest->RequiredNpcOrGo[i] < 0 means gameobject)
                    int32 requiredEntry = quest->RequiredNpcOrGo[i];
                    if ((requiredEntry < 0 && -requiredEntry == goEntry) && quest->RequiredNpcOrGoCount[i] > 0)
                    {
                        // Check if this objective is not yet completed
                        QuestStatusData const& q_status = bot->getQuestStatusMap().at(questId);
                        if (q_status.CreatureOrGOCount[i] < quest->RequiredNpcOrGoCount[i])
                        {
                            isQuestObjective = true;
                            break;
                        }
                    }
                }
                if (isQuestObjective)
                    break;
            }
            
            if (!isQuestObjective)
                continue;
        }
        
        guidDistancePairs.push_back({go->GetGUID(), bot->GetExactDist(go)});
    }
    GuidVector results;

    // Sort by distance
    std::sort(guidDistancePairs.begin(), guidDistancePairs.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });
    
    for (const auto& pair : guidDistancePairs) {
        results.push_back(pair.first);
    }
    return results;
}
