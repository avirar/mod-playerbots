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
    botAI->TellMaster("DEBUG: UseQuestItemOnTargetAction::Execute called!");
    
    uint32 spellId = 0;
    
    // Find the best quest item to use
    Item* questItem = FindBestQuestItem(&spellId);
    if (!questItem)
    {
        botAI->TellError("No usable quest items found");
        return false;
    }
    
    std::ostringstream out;
    out << "DEBUG: Found quest item " << questItem->GetTemplate()->Name1 << " with spell " << spellId;
    botAI->TellMaster(out.str());

    // Find the best target for this quest item
    Unit* target = FindBestTargetForQuestItem(spellId);
    if (!target)
    {
        botAI->TellError("No valid targets found for quest item");
        return false;
    }

    // Check if we're in range of the target (use the spell's actual range)
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    float range = spellInfo ? spellInfo->GetMaxRange() : botAI->GetRange("melee");
    
    // If spell has 0 range, use melee range as fallback
    if (range <= 0.0f)
        range = botAI->GetRange("melee");
        
    float distance = bot->GetDistance(target);
    
    std::ostringstream debugOut;
    debugOut << "DEBUG: Action range check - Distance: " << distance << ", Spell Range: " << range << " (spell " << spellId << ")";
    botAI->TellMaster(debugOut.str());
    
    if (distance > range)
    {
        std::ostringstream out;
        out << "Target " << target->GetName() << " is too far away for quest item (distance: " << distance << ", range: " << range << ")";
        botAI->TellMaster(out.str());
        return false;
    }
    
    botAI->TellMaster("DEBUG: Target is in range, proceeding to use quest item");

    // Use the quest item on the target
    return UseQuestItemOnTarget(questItem, target);
}

bool UseQuestItemOnTargetAction::isUseful()
{
    botAI->TellMaster("DEBUG: UseQuestItemOnTargetAction::isUseful() called");
    
    uint32 spellId = 0;
    
    // Check if we have any usable quest items
    Item* questItem = FindBestQuestItem(&spellId);
    if (!questItem)
    {
        botAI->TellMaster("DEBUG: isUseful - No quest items found");
        return false;
    }

    // Check if there are valid targets available
    Unit* target = FindBestTargetForQuestItem(spellId);
    bool useful = (target != nullptr);
    
    std::ostringstream out;
    out << "DEBUG: isUseful - Target found: " << (useful ? "true" : "false");
    botAI->TellMaster(out.str());
    
    return useful;
}

bool UseQuestItemOnTargetAction::isPossible()
{
    botAI->TellMaster("DEBUG: UseQuestItemOnTargetAction::isPossible() called - returing true");
    return true;
}

Item* UseQuestItemOnTargetAction::FindBestQuestItem(uint32* outSpellId) const
{
    // Search through all inventory slots for quest items with spells
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
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

    // Also search through bag slots
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

            uint32 spellId = 0;
            if (IsValidQuestItem(item, &spellId))
            {
                if (outSpellId)
                    *outSpellId = spellId;
                return item;
            }
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

    // Only consider quest items (class 12) or consumable items (class 0)
    if (itemTemplate->Class != ITEM_CLASS_QUEST && itemTemplate->Class != ITEM_CLASS_CONSUMABLE)
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
            // For quest items, we don't use CanCastSpell as it's too restrictive
            // Quest items should work based on quest logic, not normal spell rules
            if (outSpellId)
                *outSpellId = spellId;
            return true;
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

bool UseQuestItemOnTargetAction::UseQuestItemOnTarget(Item* item, Unit* target)
{
    if (!item || !target)
        return false;

    botAI->TellMaster("DEBUG: About to use quest item on target");

    // For quest items, we need to bypass normal spell checks
    // and send the item use packet directly with the target
    uint8 bagIndex = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    uint8 spell_index = 0;
    uint8 cast_count = 1;
    ObjectGuid item_guid = item->GetGUID();
    uint32 glyphIndex = 0;
    uint8 castFlags = 0;

    // Get the spell ID from the item
    uint32 spellId = 0;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (item->GetTemplate()->Spells[i].SpellId > 0)
        {
            spellId = item->GetTemplate()->Spells[i].SpellId;
            break;
        }
    }

    std::ostringstream debugOut;
    debugOut << "DEBUG: Using item in bag " << (int)bagIndex << " slot " << (int)slot << " with spell " << spellId << " on target " << target->GetName();
    botAI->TellMaster(debugOut.str());

    // Create the item use packet
    WorldPacket packet(CMSG_USE_ITEM);
    packet << bagIndex << slot << cast_count << spellId << item_guid << glyphIndex << castFlags;

    // Add target information
    uint32 targetFlag = TARGET_FLAG_UNIT;
    packet << targetFlag << target->GetGUID().WriteAsPacked();

    // Clear movement states like other item uses do
    bot->ClearUnitState(UNIT_STATE_CHASE);
    bot->ClearUnitState(UNIT_STATE_FOLLOW);

    if (bot->isMoving())
    {
        bot->StopMoving();
        botAI->SetNextCheckDelay(sPlayerbotAIConfig->globalCoolDown);
        return false;
    }

    // Send the packet
    bot->GetSession()->HandleUseItemOpcode(packet);

    std::ostringstream out;
    out << "Using " << chat->FormatItem(item->GetTemplate()) << " on " << target->GetName();
    botAI->TellMasterNoFacing(out.str());

    return true;
}
