/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "QuestItemTriggers.h"

#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "Unit.h"
#include "ConditionMgr.h"

// Maximum distance to consider a quest target as "nearby"
constexpr float QUEST_ITEM_TARGET_RANGE = 75.0f;

bool QuestItemUsableTrigger::IsActive()
{
    Item* questItem = nullptr;
    uint32 spellId = 0;
    
    // Check if we have a quest item with a spell
    if (!HasQuestItemWithSpell(&questItem, &spellId))
    {
        return false;
    }

    // Check if there are valid targets for this quest item
    bool hasValidTarget = HasValidTargetForQuestItem(spellId);
    
    
    return hasValidTarget;
}

bool QuestItemUsableTrigger::HasQuestItemWithSpell(Item** outItem, uint32* outSpellId) const
{
    
    // Check all inventory slots including bags
    // First check main inventory
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;

        const ItemTemplate* itemTemplate = item->GetTemplate();
        if (!itemTemplate)
            continue;

        // Only consider quest items (class 12) or consumable items (class 0)
        if (itemTemplate->Class != ITEM_CLASS_QUEST && itemTemplate->Class != ITEM_CLASS_CONSUMABLE)
            continue;


        // Check if this is a quest item with player-castable spells
        if (!(itemTemplate->Flags & ITEM_FLAG_PLAYERCAST))
            continue;


        // Check if the item has an associated spell
        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            uint32 spellId = itemTemplate->Spells[i].SpellId;
            if (spellId > 0)
            {
                
                // For quest items, we don't use CanCastSpell as it's too restrictive
                // Quest items should work based on quest logic, not normal spell rules
                if (outItem)
                    *outItem = item;
                if (outSpellId)
                    *outSpellId = spellId;
                return true;
            }
        }
    }

    // Also check items in bags
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (!pBag)
            continue;

        for (uint32 slot = 0; slot < pBag->GetBagSize(); ++slot)
        {
            Item* item = pBag->GetItemByPos(slot);
            if (!item)
                continue;

            const ItemTemplate* itemTemplate = item->GetTemplate();
            if (!itemTemplate)
                continue;

            // Only consider quest items (class 12) or consumable items (class 0)
            if (itemTemplate->Class != ITEM_CLASS_QUEST && itemTemplate->Class != ITEM_CLASS_CONSUMABLE)
                continue;


            // Check if this is a quest item with player-castable spells
            if (!(itemTemplate->Flags & ITEM_FLAG_PLAYERCAST))
                continue;


            // Check if the item has an associated spell
            for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                uint32 spellId = itemTemplate->Spells[i].SpellId;
                if (spellId > 0)
                {
                    
                    // For quest items, we don't use CanCastSpell as it's too restrictive
                    // Quest items should work based on quest logic, not normal spell rules
                    if (outItem)
                        *outItem = item;
                    if (outSpellId)
                        *outSpellId = spellId;
                    return true;
                }
            }
        }
    }

    return false;
}

bool QuestItemUsableTrigger::HasValidTargetForQuestItem(uint32 spellId) const
{

    // Get nearby units that could be quest targets
    GuidVector targets = AI_VALUE(GuidVector, "possible targets");
    
    
    for (ObjectGuid guid : targets)
    {
        Unit* target = botAI->GetUnit(guid);
        if (!target)
            continue;


        // Check if this target is valid for our quest item spell
        if (IsTargetValidForSpell(target, spellId))
        {
            return true;
        }
    }

    // Also check nearby NPCs specifically
    GuidVector npcs = AI_VALUE(GuidVector, "nearest npcs");
    
    
    for (ObjectGuid guid : npcs)
    {
        Unit* target = botAI->GetUnit(guid);
        if (!target)
            continue;


        // Check if this target is valid for our quest item spell
        if (IsTargetValidForSpell(target, spellId))
        {
            return true;
        }
    }

    return false;
}

