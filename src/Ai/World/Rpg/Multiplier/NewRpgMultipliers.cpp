/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgMultipliers.h"
#include "Creature.h"
#include "LootObjectStack.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

namespace
{
// A corpse the bot is already standing on: looting it needs no movement. GOs are excluded.
bool IsCorpseAtFeet(PlayerbotAI* botAI, Player* bot, LootObject loot)
{
    if (loot.IsEmpty() || loot.guid.IsGameObject())
        return false;

    Creature* corpse = botAI->GetCreature(loot.guid);
    return corpse && corpse->getDeathState() == DeathState::Corpse && loot.IsAtInteractDistance(bot);
}

LootObject GetLootCandidate(PlayerbotAI* botAI, std::string const& actionName)
{
    AiObjectContext* context = botAI->GetAiObjectContext();
    return actionName == "loot" ? context->GetValue<LootObject>("nearest loot")->Get()
                                : context->GetValue<LootObject>("loot target")->Get();
}
}  // namespace

float NewRpgQuestGiverMultiplier::GetValue(Action* action)
{
    if (!sPlayerbotAIConfig.PrioritiseQuestGivers || !action)
        return 1.0f;

    std::string const name = action->getName();
    if (name != "attack anything" && name != "move to loot" && name != "loot" && name != "open loot")
        return 1.0f;

    if (!AI_VALUE(bool, "quest giver nearby"))
        return 1.0f;

    // No chasing loot while a giver waits; corpses at the bot's feet are still looted in place.
    if (name == "attack anything" || name == "move to loot")
        return 0.0f;

    return IsCorpseAtFeet(botAI, bot, GetLootCandidate(botAI, name)) ? 1.0f : 0.0f;
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

    if (name != "move to loot" && name != "loot" && name != "open loot")
        return 1.0f;

    if (!AI_VALUE(bool, "escort npc nearby"))
        return 1.0f;

    // A chase risks the leash; loot already at the bot's feet is safe.
    if (name == "move to loot")
        return 0.0f;

    return IsCorpseAtFeet(botAI, bot, GetLootCandidate(botAI, name)) ? 1.0f : 0.0f;
}
