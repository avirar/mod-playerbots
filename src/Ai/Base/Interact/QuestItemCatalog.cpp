/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "QuestItemCatalog.h"

#include "ConditionMgr.h"
#include "ItemTemplate.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace
{
// The item's on-use spell (trigger 0). Negative ids are charge bookkeeping, not spells.
uint32 GetUseSpell(ItemTemplate const* proto)
{
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
            continue;

        if (proto->Spells[i].SpellId > 0)
            return uint32(proto->Spells[i].SpellId);
    }

    return 0;
}

// The entry named by the spell's implicit-target conditions (the primary target oracle for
// use-on-X quest items). Returns 0 when the spell has no entry-typed condition.
uint32 GetConditionTargetEntry(SpellInfo const* spellInfo, bool& isGameObject)
{
    isGameObject = false;

    ConditionList const conditions =
        sConditionMgr->GetConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_SPELL_IMPLICIT_TARGET, spellInfo->Id);

    for (Condition* condition : conditions)
    {
        if (!condition || condition->ConditionType != CONDITION_OBJECT_ENTRY_GUID || !condition->ConditionValue1)
            continue;

        isGameObject = (condition->ConditionValue2 & TYPEMASK_GAMEOBJECT) != 0;
        return condition->ConditionValue1;
    }

    return 0;
}

bool HasOpenLockEffect(SpellInfo const* spellInfo)
{
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        if (spellInfo->Effects[i].Effect == SPELL_EFFECT_OPEN_LOCK)
            return true;

    return false;
}

// Classify the spell into the interaction kind it needs. Order matters: explicit object kinds
// first, then unit/condition kinds, then quest-derived fallbacks, POI last.
InteractKind ClassifySpell(SpellInfo const* spellInfo, Quest const* quest, uint8 objectiveIdx, uint32& targetEntry)
{
    targetEntry = 0;

    bool conditionIsGameObject = false;
    uint32 const conditionEntry = GetConditionTargetEntry(spellInfo, conditionIsGameObject);
    uint32 const questEntry = quest->RequiredNpcOrGo[objectiveIdx];

    if (HasOpenLockEffect(spellInfo) || (spellInfo->Targets & TARGET_FLAG_GAMEOBJECT))
    {
        targetEntry = conditionEntry ? conditionEntry : (questEntry < 0 ? uint32(-questEntry) : 0);
        return InteractKind::UseOnGameObject;
    }

    if (conditionEntry && !conditionIsGameObject)
    {
        targetEntry = conditionEntry;
        return InteractKind::UseOnCreature;
    }

    if (conditionEntry && conditionIsGameObject)
    {
        targetEntry = conditionEntry;
        return InteractKind::UseOnGameObject;
    }

    if (spellInfo->Targets & TARGET_FLAG_UNIT)
    {
        targetEntry = questEntry > 0 ? uint32(questEntry) : 0;
        return InteractKind::UseOnCreature;
    }

    if (spellInfo->Targets & TARGET_FLAG_DEST_LOCATION)
        return sObjectMgr->GetQuestPOIVector(quest->GetQuestId()) ? InteractKind::UseAtLocation : InteractKind::SelfUse;

    if (questEntry > 0)
    {
        targetEntry = uint32(questEntry);
        return InteractKind::UseOnCreature;
    }

    if (questEntry < 0)
    {
        targetEntry = uint32(-questEntry);
        return InteractKind::UseOnGameObject;
    }

    return sObjectMgr->GetQuestPOIVector(quest->GetQuestId()) ? InteractKind::UseAtLocation : InteractKind::SelfUse;
}
} // namespace

void QuestItemCatalog::Build(Player* bot, std::vector<QuestItemCandidate>& out)
{
    out.clear();

    if (!bot)
        return;

    QuestStatusMap const& statusMap = bot->getQuestStatusMap();

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        auto statusItr = statusMap.find(questId);
        if (statusItr == statusMap.end())
            continue;

        QuestStatusData const& status = statusItr->second;
        if (status.Status != QUEST_STATUS_INCOMPLETE)
            continue; // completed objectives are the turn-in flow's job

        for (uint8 idx = 0; idx < QUEST_ITEM_OBJECTIVES_COUNT; ++idx)
        {
            uint32 const itemId = quest->RequiredItemId[idx];
            uint32 const required = quest->RequiredItemCount[idx];
            if (!itemId || !required)
                continue;

            if (status.ItemCount[idx] >= required)
                continue; // objective already satisfied

            if (!bot->GetItemCount(itemId))
                continue; // nothing usable in the bags yet

            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto)
                continue;

            uint32 const spellId = GetUseSpell(proto);
            if (!spellId)
                continue;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!spellInfo || spellInfo->IsPassive())
                continue;

            QuestItemCandidate candidate;
            candidate.questId = questId;
            candidate.objectiveIdx = idx;
            candidate.itemId = itemId;
            candidate.spellId = spellId;
            candidate.kind = ClassifySpell(spellInfo, quest, idx, candidate.targetEntry);
            candidate.requiredCount = required;
            candidate.currentCount = status.ItemCount[idx];
            out.push_back(candidate);
        }
    }
}
