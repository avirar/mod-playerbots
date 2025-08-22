/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "MoveToQuestItemTargetAction.h"

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
constexpr float QUEST_TARGET_SEARCH_RANGE = 50.0f;

bool MoveToQuestItemTargetAction::Execute(Event event)
{
    uint32 spellId = 0;
    
    // Find the best quest item that needs a target
    Item* questItem = FindBestQuestItem(&spellId);
    if (!questItem)
    {
        return false;
    }

    // Find the best target for this quest item
    Unit* target = FindBestTargetForQuestItem(spellId);
    if (!target)
    {
        return false;
    }

    // Check if we're already in range
    float range = botAI->GetRange("spell");
    if (bot->GetDistance(target) <= range)
    {
        // We're already in range, no need to move
        return false;
    }

    // Move towards the target
    float distance = bot->GetDistance(target);
    
    std::ostringstream out;
    out << "Moving to quest target " << target->GetName() << " (distance: " << distance << ")";
    botAI->TellMasterNoFacing(out.str());

    // Use the MovementAction's move functionality
    return MoveTo(target->GetMapId(), target->GetPosition().GetPositionX(), 
                  target->GetPosition().GetPositionY(), target->GetPosition().GetPositionZ());
}

bool MoveToQuestItemTargetAction::isUseful()
{
    uint32 spellId = 0;
    
    // Check if we have any usable quest items
    Item* questItem = FindBestQuestItem(&spellId);
    if (!questItem)
        return false;

    // Find the best target for this quest item
    Unit* target = FindBestTargetForQuestItem(spellId);
    if (!target)
        return false;

    // Check if we need to move (are we out of range?)
    float range = botAI->GetRange("spell");
    return bot->GetDistance(target) > range;
}

Item* MoveToQuestItemTargetAction::FindBestQuestItem(uint32* outSpellId) const
{
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

Unit* MoveToQuestItemTargetAction::FindBestTargetForQuestItem(uint32 spellId) const
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

    // Also check nearby NPCs specifically (they might not be in possible targets)
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

bool MoveToQuestItemTargetAction::IsValidQuestItem(Item* item, uint32* outSpellId) const
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

bool MoveToQuestItemTargetAction::IsTargetValidForSpell(Unit* target, uint32 spellId) const
{
    if (!target || !target->IsAlive())
        return false;

    // Check basic spell targeting requirements
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return false;

    // Check spell-specific conditions (aura requirements, creature type, etc.)
    return CheckSpellConditions(spellId, target);
}

bool MoveToQuestItemTargetAction::CheckSpellConditions(uint32 spellId, Unit* target) const
{
    // Query conditions table for this spell to find required target conditions
    ConditionList conditions = sConditionMgr->GetConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_SPELL, spellId);
    
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
