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
        // Debug: Uncomment to see if we're not finding quest items
        botAI->TellMaster("DEBUG: No quest items with spells found");
        return false;
    }

    // Check if there are valid targets for this quest item
    bool hasValidTarget = HasValidTargetForQuestItem(spellId);
    
    // Debug: Uncomment to see trigger activation
    if (hasValidTarget)
        botAI->TellMaster("DEBUG: Quest item usable trigger activated!");
    else
        botAI->TellMaster("DEBUG: No valid targets for quest item");
    
    return hasValidTarget;
}

bool QuestItemUsableTrigger::HasQuestItemWithSpell(Item** outItem, uint32* outSpellId) const
{
    // Debug: Check if we're even scanning inventory
    botAI->TellMaster("DEBUG: Scanning for quest items...");
    
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

        // Debug: Show what relevant items we find
        std::ostringstream out;
        out << "DEBUG: Found quest/consumable item " << itemTemplate->Name1 << " (ID: " << itemTemplate->ItemId << ", Class: " << itemTemplate->Class << ", Flags: " << itemTemplate->Flags << ")";
        botAI->TellMaster(out.str());

        // Check if this is a quest item with player-castable spells
        if (!(itemTemplate->Flags & ITEM_FLAG_PLAYERCAST))
            continue;

        botAI->TellMaster("DEBUG: Item has ITEM_FLAG_PLAYERCAST!");

        // Check if the item has an associated spell
        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            uint32 spellId = itemTemplate->Spells[i].SpellId;
            if (spellId > 0)
            {
                std::ostringstream spellOut;
                spellOut << "DEBUG: Item has spell ID " << spellId << " - valid quest item!";
                botAI->TellMaster(spellOut.str());
                
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

            // Debug: Show what relevant items we find in bags
            std::ostringstream out;
            out << "DEBUG: Found bag quest/consumable item " << itemTemplate->Name1 << " (ID: " << itemTemplate->ItemId << ", Class: " << itemTemplate->Class << ", Flags: " << itemTemplate->Flags << ")";
            botAI->TellMaster(out.str());

            // Check if this is a quest item with player-castable spells
            if (!(itemTemplate->Flags & ITEM_FLAG_PLAYERCAST))
                continue;

            botAI->TellMaster("DEBUG: Bag item has ITEM_FLAG_PLAYERCAST!");

            // Check if the item has an associated spell
            for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                uint32 spellId = itemTemplate->Spells[i].SpellId;
                if (spellId > 0)
                {
                    std::ostringstream spellOut;
                    spellOut << "DEBUG: Bag item has spell ID " << spellId << " - valid quest item!";
                    botAI->TellMaster(spellOut.str());
                    
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
    std::ostringstream debugOut;
    debugOut << "DEBUG: Looking for targets for spell ID " << spellId;
    botAI->TellMaster(debugOut.str());

    // Get nearby units that could be quest targets
    GuidVector targets = AI_VALUE(GuidVector, "possible targets");
    
    debugOut.str("");
    debugOut << "DEBUG: Found " << targets.size() << " possible targets";
    botAI->TellMaster(debugOut.str());
    
    for (ObjectGuid guid : targets)
    {
        Unit* target = botAI->GetUnit(guid);
        if (!target)
            continue;

        debugOut.str("");
        debugOut << "DEBUG: Checking target " << target->GetName() << " (ID: " << target->GetEntry() << ")";
        botAI->TellMaster(debugOut.str());

        // Check if this target is valid for our quest item spell
        if (IsTargetValidForSpell(target, spellId))
        {
            botAI->TellMaster("DEBUG: Found valid target!");
            return true;
        }
    }

    // Also check nearby NPCs specifically
    GuidVector npcs = AI_VALUE(GuidVector, "nearest npcs");
    
    debugOut.str("");
    debugOut << "DEBUG: Also checking " << npcs.size() << " nearby NPCs";
    botAI->TellMaster(debugOut.str());
    
    for (ObjectGuid guid : npcs)
    {
        Unit* target = botAI->GetUnit(guid);
        if (!target)
            continue;

        debugOut.str("");
        debugOut << "DEBUG: Checking NPC " << target->GetName() << " (ID: " << target->GetEntry() << ")";
        botAI->TellMaster(debugOut.str());

        // Check if this target is valid for our quest item spell
        if (IsTargetValidForSpell(target, spellId))
        {
            botAI->TellMaster("DEBUG: Found valid NPC target!");
            return true;
        }
    }

    botAI->TellMaster("DEBUG: No valid targets found after checking all possibilities");
    return false;
}

bool QuestItemUsableTrigger::IsTargetValidForSpell(Unit* target, uint32 spellId) const
{
    if (!target || !target->IsAlive())
    {
        botAI->TellMaster("DEBUG: Target is null or dead");
        return false;
    }

    std::ostringstream debugOut;
    debugOut << "DEBUG: Target " << target->GetName() << " is alive, checking spell requirements";
    botAI->TellMaster(debugOut.str());

    // Check if target is in range
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
    {
        botAI->TellMaster("DEBUG: No spell info found for spell");
        return false;
    }

    // Use the spell's actual range
    float range = spellInfo->GetMaxRange();
    
    // If spell has 0 range, use melee range as fallback
    if (range <= 0.0f)
        range = botAI->GetRange("melee");
        
    float distance = bot->GetDistance(target);
    
    debugOut.str("");
    debugOut << "DEBUG: Target distance: " << distance << ", spell range: " << range << " (spell " << spellId << ")";
    botAI->TellMaster(debugOut.str());
    
    if (distance > range)
    {
        botAI->TellMaster("DEBUG: Target out of range");
        return false;
    }

    botAI->TellMaster("DEBUG: Target in range, checking spell conditions");
    
    // Check spell-specific conditions (aura requirements, creature type, etc.)
    bool conditionsValid = CheckSpellConditions(spellId, target);
    
    debugOut.str("");
    debugOut << "DEBUG: Spell conditions valid: " << (conditionsValid ? "true" : "false");
    botAI->TellMaster(debugOut.str());
    
    return conditionsValid;
}

bool QuestItemUsableTrigger::CheckSpellConditions(uint32 spellId, Unit* target) const
{
    std::ostringstream debugOut;
    debugOut << "DEBUG: Checking spell conditions for spell " << spellId << " on target " << target->GetName();
    botAI->TellMaster(debugOut.str());

    // Query conditions table for this spell to find required target conditions
    ConditionList conditions = sConditionMgr->GetConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_SPELL, spellId);
    
    debugOut.str("");
    debugOut << "DEBUG: Found " << conditions.size() << " conditions for this spell";
    botAI->TellMaster(debugOut.str());
    
    if (conditions.empty())
    {
        botAI->TellMaster("DEBUG: No conditions found, assuming target is valid");
        return true;
    }
    
    for (Condition const* condition : conditions)
    {
        debugOut.str("");
        debugOut << "DEBUG: Checking condition type " << condition->ConditionType << " with value " << condition->ConditionValue1;
        botAI->TellMaster(debugOut.str());

        // Check for aura conditions (most common for quest items)
        if (condition->ConditionType == CONDITION_AURA)
        {
            uint32 requiredAuraId = condition->ConditionValue1;
            
            debugOut.str("");
            debugOut << "DEBUG: Checking for aura " << requiredAuraId << " on target";
            botAI->TellMaster(debugOut.str());
            
            // Check if target has the required aura
            bool hasAura = target->HasAura(requiredAuraId);
            
            debugOut.str("");
            debugOut << "DEBUG: Target has aura " << requiredAuraId << ": " << (hasAura ? "true" : "false");
            botAI->TellMaster(debugOut.str());
            
            // Apply condition logic (negated or normal)
            if (condition->NegativeCondition)
            {
                botAI->TellMaster("DEBUG: Condition is negated - target should NOT have aura");
                return !hasAura;
            }
            else
            {
                botAI->TellMaster("DEBUG: Condition is normal - target should have aura");
                return hasAura;
            }
        }
        // Add other condition types as needed
        else if (condition->ConditionType == CONDITION_CREATURE_TYPE)
        {
            botAI->TellMaster("DEBUG: Checking creature type condition");
            if (target->GetTypeId() != TYPEID_UNIT)
            {
                botAI->TellMaster("DEBUG: Target is not a creature");
                return false;
            }
                
            uint32 requiredCreatureType = condition->ConditionValue1;
            uint32 targetCreatureType = target->ToCreature()->GetCreatureTemplate()->type;
            
            debugOut.str("");
            debugOut << "DEBUG: Required creature type: " << requiredCreatureType << ", target type: " << targetCreatureType;
            botAI->TellMaster(debugOut.str());
            
            return targetCreatureType == requiredCreatureType;
        }
        else
        {
            debugOut.str("");
            debugOut << "DEBUG: Unknown condition type " << condition->ConditionType << " - assuming valid";
            botAI->TellMaster(debugOut.str());
        }
    }

    // If no specific conditions found, assume it's valid (fallback)
    botAI->TellMaster("DEBUG: All conditions checked, assuming valid");
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

    // Check if we're too far from the target (use spell's actual range)
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    float range = spellInfo ? spellInfo->GetMaxRange() : botAI->GetRange("melee");
    
    // If spell has 0 range, use melee range as fallback
    if (range <= 0.0f)
        range = botAI->GetRange("melee");
        
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

        // Check if this target is valid for our quest item spell
        if (!IsTargetValidForSpell(target, spellId))
            continue;

        float distance = bot->GetDistance(target);
        if (distance < closestDistance)
        {
            closestDistance = distance;
            bestTarget = target;
        }
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
