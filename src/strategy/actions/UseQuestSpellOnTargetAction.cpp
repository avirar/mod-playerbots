/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "UseQuestSpellOnTargetAction.h"

#include "ChatHelper.h"
#include "Event.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "QuestSpellHelper.h"
#include "SpellInfo.h"
#include "Unit.h"

#include <sstream>

bool UseQuestSpellOnTargetAction::Execute(Event event)
{
    // Find active quests requiring spell casts
    std::vector<Quest const*> quests = QuestSpellHelper::FindQuestsRequiringSpellCast(bot);

    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestSpell: Execute() called, found " << quests.size() << " quests";
        botAI->TellMaster(out.str());
    }

    if (quests.empty())
        return false;

    // Try each quest to find a valid target and spell
    for (Quest const* quest : quests)
    {
        uint32 spellId = 0;
        Unit* target = QuestSpellHelper::FindTargetForQuestSpell(botAI, quest, &spellId);

        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestSpell: Quest " << quest->GetQuestId() << " - target: "
                << (target ? target->GetName() : "nullptr") << ", spellId: " << spellId;
            botAI->TellMaster(out.str());
        }

        if (!target || !spellId)
            continue;

        // Check if we're in range
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        float range = spellInfo ? (spellInfo->GetMaxRange() - 2.0f) : (INTERACTION_DISTANCE - 2.0f);

        if (range <= 0.0f)
            range = 0.5f;

        float distance = bot->GetDistance(target);

        if (distance > range)
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestSpell: Target " << target->GetName() << " out of range (" << distance << " > " << range << ")";
                botAI->TellMaster(out.str());
            }
            continue;
        }

        // Cast the spell
        if (CastQuestSpellOnTarget(spellId, target))
        {
            // Record usage to prevent spam
            QuestSpellHelper::RecordQuestSpellUsage(botAI, target, spellId);
            return true;
        }
    }

    return false;
}

bool UseQuestSpellOnTargetAction::isUseful()
{
    // Check if we have any quests requiring spell casts
    std::vector<Quest const*> quests = QuestSpellHelper::FindQuestsRequiringSpellCast(bot);

    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestSpell: isUseful() checking - found " << quests.size() << " quests requiring spell cast";
        botAI->TellMaster(out.str());
    }

    if (quests.empty())
        return false;

    // Check if there are valid targets available
    for (Quest const* quest : quests)
    {
        uint32 spellId = 0;
        Unit* target = QuestSpellHelper::FindTargetForQuestSpell(botAI, quest, &spellId);

        if (target && spellId)
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestSpell: isUseful() TRUE - found target " << target->GetName() << " for spell " << spellId;
                botAI->TellMaster(out.str());
            }
            return true;
        }
    }

    return false;
}

bool UseQuestSpellOnTargetAction::isPossible()
{
    return true;
}

bool UseQuestSpellOnTargetAction::CastQuestSpellOnTarget(uint32 spellId, Unit* target)
{
    if (!spellId || !target)
        return false;

    // Clear movement states
    bot->ClearUnitState(UNIT_STATE_CHASE);
    bot->ClearUnitState(UNIT_STATE_FOLLOW);

    if (bot->isMoving())
    {
        bot->StopMoving();
        botAI->SetNextCheckDelay(sPlayerbotAIConfig->globalCoolDown);
        return false;
    }

    // Check if spell requires facing the target
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (spellInfo && spellInfo->FacingCasterFlags & 0x1) // SPELL_FACING_FLAG_INFRONT
    {
        if (!bot->HasInArc(static_cast<float>(M_PI), target))
        {
            // Face the target before casting
            bot->SetFacingToObject(target);
            botAI->SetNextCheckDelay(sPlayerbotAIConfig->globalCoolDown);

            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestSpell: Facing target " << target->GetName() << " before casting";
                botAI->TellMaster(out.str());
            }

            return false;
        }
    }

    // Cast the spell
    bool result = botAI->CastSpell(spellId, target);

    if (result)
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestSpell: Cast spell " << spellId << " on " << target->GetName();
            botAI->TellMaster(out.str());
        }
    }
    else
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestSpell: Failed to cast spell " << spellId << " on " << target->GetName();
            botAI->TellMaster(out.str());
        }
    }

    return result;
}
