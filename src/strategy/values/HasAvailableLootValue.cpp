/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "HasAvailableLootValue.h"

#include "LootObjectStack.h"
#include "Playerbots.h"

bool HasAvailableLootValue::Calculate()
{
    LootObjectStack* lootStack = AI_VALUE(LootObjectStack*, "available loot");
    if (!lootStack)
        return false;

    float adjustedLootDistance = sPlayerbotAIConfig->lootDistance; // Default loot distance

    for (LootObject loot : lootStack->GetLoot(adjustedLootDistance))
    {
        if (loot.IsGameObject()) // Check if the lootable object is a GameObject (like chest/herb/mining node)
        {
            adjustedLootDistance *= 5.0f; // 15 * 5 = 75, SightDistance
        }

        if (lootStack->CanLoot(adjustedLootDistance))
        {
            return true;
        }
    }
    return false;
}
