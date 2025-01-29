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
    // Get the available loot stack
    LootObjectStack* lootStack = AI_VALUE(LootObjectStack*, "available loot");
    if (!lootStack)
        return false; // No loot available

    // Define normal loot distance and max search range
    float baseLootDistance = sPlayerbotAIConfig->lootDistance;    // Default loot range (e.g., 15 yards)
    float maxSearchDistance = sPlayerbotAIConfig->sightDistance;  // Max possible search range (e.g., 75 yards)

    std::set<ObjectGuid> checkedLoots; // To prevent checking the same loot multiple times

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
            break; // We have already checked this loot object

        checkedLoots.insert(loot.guid); // Mark as checked

        // Get the distance to this loot object
        float dist = AI_VALUE2(float, "distance", loot.guid);
        if (dist > maxSearchDistance)
            continue; // Ignore objects beyond max search range

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