bool QuestItemUsableTrigger::IsTargetValidForSpell(Unit* target, uint32 spellId) const
{
    if (!target || !target->IsAlive())
    {
        return false;
    }


    // Check if target is in range
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
    {
        return false;
    }

    // Use the spell's actual range - 2.0f or INTERACTION_DISTANCE - 2.0f
    float range = spellInfo->GetMaxRange() - 2.0f;
    
    // Ensure minimum distance is INTERACTION_DISTANCE - 2.0f for quest item interactions
    if (range <= 0.0f || range < (INTERACTION_DISTANCE - 2.0f))
        range = INTERACTION_DISTANCE - 2.0f;
        
    float distance = bot->GetDistance(target);
    
    
    if (distance > range)
    {
        return false;
    }

    
    // Check spell-specific conditions (aura requirements, creature type, etc.)
    bool conditionsValid = CheckSpellConditions(spellId, target);
    
    
    return conditionsValid;
}

bool QuestItemUsableTrigger::CheckSpellConditions(uint32 spellId, Unit* target) const
{

    // Query conditions table for this spell to find required target conditions
    ConditionList conditions = sConditionMgr->GetConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_SPELL, spellId);
    
    
    if (conditions.empty())
    {
        return true;
    }
    
    for (Condition const* condition : conditions)
    {

        // Check for aura conditions (most common for quest items)
        if (condition->ConditionType == CONDITION_AURA)
        {
            uint32 requiredAuraId = condition->ConditionValue1;
            
            
            // Check if target has the required aura
            bool hasAura = target->HasAura(requiredAuraId);
            
            
            // Apply condition logic (negated or normal)
            if (condition->NegativeCondition)
            {
                return !hasAura;
            }
            else
            {
                return hasAura;
            }
        }
        // Add other condition types as needed
        else if (condition->ConditionType == CONDITION_CREATURE_TYPE)
        {
            if (target->GetTypeId() != TYPEID_UNIT)
            {
                return false;
            }
                
            uint32 requiredCreatureType = condition->ConditionValue1;
            uint32 targetCreatureType = target->ToCreature()->GetCreatureTemplate()->type;
            
            
            return targetCreatureType == requiredCreatureType;
        }
        else
        {
        }
    }

    // If no specific conditions found, assume it's valid (fallback)
    return true;
}

bool FarFromQuestItemTargetTrigger::IsActive()
{
    Item* questItem = nullptr;
    uint32 spellId = 0;
    
    // Check if we have a quest item with a spell
    if (!HasQuestItemWithSpell(&questItem, &spellId))
        return false;

    // Find the best available target
    Unit* target = FindBestQuestItemTarget();
    if (!target)
        return false;

    // Check if we're too far from the target (use spell's actual range - 2.0f or INTERACTION_DISTANCE - 2.0f)
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    float range = spellInfo ? (spellInfo->GetMaxRange() - 2.0f) : (INTERACTION_DISTANCE - 2.0f);
    
    // Ensure minimum distance is INTERACTION_DISTANCE - 2.0f for quest item interactions
    if (range <= 0.0f || range < (INTERACTION_DISTANCE - 2.0f))
        range = INTERACTION_DISTANCE - 2.0f;
        
    return bot->GetDistance(target) > range;
}

Unit* FarFromQuestItemTargetTrigger::FindBestQuestItemTarget() const
{
    Item* questItem = nullptr;
    uint32 spellId = 0;
    
    if (!HasQuestItemWithSpell(&questItem, &spellId))
        return nullptr;

    Unit* bestTarget = nullptr;
    float closestDistance = QUEST_ITEM_TARGET_RANGE;

    // Get nearby units that could be quest targets
    GuidVector targets = AI_VALUE(GuidVector, "possible targets");
    
    for (ObjectGuid guid : targets)
    {
        Unit* target = botAI->GetUnit(guid);
        if (!target)
            continue;

        // Early distance check before expensive spell validation
        float distance = bot->GetDistance(target);
        if (distance >= closestDistance)
            continue;

        // Check if this target is valid for our quest item spell
        if (!IsTargetValidForSpell(target, spellId))
            continue;

        // Target is both valid and closer
        closestDistance = distance;
        bestTarget = target;
    }

    return bestTarget;
}

