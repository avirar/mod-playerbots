/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "QuestSpellHelper.h"

#include "Creature.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "QuestDef.h"
#include "QuestItemHelper.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "AiObjectContext.h"

#include <sstream>
#include <map>
#include <ctime>

// Cooldown tracking for quest spell usage (GUID -> spell ID -> timestamp)
static std::map<ObjectGuid, std::map<uint32, time_t>> questSpellUsageTracker;

std::vector<Quest const*> QuestSpellHelper::FindQuestsRequiringSpellCast(Player* player)
{
    std::vector<Quest const*> result;

    if (!player)
        return result;

    // Iterate through active quests
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = player->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        // Check if quest is active and incomplete
        QuestStatus status = player->GetQuestStatus(questId);
        if (status != QUEST_STATUS_INCOMPLETE)
            continue;

        // Check if quest requires spell cast on creatures
        if (QuestRequiresSpellCast(quest))
        {
            result.push_back(quest);
        }
    }

    return result;
}

bool QuestSpellHelper::QuestRequiresSpellCast(Quest const* quest)
{
    if (!quest)
        return false;

    // Check for RequiredNpcOrGo objectives (creatures to interact with)
    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        int32 entry = quest->RequiredNpcOrGo[i];
        uint32 count = quest->RequiredNpcOrGoCount[i];

        // Positive entry = creature, count > 0 = need to interact
        if (entry > 0 && count > 0)
        {
            // Check if this is a creature that requires spell cast
            // (e.g., Draenei Survivor for Gift of the Naaru)
            CreatureTemplate const* creatureTemplate = sObjectMgr->GetCreatureTemplate(entry);
            if (creatureTemplate)
            {
                // For now, we'll assume any creature objective with a ScriptName
                // might require spell casting. We'll validate with actual spell later.
                return true;
            }
        }
    }

    return false;
}

Unit* QuestSpellHelper::FindTargetForQuestSpell(PlayerbotAI* botAI, Quest const* quest, uint32* outSpellId)
{
    if (!botAI || !quest)
        return nullptr;

    Player* bot = botAI->GetBot();
    if (!bot)
        return nullptr;

    // Find the quest objective (creature entry)
    uint32 targetEntry = 0;

    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        int32 entry = quest->RequiredNpcOrGo[i];
        uint32 count = quest->RequiredNpcOrGoCount[i];

        if (entry > 0 && count > 0)
        {
            targetEntry = entry;
            break;
        }
    }

    if (!targetEntry)
        return nullptr;

    // Determine which spell to use
    uint32 spellId = GetSpellForQuestObjective(bot, quest, targetEntry);
    if (!spellId)
    {
        if (botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestSpell: No spell found for quest " << quest->GetQuestId() << " (creature " << targetEntry << ")";
            botAI->TellMaster(out.str());
        }
        return nullptr;
    }

    if (outSpellId)
        *outSpellId = spellId;

    // Get spell range
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    float maxRange = spellInfo ? spellInfo->GetMaxRange() : sPlayerbotAIConfig->grindDistance;
    float searchRange = std::min(maxRange, sPlayerbotAIConfig->grindDistance);

    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestSpell: FindTarget - spell " << spellId << ", range " << searchRange << ", target entry " << targetEntry;
        botAI->TellMaster(out.str());
    }

    // Find nearby NPCs (not just hostile targets, since quest targets are often friendly)
    // Use "nearest npcs" instead of "possible targets" to include friendly creatures
    GuidVector targets = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();

    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestSpell: Found " << targets.size() << " nearest npcs";
        botAI->TellMaster(out.str());
    }

    Unit* bestTarget = nullptr;
    float closestDistance = searchRange;

    int checkedCount = 0;
    int creatureCount = 0;
    int matchingEntryCount = 0;

    for (ObjectGuid guid : targets)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        checkedCount++;

        Creature* creature = unit->ToCreature();
        if (!creature)
            continue;

        creatureCount++;

        // Check if creature matches the quest requirement
        if (creature->GetEntry() != targetEntry)
            continue;

        matchingEntryCount++;

        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestSpell: Found matching creature " << creature->GetName()
                << " (entry " << creature->GetEntry() << ") at distance " << bot->GetDistance(unit);
            botAI->TellMaster(out.str());
        }

        // Check distance
        float distance = bot->GetDistance(unit);
        if (distance > closestDistance)
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestSpell: Target too far (" << distance << " > " << closestDistance << ")";
                botAI->TellMaster(out.str());
            }
            continue;
        }

        // Check if target is alive
        if (!creature->IsAlive())
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                botAI->TellMaster("QuestSpell: Target is not alive");
            }
            continue;
        }

        // Check if we can use spell on this target (cooldown check)
        if (!CanUseQuestSpellOnTarget(botAI, unit, spellId))
            continue;

        // Check quest progress - don't target if already complete
        QuestStatusData const* questStatus = &bot->getQuestStatusMap().at(quest->GetQuestId());
        if (questStatus)
        {
            for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            {
                if (quest->RequiredNpcOrGo[i] == (int32)targetEntry)
                {
                    if (questStatus->CreatureOrGOCount[i] >= quest->RequiredNpcOrGoCount[i])
                    {
                        if (botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                        {
                            std::ostringstream out;
                            out << "QuestSpell: Quest objective already complete ("
                                << questStatus->CreatureOrGOCount[i] << "/" << quest->RequiredNpcOrGoCount[i] << ")";
                            botAI->TellMaster(out.str());
                        }
                        return nullptr; // Objective complete, don't use spell
                    }
                    break;
                }
            }
        }

        bestTarget = unit;
        closestDistance = distance;
    }

    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestSpell: Checked " << checkedCount << " units, " << creatureCount << " creatures, "
            << matchingEntryCount << " matching entry " << targetEntry;
        botAI->TellMaster(out.str());
    }

    if (bestTarget && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestSpell: Found target " << bestTarget->GetName() << " (entry " << targetEntry
            << ") at distance " << closestDistance << " for spell " << spellId;
        botAI->TellMaster(out.str());
    }

    return bestTarget;
}

