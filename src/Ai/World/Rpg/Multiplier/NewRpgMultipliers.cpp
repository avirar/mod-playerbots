/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgMultipliers.h"
#include "NewRpgBaseAction.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

float NewRpgLootPriorityMultiplier::GetValue(Action* action)
{
    // Filter first: "has available loot" walks the loot stack, so only pay for it for the actions
    // this multiplier can actually block.
    if (!sPlayerbotAIConfig.lootPriority || !action || !dynamic_cast<NewRpgBaseAction*>(action))
        return 1.0f;

    if (!AI_VALUE(bool, "has available loot"))
    {
        lootAvailableSince = 0;
        return 1.0f;
    }

    // Fail open after a while: the loot stack drops a target 30s after it was added, but
    // AddAllLootAction re-adds nearby loot, so a target the bot cannot actually reach (bad path,
    // ledge, across water) would otherwise keep "has available loot" true forever. After the
    // patience window RPG actions run again and the bot walks away; the target stops qualifying
    // once the bot is beyond AiPlayerbot.LootDistance (15 yd).
    time_t const now = time(nullptr);
    if (!lootAvailableSince)
        lootAvailableSince = now;

    return now - lootAvailableSince >= time_t(sPlayerbotAIConfig.lootPriorityTimeout) ? 1.0f : 0.0f;
}
