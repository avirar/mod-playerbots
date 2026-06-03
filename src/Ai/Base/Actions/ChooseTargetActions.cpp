/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "ChooseTargetActions.h"

#include "ChooseRpgTargetAction.h"
#include "Creature.h"
#include "Event.h"
#include "LootMgr.h"
#include "LootObjectStack.h"
#include "NewRpgStrategy.h"
#include "ObjectMgr.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "RtiTargetValue.h"
#include "PossibleRpgTargetsValue.h"
#include "PvpTriggers.h"
#include "ServerFacade.h"

bool AttackEnemyPlayerAction::isUseful()
{
    if (PlayerHasFlag::IsCapturingFlag(bot))
        return false;

    return !sPlayerbotAIConfig.IsPvpProhibited(bot->GetZoneId(), bot->GetAreaId());
}

bool AttackEnemyFlagCarrierAction::isUseful()
{
    Unit* target = context->GetValue<Unit*>("enemy flag carrier")->Get();
    return target && ServerFacade::instance().IsDistanceLessOrEqualThan(ServerFacade::instance().GetDistance2d(bot, target), 100.0f) &&
           PlayerHasFlag::IsCapturingFlag(bot);
}

bool AggressiveTargetAction::isUseful()
{
    if (bot->IsInCombat())
        return false;

    return true;
}

bool DropTargetAction::Execute(Event /*event*/)
{
    Unit* target = context->GetValue<Unit*>("current target")->Get();
    if (target && target->isDead())
    {
        ObjectGuid guid = target->GetGUID();
        if (guid)
            context->GetValue<LootObjectStack*>("available loot")->Get()->Add(guid);
    }

    // ObjectGuid pullTarget = context->GetValue<ObjectGuid>("pull target")->Get();
    // GuidVector possible = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets no los")->Get();

    // if (pullTarget && find(possible.begin(), possible.end(), pullTarget) == possible.end())
    // {
    //     context->GetValue<ObjectGuid>("pull target")->Set(ObjectGuid::Empty);
    // }

    context->GetValue<Unit*>("current target")->Set(nullptr);

    bot->SetTarget(ObjectGuid::Empty);
    bot->SetSelection(ObjectGuid());
    botAI->ChangeEngine(BOT_STATE_NON_COMBAT);
    if (bot->getClass() == CLASS_HUNTER) // Check for Hunter Class
    {
        Spell const* spell = bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL); // Get the current spell being cast by the bot
        if (spell && spell->m_spellInfo->Id == 75) //Check spell is not nullptr before accessing m_spellInfo
            bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL); // Interrupt Auto Shot
    }
    bot->AttackStop();

    // if (Pet* pet = bot->GetPet())
    // {
    //     if (CreatureAI* creatureAI = ((Creature*)pet)->AI())
    //     {
    //         pet->SetReactState(REACT_PASSIVE);
    //         pet->GetCharmInfo()->SetCommandState(COMMAND_FOLLOW);
    //         pet->GetCharmInfo()->SetIsCommandFollow(true);
    //         pet->AttackStop();
    //         pet->GetCharmInfo()->IsReturning();
    //         pet->GetMotionMaster()->MoveFollow(bot, PET_FOLLOW_DIST, pet->GetFollowAngle());
    //     }
    // }

    return true;
}

bool AttackAnythingAction::Execute(Event event)
{
    bool result = AttackAction::Execute(event);
    if (result)
    {
        if (Unit* grindTarget = GetTarget())
        {
            if (char const* grindName = grindTarget->GetName().c_str())
            {
                context->GetValue<ObjectGuid>("pull target")->Set(grindTarget->GetGUID());
                bot->GetMotionMaster()->Clear();
                // bot->StopMoving();
            }
        }
    }

    return result;
}

bool AttackAnythingAction::isUseful()
{
    if (!bot || !botAI)  // Prevents invalid accesses
        return false;

    if (!botAI->AllowActivity(GRIND_ACTIVITY))  // Bot cannot be active
        return false;

    if (botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT))
        return false;

    if (bot->IsInCombat())
        return false;

 // Prevent attacking if loot is available
    if (AI_VALUE(bool, "has available loot"))
        return false;

    // NEW: Check if target would provide quest credit before attacking
    Unit* target = GetTarget();
    if (target && target->IsInWorld() && !WouldTargetProvideQuestCredit(target))
        return false;
    if (!target || !target->IsInWorld())  // Checks if the target is valid and in the world
        return false;

    std::string const name = std::string(target->GetName());
    if (!name.empty() &&
        (name.find("Dummy") != std::string::npos ||
         name.find("Charge Target") != std::string::npos ||
         name.find("Melee Target") != std::string::npos ||
         name.find("Ranged Target") != std::string::npos))
    {
        return false;
    }

    return true;
}