uint32 QuestSpellHelper::GetSpellForQuestObjective(Player* player, Quest const* quest, uint32 creatureEntry)
{
    if (!player || !quest)
        return 0;

    // Special handling for known quest-spell combinations
    // Quest 9283: Rescue the Survivors! - requires Gift of the Naaru on Draenei Survivor (16483)
    if (quest->GetQuestId() == 9283 && creatureEntry == 16483)
    {
        return FindRacialSpellForQuest(player, quest->GetQuestId());
    }

    // Future: Add more quest-specific spell mappings here
    // or implement a database-driven approach

    return 0;
}

uint32 QuestSpellHelper::FindRacialSpellForQuest(Player* player, uint32 questId)
{
    if (!player)
        return 0;

    // Quest 9283: Gift of the Naaru
    if (questId == 9283)
    {
        // Check for Gift of the Naaru (various ranks)
        uint32 giftSpells[] = { 28880, 59542, 59543, 59544, 59545, 59547, 59548 };

        for (uint32 spellId : giftSpells)
        {
            if (player->HasSpell(spellId))
                return spellId;
        }
    }

    return 0;
}

bool QuestSpellHelper::CanUseQuestSpellOnTarget(PlayerbotAI* botAI, WorldObject* target, uint32 spellId)
{
    if (!botAI || !target)
        return false;

    // Check if we've used this spell on this target recently (15 second cooldown)
    ObjectGuid targetGuid = target->GetGUID();

    auto targetIter = questSpellUsageTracker.find(targetGuid);
    if (targetIter != questSpellUsageTracker.end())
    {
        auto spellIter = targetIter->second.find(spellId);
        if (spellIter != targetIter->second.end())
        {
            time_t currentTime = time(nullptr);
            time_t lastUsed = spellIter->second;

            // 15 second cooldown per target
            if (currentTime - lastUsed < 15)
            {
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestSpell: Target " << target->GetName() << " used recently for spell " << spellId
                        << " (" << (currentTime - lastUsed) << "s ago)";
                    botAI->TellMaster(out.str());
                }
                return false;
            }
        }
    }

    return true;
}

void QuestSpellHelper::RecordQuestSpellUsage(PlayerbotAI* botAI, WorldObject* target, uint32 spellId)
{
    if (!botAI || !target)
        return;

    // Record the usage with current timestamp
    ObjectGuid targetGuid = target->GetGUID();
    questSpellUsageTracker[targetGuid][spellId] = time(nullptr);

    if (botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestSpell: Recorded usage of spell " << spellId << " on " << target->GetName();
        botAI->TellMaster(out.str());
    }
}
