/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "QuestItemHelper.h"

#include "ConditionMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "Unit.h"
#include "ObjectGuid.h"
#include "AiObjectContext.h"
#include "CreatureAI.h"

// Static cache definitions
std::unordered_map<ObjectGuid, QuestItemHelper::QuestItemCacheEntry> QuestItemHelper::questItemCache;
std::unordered_map<uint32, std::vector<Condition const*>> QuestItemHelper::spellConditionCache;
std::mutex QuestItemHelper::cacheMutex;

Item* QuestItemHelper::FindBestQuestItem(Player* bot, uint32* outSpellId)
{
    if (!bot)
        return nullptr;

    return GetCachedQuestItem(bot, outSpellId);
}

bool QuestItemHelper::IsValidQuestItem(Item* item, uint32* outSpellId)
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

Unit* QuestItemHelper::FindBestTargetForQuestItem(PlayerbotAI* botAI, uint32 spellId)
{
    if (!botAI)
        return nullptr;

    Player* bot = botAI->GetBot();
    if (!bot)
        return nullptr;

    Unit* bestTarget = nullptr;
    float closestDistance = sPlayerbotAIConfig->grindDistance;

    // Combine both target lists to avoid duplicate processing
    GuidVector allTargets;
    
    // Get possible targets
    GuidVector possibleTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
    allTargets.insert(allTargets.end(), possibleTargets.begin(), possibleTargets.end());
    
    // Get nearby NPCs (only if we didn't find anything in possible targets)
    GuidVector npcs = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
    allTargets.insert(allTargets.end(), npcs.begin(), npcs.end());
    
    // Remove duplicates - NPCs might be in both lists
    std::sort(allTargets.begin(), allTargets.end());
    allTargets.erase(std::unique(allTargets.begin(), allTargets.end()), allTargets.end());
    
    for (ObjectGuid guid : allTargets)
    {
        Unit* target = botAI->GetUnit(guid);
        if (!target)
            continue;

        // Quick rough distance check first (cheapest operation)
        // Use squared distance to avoid sqrt() - even cheaper than GetDistance()
        float dx = bot->GetPositionX() - target->GetPositionX();
        float dy = bot->GetPositionY() - target->GetPositionY();
        float dz = bot->GetPositionZ() - target->GetPositionZ();
        float distanceSquared = dx*dx + dy*dy + dz*dz;
        
        // Quick reject if obviously too far (compare squared distances)
        float maxDistanceSquared = closestDistance * closestDistance;
        if (distanceSquared >= maxDistanceSquared)
            continue;

        // Now do spell validation for potentially close targets
        if (!IsTargetValidForSpell(target, spellId))
            continue;

        // Only do precise distance calculation for targets that passed both checks
        float distance = bot->GetDistance(target);
        if (distance >= closestDistance)
            continue;

        // Target is both valid and closer
        closestDistance = distance;
        bestTarget = target;
    }

    return bestTarget;
}

bool QuestItemHelper::IsTargetValidForSpell(Unit* target, uint32 spellId)
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

bool QuestItemHelper::CheckSpellConditions(uint32 spellId, Unit* target)
{
    if (!target)
        return false;

    // Use cached conditions to avoid repeated DB queries
    const std::vector<Condition const*>* conditions = GetCachedSpellConditions(spellId);
    
    // If no conditions are found, assume the target is valid
    if (!conditions || conditions->empty())
        return true;

    // Check each condition
    for (Condition const* condition : *conditions)
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

void QuestItemHelper::InvalidateQuestItemCache(ObjectGuid botGuid)
{
    std::lock_guard<std::mutex> lock(cacheMutex);
    questItemCache.erase(botGuid);
}

Item* QuestItemHelper::GetCachedQuestItem(Player* bot, uint32* outSpellId)
{
    if (!bot)
        return nullptr;

    ObjectGuid botGuid = bot->GetGUID();
    uint32 currentInventoryChangeCount = bot->GetTotalPlayedTime(); // Use as a simple change indicator
    
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        auto it = questItemCache.find(botGuid);
        if (it != questItemCache.end())
        {
            // Check if cached data is still valid
            if (it->second.inventoryChangeCount == currentInventoryChangeCount && it->second.item)
            {
                // Verify the cached item still exists and is still valid
                if (bot->GetItemByGuid(it->second.item->GetGUID()) && IsValidQuestItem(it->second.item))
                {
                    if (outSpellId)
                        *outSpellId = it->second.spellId;
                    return it->second.item;
                }
            }
        }
    }
    
    // Cache miss or invalid - perform fresh search
    Item* foundItem = nullptr;
    uint32 foundSpellId = 0;
    
    // Search through all inventory slots for quest items with spells
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;

        uint32 spellId = 0;
        if (IsValidQuestItem(item, &spellId))
        {
            foundItem = item;
            foundSpellId = spellId;
            break; // Early exit optimization - take first valid item found
        }
    }

    // Also search through bag slots if not found in main inventory
    if (!foundItem)
    {
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
                    foundItem = item;
                    foundSpellId = spellId;
                    break;
                }
            }
            
            if (foundItem)
                break; // Early exit from bag loop
        }
    }
    
    // Update cache
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (foundItem)
        {
            questItemCache[botGuid] = {foundItem, foundSpellId, currentInventoryChangeCount};
        }
        else
        {
            // Cache the "no item found" result to avoid repeated searches
            questItemCache[botGuid] = {nullptr, 0, currentInventoryChangeCount};
        }
    }
    
    if (outSpellId)
        *outSpellId = foundSpellId;
        
    return foundItem;
}

const std::vector<Condition const*>* QuestItemHelper::GetCachedSpellConditions(uint32 spellId)
{
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        auto it = spellConditionCache.find(spellId);
        if (it != spellConditionCache.end())
        {
            return &it->second;
        }
    }
    
    // Cache miss - query database
    ConditionList conditions = sConditionMgr->GetConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_SPELL, spellId);
    
    // Convert to vector for caching
    std::vector<Condition const*> conditionVector;
    for (Condition const* condition : conditions)
    {
        conditionVector.push_back(condition);
    }
    
    // Update cache
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        spellConditionCache[spellId] = std::move(conditionVector);
        return &spellConditionCache[spellId];
    }
}
