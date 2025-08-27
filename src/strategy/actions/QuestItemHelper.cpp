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

Item* QuestItemHelper::FindBestQuestItem(Player* bot, uint32* outSpellId)
{
    if (!bot)
        return nullptr;

    PlayerbotAI* botAI = bot->GetPlayerbotAI();

    // Search through all inventory slots for quest items with spells
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;

        uint32 spellId = 0;
        if (IsValidQuestItem(item, &spellId))
        {
            // Debug output for found quest item
            if (botAI)
            {
                std::ostringstream out;
                out << "QuestItem: Found quest item " << item->GetTemplate()->Name1 << " (ID:" << item->GetEntry() << ") with spell " << spellId;
                botAI->TellMaster(out.str());
            }
            
            // Return the first valid quest item found
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
    float closestDistance = sPlayerbotAIConfig->grindDistance; // Reuse grind distance for quest target search

    // Get nearby units that could be quest targets
    GuidVector targets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
    
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
        if (!IsTargetValidForSpell(target, spellId, bot, botAI))
            continue;

        // Target is both valid and closer
        closestDistance = distance;
        bestTarget = target;
    }

    // Also check nearby NPCs specifically (they might not be in possible targets)
    if (!bestTarget)
    {
        GuidVector npcs = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
        
        for (ObjectGuid guid : npcs)
        {
            Unit* target = botAI->GetUnit(guid);
            if (!target)
                continue;

            // Early distance check before expensive spell validation
            float distance = bot->GetDistance(target);
            if (distance >= closestDistance)
                continue;

            if (!IsTargetValidForSpell(target, spellId, bot, botAI))
                continue;

            // Target is both valid and closer
            closestDistance = distance;
            bestTarget = target;
        }
    }

    return bestTarget;
}

bool QuestItemHelper::IsTargetValidForSpell(Unit* target, uint32 spellId, Player* caster, PlayerbotAI* botAI)
{
    if (!target || !target->IsAlive())
        return false;

    // Check basic spell targeting requirements
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return false;

    // Check spell-specific conditions (aura requirements, creature type, etc.)
    return CheckSpellConditions(spellId, target, caster, botAI);
}

