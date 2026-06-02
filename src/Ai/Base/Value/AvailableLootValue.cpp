/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "AvailableLootValue.h"

#include "Log.h"
#include "LootObjectStack.h"
#include "Playerbots.h"
#include "ServerFacade.h"

AvailableLootValue::AvailableLootValue(PlayerbotAI* botAI, std::string const name)
    : ManualSetValue<LootObjectStack*>(botAI, nullptr, name)
{
    value = new LootObjectStack(botAI->GetBot());
}

AvailableLootValue::~AvailableLootValue() { delete value; }

LootTargetValue::LootTargetValue(PlayerbotAI* botAI, std::string const name)
    : ManualSetValue<LootObject>(botAI, LootObject(), name)
{
}

bool CanLootValue::Calculate()
{
    LootObject loot = AI_VALUE(LootObject, "loot target");
    bool notEmpty = !loot.IsEmpty();
    bool hasWo = loot.GetWorldObject(bot) != nullptr;
    bool possible = loot.IsLootPossible(bot);
    float dist = AI_VALUE2(float, "distance", "loot target");
    bool inRange = ServerFacade::instance().IsDistanceLessOrEqualThan(dist, INTERACTION_DISTANCE - 2);

    bool result = notEmpty && hasWo && possible && inRange;

    if (result != _lastResult)
    {
        LOG_DEBUG("playerbots", "CanLoot: notEmpty={} hasWo={} possible={} dist={:.1f} inRange={} => {}",
            notEmpty, hasWo, possible, dist, inRange, result);
        _lastResult = result;
    }

    return result;
}