bool FarFromQuestItemTargetTrigger::HasQuestItemWithSpell(Item** outItem, uint32* outSpellId) const
{
    // Reuse the same logic as QuestItemUsableTrigger
    for (uint8 bag = INVENTORY_SLOT_ITEM_START; bag < INVENTORY_SLOT_ITEM_END; ++bag)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (!item)
            continue;

        const ItemTemplate* itemTemplate = item->GetTemplate();
        if (!itemTemplate)
            continue;

        if (!(itemTemplate->Flags & ITEM_FLAG_PLAYERCAST))
            continue;

        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            uint32 spellId = itemTemplate->Spells[i].SpellId;
            if (spellId > 0 && botAI->CanCastSpell(spellId, bot, false))
            {
                if (outItem)
                    *outItem = item;
                if (outSpellId)
                    *outSpellId = spellId;
                return true;
            }
        }
    }

    return false;
}

bool FarFromQuestItemTargetTrigger::IsTargetValidForSpell(Unit* target, uint32 spellId) const
{
    if (!target || !target->IsAlive())
        return false;

    return CheckSpellConditions(spellId, target);
}

bool FarFromQuestItemTargetTrigger::CheckSpellConditions(uint32 spellId, Unit* target) const
{
    // Query conditions table for this spell to find required target conditions
    ConditionList conditions = sConditionMgr->GetConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_SPELL, spellId);
    
    for (Condition const* condition : conditions)
    {
        if (condition->ConditionType == CONDITION_AURA)
        {
            uint32 requiredAuraId = condition->ConditionValue1;
            bool hasAura = target->HasAura(requiredAuraId);
            
            if (condition->NegativeCondition)
                return !hasAura;
            else
                return hasAura;
        }
        else if (condition->ConditionType == CONDITION_CREATURE_TYPE)
        {
            if (target->GetTypeId() != TYPEID_UNIT)
                return false;
                
            uint32 requiredCreatureType = condition->ConditionValue1;
            return target->ToCreature()->GetCreatureTemplate()->type == requiredCreatureType;
        }
    }

    return true;
}

bool QuestItemTargetAvailableTrigger::IsActive()
{
    // Check if we have quest items with spells
    if (!HasQuestItemWithSpell())
        return false;

    // Check if there are valid targets nearby
    return HasValidTargetsNearby();
}

bool QuestItemTargetAvailableTrigger::HasQuestItemWithSpell() const
{
    for (uint8 bag = INVENTORY_SLOT_ITEM_START; bag < INVENTORY_SLOT_ITEM_END; ++bag)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (!item)
            continue;

        const ItemTemplate* itemTemplate = item->GetTemplate();
        if (!itemTemplate)
            continue;

        if (!(itemTemplate->Flags & ITEM_FLAG_PLAYERCAST))
            continue;

        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            uint32 spellId = itemTemplate->Spells[i].SpellId;
            if (spellId > 0 && botAI->CanCastSpell(spellId, bot, false))
                return true;
        }
    }

    return false;
}

bool QuestItemTargetAvailableTrigger::HasValidTargetsNearby() const
{
    // Get nearby NPCs
    GuidVector npcs = AI_VALUE(GuidVector, "nearest npcs");
    
    // Check if any nearby NPCs could be valid quest targets
    for (ObjectGuid guid : npcs)
    {
        Unit* target = botAI->GetUnit(guid);
        if (!target)
            continue;

        if (bot->GetDistance(target) <= QUEST_ITEM_TARGET_RANGE)
            return true;
    }

    return false;
}