bool QuestItemHelper::CheckSpellConditions(uint32 spellId, Unit* target, Player* caster, PlayerbotAI* botAI)
{
    if (!target)
    {
        if (botAI)
        {
            std::ostringstream out;
            out << "QuestItem: CheckSpellConditions - no target for spell " << spellId;
            botAI->TellMaster(out.str());
        }
        return false;
    }

    // If no caster was provided, try to determine it from the target
    if (!caster)
    {
        if (target->GetTypeId() == TYPEID_PLAYER)
            caster = target->ToPlayer();
        else
        {
            // Get the caster from the target's master or nearby players
            if (Unit* owner = target->GetOwner())
                if (owner->GetTypeId() == TYPEID_PLAYER)
                    caster = owner->ToPlayer();
        }
    }

    // Check spell location requirements first (RequiredAreasID from spell data)
    if (caster && !CheckSpellLocationRequirements(caster, spellId))
    {
        if (botAI)
        {
            std::ostringstream out;
            out << "QuestItem: Spell " << spellId << " failed location requirements";
            botAI->TellMaster(out.str());
        }
        return false;
    }

    // Query conditions table for this spell to find required target conditions
    ConditionList conditions = sConditionMgr->GetConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_SPELL, spellId);
    
    // Debug output for conditions found
    if (botAI)
    {
        std::ostringstream out;
        out << "QuestItem: Spell " << spellId << " has " << conditions.size() << " conditions for target " << target->GetName() << " (entry:" << target->GetEntry() << ")";
        botAI->TellMaster(out.str());
    }
    
    // If no conditions are found, assume the target is valid
    if (conditions.empty())
        return true;

    // Check each condition
    for (Condition const* condition : conditions)
    {
        bool conditionMet = false;

        // Debug output for each condition
        if (botAI)
        {
            std::ostringstream out;
            out << "QuestItem: Checking condition type " << condition->ConditionType 
                << " values(" << condition->ConditionValue1 << "," << condition->ConditionValue2 << "," << condition->ConditionValue3 << ")"
                << " negative:" << (condition->NegativeCondition ? "true" : "false");
            botAI->TellMaster(out.str());
        }

        switch (condition->ConditionType)
        {
            case CONDITION_AURA:
            {
                uint32 requiredAuraId = condition->ConditionValue1;
                bool hasAura = target->HasAura(requiredAuraId);
                conditionMet = hasAura;
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
            case CONDITION_NEAR_CREATURE:
            {
                if (botAI)
                {
                    uint32 creatureEntry = condition->ConditionValue1;
                    float maxDistance = condition->ConditionValue2;
                    bool requireAlive = (condition->ConditionValue3 == 0);
                    
                    conditionMet = IsNearCreature(botAI, creatureEntry, maxDistance, requireAlive);
                    
                    std::ostringstream out;
                    out << "QuestItem: NEAR_CREATURE check for entry " << creatureEntry 
                        << " within " << maxDistance << "y, requireAlive:" << (requireAlive ? "true" : "false")
                        << " = " << (conditionMet ? "PASSED" : "FAILED");
                    botAI->TellMaster(out.str());
                }
                break;
            }
            case CONDITION_AREAID:
            {
                if (caster)
                {
                    uint32 requiredAreaId = condition->ConditionValue1;
                    uint32 playerAreaId = caster->GetAreaId();
                    conditionMet = (playerAreaId == requiredAreaId);
                }
                break;
            }
            case CONDITION_ZONEID:
            {
                if (caster)
                {
                    uint32 requiredZoneId = condition->ConditionValue1;
                    uint32 playerZoneId = caster->GetZoneId();
                    conditionMet = (playerZoneId == requiredZoneId);
                }
                break;
            }
            case CONDITION_MAPID:
            {
                if (caster)
                {
                    uint32 requiredMapId = condition->ConditionValue1;
                    uint32 playerMapId = caster->GetMapId();
                    conditionMet = (playerMapId == requiredMapId);
                }
                break;
            }
            case CONDITION_ALIVE:
            {
                bool isAlive = target->IsAlive();
                conditionMet = isAlive;
                break;
            }
            case CONDITION_HP_PCT:
            {
                if (target->GetMaxHealth() > 0)
                {
                    uint32 requiredPct = condition->ConditionValue1;
                    uint32 currentPct = (target->GetHealth() * 100) / target->GetMaxHealth();
                    uint32 compareType = condition->ConditionValue2;
                    
                    switch (compareType)
                    {
                        case 0: conditionMet = (currentPct == requiredPct); break; // Equal
                        case 1: conditionMet = (currentPct > requiredPct); break;  // Higher
                        case 2: conditionMet = (currentPct < requiredPct); break;  // Lower
                        case 3: conditionMet = (currentPct >= requiredPct); break; // Equal or higher
                        case 4: conditionMet = (currentPct <= requiredPct); break; // Equal or lower
                        default: conditionMet = true; break;
                    }
                }
                break;
            }
            case CONDITION_UNIT_STATE:
            {
                uint32 requiredState = condition->ConditionValue1;
                conditionMet = target->HasUnitState(static_cast<UnitState>(requiredState));
                break;
            }
            case CONDITION_NEAR_GAMEOBJECT:
            {
                if (caster)
                {
                    uint32 goEntry = condition->ConditionValue1;
                    float maxDistance = condition->ConditionValue2;
                    // For simplicity, we'll assume this condition is met
                    // Full implementation would require searching for nearby gameobjects
                    conditionMet = true; // TODO: Implement gameobject proximity check
                }
                break;
            }
            default:
                // For unknown condition types, assume they're met to avoid breaking functionality
                conditionMet = true;
                break;
        }

        // Apply negative condition logic
        if (condition->NegativeCondition)
            conditionMet = !conditionMet;

        // Debug output for final condition result
        if (botAI)
        {
            std::ostringstream out;
            out << "QuestItem: Condition type " << condition->ConditionType << " final result: " << (conditionMet ? "PASSED" : "FAILED");
            botAI->TellMaster(out.str());
        }

        // If any condition is not met, the target is invalid
        if (!conditionMet)
        {
            if (botAI)
            {
                std::ostringstream out;
                out << "QuestItem: Spell " << spellId << " REJECTED - condition failed";
                botAI->TellMaster(out.str());
            }
            return false;
        }
    }

    // All conditions were met
    return true;
}

