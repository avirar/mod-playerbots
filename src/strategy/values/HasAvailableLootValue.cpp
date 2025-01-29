/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "HasAvailableLootValue.h"

#include "LootObjectStack.h"
#include "Playerbots.h"

#include "HasAvailableLootValue.h"
#include "LootObjectStack.h"
#include "PlayerbotAIConfig.h"

bool HasAvailableLootValue::Calculate()
{
    LootObjectStack* lootStack = AI_VALUE(LootObjectStack*, "available loot");
    if (!lootStack)
        return false;

    float baseLootDistance = sPlayerbotAIConfig->lootDistance; // Default loot distance

    std::vector<LootObject> lootList = lootStack->OrderByDistance(baseLootDistance); // Get loot objects
    if (lootList.empty()) // Check if loot is available
        return false;

    for (LootObject& loot : lootList) 
    {
        float adjustedLootDistance = baseLootDistance; // Reset for each loot object

        if (loot.IsGameObject()) // Check if the lootable object is a GameObject (chest/herb/mining node)
        {
            adjustedLootDistance *= 5.0f; // 15 * 5 = 75, SightDistance
        }

        if (lootStack->CanLoot(adjustedLootDistance)) // Use adjusted distance
        {
            return true;
        }
    }
    return false;
}
