/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "NearestNpcsValue.h"

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Playerbots.h"
#include "Vehicle.h"

void NearestNpcsValue::FindUnits(std::list<Unit*>& targets)
{
    Acore::AnyUnitInObjectRangeCheck u_check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
}

bool NearestNpcsValue::AcceptUnit(Unit* unit) { return !unit->IsHostileTo(bot) && !unit->IsPlayer(); }

void NearestHostileNpcsValue::FindUnits(std::list<Unit*>& targets)
{
    Acore::AnyUnitInObjectRangeCheck u_check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
}

bool NearestHostileNpcsValue::AcceptUnit(Unit* unit) { return unit->IsHostileTo(bot) && !unit->IsPlayer(); }

void NearestVehiclesValue::FindUnits(std::list<Unit*>& targets)
{
    Acore::AnyUnitInObjectRangeCheck u_check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
}

bool NearestVehiclesValue::AcceptUnit(Unit* unit)
{
    if (!unit || !unit->IsVehicle() || !unit->IsAlive())
        return false;

    Vehicle* veh = unit->GetVehicleKit();
    if (!veh || !veh->GetAvailableSeatCount())
        return false;

    return true;
}

void NearestTriggersValue::FindUnits(std::list<Unit*>& targets)
{
    Acore::AnyUnfriendlyUnitInObjectRangeCheck u_check(bot, bot, range);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
}

bool NearestTriggersValue::AcceptUnit(Unit* unit) { return !unit->IsPlayer(); }

void NearestTotemsValue::FindUnits(std::list<Unit*>& targets)
{
    Acore::AnyUnitInObjectRangeCheck u_check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
}

bool NearestTotemsValue::AcceptUnit(Unit* unit) { return unit->IsTotem(); }

void NearestQuestNpcsValue::FindUnits(std::list<Unit*>& targets)
{
    Acore::AnyUnitInObjectRangeCheck u_check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
}

bool NearestQuestNpcsValue::AcceptUnit(Unit* unit)
{
    if (!unit || unit->IsPlayer() || !unit->IsAlive())
        return false;

    static std::unordered_set<uint32> questNpcEntries = GetRequiredNpcEntries();
    return questNpcEntries.find(unit->GetEntry()) != questNpcEntries.end();
}

std::unordered_set<uint32> NearestQuestNpcsValue::GetRequiredNpcEntries()
{
    std::unordered_set<uint32> entries;

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;

        const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 npcOrGo = quest->RequiredNpcOrGo[i];
            if (npcOrGo > 0)  // Only NPCs
                entries.insert(uint32(npcOrGo));
        }
    }

    return entries;
}
