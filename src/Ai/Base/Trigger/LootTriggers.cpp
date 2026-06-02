/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "LootTriggers.h"

#include "Log.h"
#include "LootObjectStack.h"
#include "Playerbots.h"
#include "ServerFacade.h"

bool LootAvailableTrigger::IsActive()
{
    bool hasLoot = AI_VALUE(bool, "has available loot");
    bool noTargets = AI_VALUE(GuidVector, "all targets").empty();

    bool distanceCheck = false;
    if (botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT))
    {
        distanceCheck =
            ServerFacade::instance().IsDistanceLessOrEqualThan(AI_VALUE2(float, "distance", "loot target"), CONTACT_DISTANCE);
    }
    else
    {
        distanceCheck = ServerFacade::instance().IsDistanceLessOrEqualThan(AI_VALUE2(float, "distance", "loot target"),
                                                                 INTERACTION_DISTANCE - 2.0f);
    }

    LOG_DEBUG("playerbots", "LootAvailableTrigger: hasLoot={} distanceCheck={} noTargets={} stay={}",
        hasLoot, distanceCheck, noTargets, botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT));

    // if loot target if empty, always pass distance check
    return hasLoot && (distanceCheck || noTargets);
}

bool FarFromCurrentLootTrigger::IsActive()
{
    LootObject loot = AI_VALUE(LootObject, "loot target");
    if (loot.IsEmpty())
    {
        LOG_DEBUG("playerbots", "FarFromLootTrigger: No loot target set");
        return false;
    }

    bool possible = loot.IsLootPossible(bot);
    WorldObject* wo = loot.GetWorldObject(bot);
    float dist = wo ? bot->GetDistance(wo) : -1.0f;

    LOG_DEBUG("playerbots", "FarFromLootTrigger: target={} dist={:.1f} isLootPossible={} threshold={:.1f}",
        wo ? wo->GetName() : "null", dist, possible, INTERACTION_DISTANCE - 2.0f);

    if (!possible)
        return false;

    return dist >= INTERACTION_DISTANCE - 2.0f;
}

bool CanLootTrigger::IsActive() { return AI_VALUE(bool, "can loot"); }
