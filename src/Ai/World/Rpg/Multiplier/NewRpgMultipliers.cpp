/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgMultipliers.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

float NewRpgQuestGiverMultiplier::GetValue(Action* action)
{
    if (!sPlayerbotAIConfig.PrioritiseQuestGivers || !action)
        return 1.0f;

    // A giver with something to accept or reward is handled first; hold back grinding and loot
    // pickup/chase so the interaction is not starved by nearby resources. The giver search
    // (QuestGiverSearchDistance) already only counts givers with a real accept/reward.
    std::string const name = action->getName();
    if (name != "attack anything" && name != "move to loot" && name != "loot" && name != "open loot")
        return 1.0f;

    return AI_VALUE(bool, "quest giver nearby") ? 0.0f : 1.0f;
}

float NewRpgEscortMultiplier::GetValue(Action* action)
{
    if (!sPlayerbotAIConfig.QuestEscorts || !action)
        return 1.0f;

    std::string const name = action->getName();
    if (name == "attack anything")
    {
        // Defending the escort outranks the leash: attacks stay available while it is attacked.
        if (!AI_VALUE(bool, "escort npc nearby") || AI_VALUE(bool, "escort npc attacked"))
            return 1.0f;

        return 0.0f;
    }

    // A bot that chases loot off the leash risks the whole escort.
    if (name == "move to loot" || name == "loot" || name == "open loot")
        return AI_VALUE(bool, "escort npc nearby") ? 0.0f : 1.0f;

    return 1.0f;
}
