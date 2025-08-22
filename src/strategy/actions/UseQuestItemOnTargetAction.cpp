/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "UseQuestItemOnTargetAction.h"

#include "ConditionMgr.h"
#include "Event.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "Unit.h"
#include "ChatHelper.h"

// Maximum distance to search for quest targets
constexpr float QUEST_TARGET_SEARCH_RANGE = 30.0f;

bool UseQuestItemOnTargetAction::Execute(Event event)
{
    uint32 spellId = 0;
    
    // Find the best quest item to use
    Item* questItem = FindBestQuestItem(&spellId);
    if (!questItem)
    {
        botAI->TellError("No usable quest items found");
        return false;
    }

    // Find the best target for this quest item
    Unit* target = FindBestTargetForQuestItem(spellId);
    if (!target)
    {
        botAI->TellError("No valid targets found for quest item");
        return false;
    }

    // Check if we're in range of the target
    float range = botAI->GetRange("spell");
    if (bot->GetDistance(target) > range)
    {
        std::ostringstream out;
        out << "Target " << target->GetName() << " is too far away for quest item";
        botAI->TellMaster(out.str());
        return false;
    }

    // Use the quest item on the target
    return UseQuestItemOnTarget(questItem, target);
}

bool UseQuestItemOnTargetAction::isUseful()
{
    uint32 spellId = 0;
    
    // Check if we have any usable quest items
    Item* questItem = FindBestQuestItem(&spellId);
    if (!questItem)
        return false;

    // Check if there are valid targets available
    Unit* target = FindBestTargetForQuestItem(spellId);
    return target != nullptr;
}

Item* UseQuestItemOnTargetAction::FindBestQuestItem(uint32* outSpellId) const
{
    Item* bestItem = nullptr;
    uint32 bestSpellId = 0;

    // Search through all inventory slots for quest items with spells
    for (uint8 bag = INVENTORY_SLOT_ITEM_START; bag < INVENTORY_SLOT_ITEM_END; ++bag)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (!item)
            continue;

        uint32 spellId = 0;
        if (IsValidQuestItem(item, &spellId))
        {
            // For now, return the first valid quest item found
            // Could be enhanced to prioritize based on quest urgency, etc.
            if (outSpellId)
                *outSpellId = spellId;
            return item;
        }
    }

    return nullptr;
}

Unit* UseQuestItemOnTargetAction::FindBestTargetForQuestItem(uint32 spellId) const
{
    Unit* bestTarget = nullptr;
    float closestDistance = QUEST_TARGET_SEARCH_RANGE;

    // Get nearby units that could be quest targets
    GuidVector targets = AI_VALUE(GuidVector, "possible targets");
    
    for (ObjectGuid guid : targets)
    {
        Unit* target = botAI->GetUnit(guid);
        if (!target)
            continue;

        // Check if this target is valid for our quest item spell
        if (!IsTargetValidForSpell(target, spellId))
            continue;

        // Prefer closer targets
        float distance = bot->GetDistance(target);
        if (distance < closestDistance)
        {
            closestDistance = distance;
            bestTarget = target;
        }
    }

    // Also check nearby NPCs specifically
    if (!bestTarget)
    {
        GuidVector npcs = AI_VALUE(GuidVector, "nearest npcs");
        
        for (ObjectGuid guid : npcs)
        {
            Unit* target = botAI->GetUnit(guid);
            if (!target)
                continue;

            if (!IsTargetValidForSpell(target, spellId))
                continue;

            float distance = bot->GetDistance(target);
            if (distance < closestDistance)
            {
                closestDistance = distance;
                bestTarget = target;
            }
        }
    }

    return bestTarget;
}

bool UseQuestItemOnTargetAction::IsValidQuestItem(Item* item, uint32* outSpellId) const
{
    if (!item)
        return false;

    const ItemTemplate* itemTemplate = item->GetTemplate();
    if (!itemTemplate)
        return false;

    // Check if this item has the player-castable flag
    if (!(itemTemplate->Flags & ITEM_FLAG_PLAYERCAST))
        return false;

    // Check if the item has an associated spell that we can cast
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        uint32 spellId = itemTemplate->Spells[i].SpellId;
        if (spellId > 0)
        {
            // Verify we can cast this spell
            if (botAI->CanCastSpell(spellId, bot, false))
            {
                if (outSpellId)
                    *outSpellId = spellId;
                return true;
            }
        }
    }

    return false;
}

bool UseQuestItemOnTargetAction::IsTargetValidForSpell(Unit* target, uint32 spellId) const
{
    if (!target || !target->IsAlive())
        return false;

    // Check basic spell targeting requirements
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return false;

    // Check if target is hostile and we shouldn't target hostile units
    if (target->IsHostileTo(bot) && !spellInfo->IsPositive())
    {
        // Allow targeting hostile units if the spell is meant for them
        // This covers cases like quest items that need to be used on hostile NPCs
    }

    // Check spell-specific conditions (aura requirements, creature type, etc.)
    return CheckSpellConditions(spellId, target);
}

bool UseQuestItemOnTargetAction::CheckSpellConditions(uint32 spellId, Unit* target) const
{
    // Query conditions table for this spell to find required target conditions
    ConditionList conditions = sConditionMgr->GetConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_SPELL, 0, spellId);
    
    // If no conditions are found, assume the target is valid
    if (conditions.empty())
        return true;

    // Check each condition
    for (Condition const* condition : conditions)
    {
        bool conditionMet = false;

        switch (condition->ConditionType)
        {
            case CONDITION_AURA:
            {
                uint32 requiredAuraId = condition->ConditionValue1;
                bool hasAura = target->HasAura(requiredAuraId);
                conditionMet = condition->NegativeCondition ? !hasAura : hasAura;
                break;
            }
            case CONDITION_CREATURE_TYPE:
            {
                if (target->GetTypeId() == TYPEID_UNIT)
                {
                    uint32 requiredCreatureType = condition->ConditionValue1;
                    conditionMet = target->ToCreature()->GetCreatureTemplate()->type == requiredCreatureType;
                }
                break;
            }
            case CONDITION_OBJECT_ENTRY_GUID:
            {
                uint32 requiredEntry = condition->ConditionValue1;
                conditionMet = target->GetEntry() == requiredEntry;
                break;
            }
            default:
                // For unknown condition types, assume they're met
                conditionMet = true;
                break;
        }

        // If any condition is not met, the target is invalid
        if (!conditionMet)
            return false;
    }

    // All conditions were met
    return true;
}

bool UseQuestItemOnTargetAction::UseQuestItemOnTarget(Item* item, Unit* target) const
{
    if (!item || !target)
        return false;

    // Use the item on the target using the UseItem method from UseItemAction
    bool result = UseItem(item, ObjectGuid::Empty, nullptr, target);
    
    if (result)
    {
        std::ostringstream out;
        out << "Using " << chat->FormatItem(item->GetTemplate()) << " on " << target->GetName();
        botAI->TellMasterNoFacing(out.str());
    }
    else
    {
        std::ostringstream out;
        out << "Failed to use " << chat->FormatItem(item->GetTemplate()) << " on " << target->GetName();
        botAI->TellError(out.str());
    }

    return result;
}