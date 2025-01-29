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
// adjustedLootDistance *= 5.0f; // 15 * 5 = 75, SightDistance


bool HasAvailableLootValue::Calculate()
{
    LootObjectStack* lootStack = AI_VALUE(LootObjectStack*, "available loot");
    if (!lootStack)
        return false;

    float baseLootDistance = sPlayerbotAIConfig->lootDistance; // Default loot distance

    LootObject loot = lootStack->GetLoot(baseLootDistance); // Retrieve nearest loot object
    if (loot.guid.IsEmpty()) // Corrected check for an invalid GUID
        return false;

    float adjustedLootDistance = baseLootDistance; // Set default adjusted to base

    if (loot.guid.IsGameObject()) // Check if the loot is a GameObject
    {
        adjustedLootDistance *= 5.0f; // Double the loot distance for GOs
    }

    return lootStack->CanLoot(adjustedLootDistance); // Use adjusted distance
}
