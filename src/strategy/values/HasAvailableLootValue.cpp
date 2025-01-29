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

    // 1. Use the normal loot distance and max "search" distance
    float baseLootDistance = sPlayerbotAIConfig->lootDistance;    // e.g. 15 yards
    float maxSearchDistance = sPlayerbotAIConfig->sightDistance;  // e.g. 75 yards

    // 2. Retrieve all loot objects within your max search distance
    std::vector<LootObject> possibleLoots = lootStack->OrderByDistance(maxSearchDistance);
    if (possibleLoots.empty())
        return false;

    // 3. We will pick the nearest lootable object
    LootObject bestLoot;
    float bestDist = std::numeric_limits<float>::max();

    // 4. Check each candidate
    for (auto const& loot : possibleLoots)
    {
        if (loot.guid.IsEmpty())
            continue;

        // Distance from the player to this loot object
        float dist = AI_VALUE2(float, "distance", loot.guid);
        if (dist > maxSearchDistance)
            continue; // It's beyond even our sight distance, so ignore

        // 5. Figure out if it's a GameObject or a corpse, and set the "allowed" distance
        float allowedDist = baseLootDistance; // e.g. 15 yards for corpse by default
        if (loot.guid.IsGameObject())
        {
            // We want to allow a bigger distance (e.g. x5)
            allowedDist *= 5.0f; // e.g. 75 yards
        }

        // 6. Check if this loot is actually in range for that type
        if (dist <= allowedDist)
        {
            // 7. If it is lootable, see if it is the closest so far
            if (dist < bestDist)
            {
                bestDist = dist;
                bestLoot = loot;
            }
        }
    }

    // 8. Finally, return true if we found any lootable object
    if (!bestLoot.guid.IsEmpty())
        return true;

    return false;
}
