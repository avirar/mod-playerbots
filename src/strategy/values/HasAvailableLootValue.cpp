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

#include "HasAvailableLootValue.h"

#include "LootObjectStack.h"
#include "Playerbots.h"
#include "PlayerbotAIConfig.h"
#include <limits> // for std::numeric_limits
#include <set>    // To track checked loot objects

bool HasAvailableLootValue::Calculate()
{
    // Get the available loot stack
    LootObjectStack* lootStack = AI_VALUE(LootObjectStack*, "available loot");
    if (!lootStack)
        return false; // No loot available

    // Define normal loot distance and max search range
    float baseLootDistance = sPlayerbotAIConfig->lootDistance;    // Default loot range (e.g., 15 yards)
    float maxSearchDistance = sPlayerbotAIConfig->sightDistance;  // Max possible search range (e.g., 75 yards)

    std::set<ObjectGuid> checkedLoots; // Prevent duplicate loot checks

    LootObject bestLoot;
    bestLoot.guid.Clear(); // Initialize bestLoot to an invalid object
    float bestDist = std::numeric_limits<float>::max();

    while (true)
    {
        // Retrieve the next closest loot object
        LootObject loot = lootStack->GetLoot(maxSearchDistance);
        if (loot.guid.IsEmpty()) 
            break; // No more loot objects found

        // Prevent duplicate checks
        if (checkedLoots.find(loot.guid) != checkedLoots.end())
            continue; // Skip already checked loot

        checkedLoots.insert(loot.guid); // Mark as checked

        // ✅ **Fix: Ensure loot.guid is valid before fetching distance**
        if (!loot.guid.IsPlayer() && !loot.guid.IsCreature() && !loot.guid.IsGameObject()) 
            continue; // Skip invalid objects

        // ✅ **Fix: Use correct AI_VALUE2 lookup based on object type**
        Unit* target = nullptr;
        if (loot.guid.IsPlayer() || loot.guid.IsCreature()) 
        {
            target = AI_VALUE2(Unit*, "nearest unit", loot.guid.ToString());
        } 
        else if (loot.guid.IsGameObject()) 
        {
            target = AI_VALUE2(GameObject*, "nearest game object", loot.guid.ToString());
        }
        if (!target) 
            continue; // Skip if target is invalid or not found

        float dist = AI_VALUE2(float, "distance", loot.guid.ToString());

        // ✅ **Fix: Ensure Distance Calculation Doesn't Use a Negative Value**
        if (dist <= 0.0f || dist > maxSearchDistance)
            continue; // Ignore objects beyond max search range or bad values

        // Adjust allowed loot distance based on type
        float allowedDist = baseLootDistance; // Default (e.g., 15 yards for corpses)
        if (loot.guid.IsGameObject()) 
        {
            allowedDist *= 5.0f; // Increase distance for GameObjects (e.g., chests, herbs, mining nodes)
        }

        // Check if loot is within allowed range
        if (dist <= allowedDist + 0.1f) // Use small epsilon for floating-point precision
        {
            // Choose the closest lootable object
            if (dist < bestDist)
            {
                bestDist = dist;
                bestLoot = loot;
            }
        }
    }

    // Return true if a valid loot object was found
    return !bestLoot.guid.IsEmpty();
}