bool QuestItemHelper::IsNearCreature(PlayerbotAI* botAI, uint32 creatureEntry, float maxDistance, bool requireAlive)
{
    if (!botAI)
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    // Use the existing playerbots infrastructure to get nearby NPCs
    GuidVector npcs = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
    
    std::ostringstream debugOut;
    debugOut << "QuestItem: IsNearCreature scanning " << npcs.size() << " nearby NPCs for entry " << creatureEntry;
    botAI->TellMaster(debugOut.str());
    
    int foundCount = 0;
    for (ObjectGuid guid : npcs)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;
            
        if (unit->GetEntry() == creatureEntry)
        {
            foundCount++;
            float distance = bot->GetDistance(unit);
            bool alive = unit->IsAlive();
            
            std::ostringstream out;
            out << "QuestItem: Found creature " << creatureEntry << " at " << distance << "y, alive:" << (alive ? "true" : "false");
            botAI->TellMaster(out.str());
            
            // Check distance requirement
            if (distance > maxDistance)
            {
                out.str("");
                out << "QuestItem: Creature too far (" << distance << " > " << maxDistance << ")";
                botAI->TellMaster(out.str());
                continue;
            }
                
            // Check alive/dead requirement
            if (requireAlive && alive)
            {
                botAI->TellMaster("QuestItem: FOUND valid alive creature!");
                return true;
            }
            if (!requireAlive && !alive)
            {
                botAI->TellMaster("QuestItem: FOUND valid dead creature!");
                return true;
            }
        }
    }

    std::ostringstream finalOut;
    finalOut << "QuestItem: No valid creature found. Total matching entry: " << foundCount;
    botAI->TellMaster(finalOut.str());
    return false;
}

bool QuestItemHelper::CheckSpellLocationRequirements(Player* player, uint32 spellId)
{
    if (!player)
        return false;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return true; // No spell info means no restrictions

    // Use the built-in SpellInfo location checking
    uint32 mapId = player->GetMapId();
    uint32 zoneId = player->GetZoneId();
    uint32 areaId = player->GetAreaId();
    
    SpellCastResult locationResult = spellInfo->CheckLocation(mapId, zoneId, areaId, player);
    if (locationResult != SPELL_CAST_OK)
    {
        // Debug output for location failures
        if (PlayerbotAI* botAI = player->GetPlayerbotAI())
        {
            std::ostringstream out;
            out << "QuestItem: Spell " << spellId << " location check failed. Map:" << mapId 
                << " Zone:" << zoneId << " Area:" << areaId << " AreaGroupId:" << spellInfo->AreaGroupId 
                << " Result:" << locationResult;
            botAI->TellMaster(out.str());
        }
        return false; // Player is not in a valid location for this spell
    }

    // Debug output for successful location check
    if (PlayerbotAI* botAI = player->GetPlayerbotAI())
    {
        if (spellInfo->AreaGroupId > 0)
        {
            std::ostringstream out;
            out << "QuestItem: Spell " << spellId << " location check passed. Map:" << mapId 
                << " Zone:" << zoneId << " Area:" << areaId << " AreaGroupId:" << spellInfo->AreaGroupId;
            botAI->TellMaster(out.str());
        }
    }

    return true; // All location requirements met
}