bool AttackAnythingAction::isPossible() { return GetTarget() && AttackAction::isPossible(); }

bool DpsAssistAction::isUseful()
{
    if (PlayerHasFlag::IsCapturingFlag(bot))
        return false;

    return true;
}

bool AttackRtiTargetAction::Execute(Event /*event*/)
{
    Unit* rtiTarget = AI_VALUE(Unit*, "rti target");

    // Fallback: if the "rti target" value did not resolve a valid unit yet,
    // try to resolve the raid icon directly from the group.
    if (!rtiTarget)
    {
        if (Group* group = bot->GetGroup())
        {
            std::string const rti = AI_VALUE(std::string, "rti");
            int32 const index = RtiTargetValue::GetRtiIndex(rti);
            if (index >= 0)
            {
                ObjectGuid const guid = group->GetTargetIcon(index);
                if (!guid.IsEmpty())
                    rtiTarget = botAI->GetUnit(guid);
            }
        }
    }

    if (rtiTarget && rtiTarget->IsInWorld() && rtiTarget->GetMapId() == bot->GetMapId())
    {
        botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Set({rtiTarget->GetGUID()});
        bool result = Attack(botAI->GetUnit(rtiTarget->GetGUID()));
        if (result)
        {
            context->GetValue<ObjectGuid>("pull target")->Set(rtiTarget->GetGUID());
            return true;
        }
    }
    else
        botAI->TellError("I dont see my rti attack target");

    return false;
}

bool AttackRtiTargetAction::isUseful()
{
    if (botAI->ContainsStrategy(STRATEGY_TYPE_HEAL))
        return false;

    return true;
}

bool AttackAnythingAction::WouldTargetProvideQuestCredit(Unit* target)
{
    if (!target || !bot)
        return true;

    uint32 targetEntry = target->GetEntry();

    for (uint32 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (questId == 0)
            continue;

        QuestStatus questStatus = bot->GetQuestStatus(questId);
        if (questStatus != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            uint32 requiredEntry = quest->RequiredNpcOrGo[i];
            if (requiredEntry == 0)
                continue;

            if (requiredEntry == targetEntry)
            {
                uint32 requiredCount = quest->RequiredNpcOrGoCount[i];
                uint32 currentCount = bot->GetQuestSlotCounter(slot, i);
                if (currentCount < requiredCount)
                    return true;
            }

            if (target->GetTypeId() == TYPEID_UNIT)
            {
                Creature* creature = target->ToCreature();
                if (creature)
                {
                    CreatureTemplate const* creatureTemplate = creature->GetCreatureTemplate();
                    if (creatureTemplate &&
                        (creatureTemplate->KillCredit[0] == requiredEntry || creatureTemplate->KillCredit[1] == requiredEntry))
                    {
                        uint32 requiredCount = quest->RequiredNpcOrGoCount[i];
                        uint32 currentCount = bot->GetQuestSlotCounter(slot, i);
                        if (currentCount < requiredCount)
                            return true;
                    }
                }
            }
        }

        if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_CAST))
        {
            for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            {
                uint32 requiredCount = quest->RequiredNpcOrGoCount[i];
                if (requiredCount == 0)
                    continue;

                uint32 currentCount = bot->GetQuestSlotCounter(slot, i);
                if (currentCount < requiredCount)
                    return true;
            }
        }
    }

    if (target->GetTypeId() == TYPEID_UNIT)
    {
        CreatureTemplate const* cdata = sObjectMgr->GetCreatureTemplate(target->GetEntry());
        if (cdata && cdata->lootid)
        {
            if (LootTemplates_Creature.HaveQuestLootForPlayer(cdata->lootid, bot))
            {
                return true;
            }
        }
    }

    bool hasIncompleteQuestObjectives = false;

    for (uint32 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (questId == 0)
            continue;

        QuestStatus questStatus = bot->GetQuestStatus(questId);
        if (questStatus != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            if (quest->RequiredNpcOrGo[i] == 0)
                continue;

            uint32 reqCount = quest->RequiredNpcOrGoCount[i];
            if (reqCount == 0)
                continue;

            uint32 currentCount = bot->GetQuestSlotCounter(slot, i);
            if (currentCount < reqCount)
            {
                hasIncompleteQuestObjectives = true;
                break;
            }
        }

        if (hasIncompleteQuestObjectives)
            break;
    }

    if (hasIncompleteQuestObjectives)
        return false;

    return true;
}
