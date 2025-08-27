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

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);

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
                ItemTemplate const* template_ptr = item->GetTemplate();
                if (template_ptr)
                {
                    std::ostringstream out;
                    out << "QuestItem: Found quest item " << template_ptr->Name1 << " (ID:" << item->GetEntry() << ") with spell " << spellId;
                    botAI->TellMaster(out.str());
                }
                else
                {
                    std::ostringstream out;
                    out << "QuestItem: Found quest item (no template) with spell " << spellId;
                    botAI->TellMaster(out.str());
                }
            }
            
            // Check if this quest item can be used (prevent spam casting)
            if (!CanUseQuestItem(botAI, bot, spellId))
            {
                if (botAI)
                {
                    ItemTemplate const* template_ptr = item->GetTemplate();
                    std::ostringstream out;
                    if (template_ptr)
                        out << "QuestItem: Skipping " << template_ptr->Name1 << " - usage prevented";
                    else
                        out << "QuestItem: Skipping quest item - usage prevented";
                    botAI->TellMaster(out.str());
                }
                continue; // Skip this item and look for others
            }
            
            // Return the first valid and usable quest item found
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
                // Check if this quest item can be used (prevent spam casting)
                if (!CanUseQuestItem(botAI, bot, spellId))
                {
                    if (botAI)
                    {
                        ItemTemplate const* template_ptr = item->GetTemplate();
                        std::ostringstream out;
                        if (template_ptr)
                            out << "QuestItem: Skipping bag item " << template_ptr->Name1 << " - usage prevented";
                        else
                            out << "QuestItem: Skipping bag quest item - usage prevented";
                        botAI->TellMaster(out.str());
                    }
                    continue; // Skip this item and look for others
                }
                
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

    // Check if this spell needs a target at all
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return nullptr;

    // Debug output for spell targeting info
    std::ostringstream debugOut;
    debugOut << "QuestItem: Spell " << spellId << " targets=" << spellInfo->Targets << " needsTarget=" << spellInfo->NeedsExplicitUnitTarget();
    botAI->TellMaster(debugOut.str());

    // If spell doesn't need an explicit target, return the bot as self-target
    if (!spellInfo->NeedsExplicitUnitTarget())
    {
        botAI->TellMaster("QuestItem: Spell is self-cast, using bot as target");
        return bot;
    }

    Unit* bestTarget = nullptr;
    float closestDistance = sPlayerbotAIConfig->grindDistance; // Reuse grind distance for quest target search

    // Get nearby units that could be quest targets
    GuidVector targets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
    
    if (botAI)
    {
        std::ostringstream out;
        out << "QuestItem: Found " << targets.size() << " possible targets";
        botAI->TellMaster(out.str());
    }
    
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
        
        if (botAI)
        {
            std::ostringstream out;
            out << "QuestItem: Found " << npcs.size() << " nearest npcs";
            botAI->TellMaster(out.str());
        }
        
        for (ObjectGuid guid : npcs)
        {
            Unit* target = botAI->GetUnit(guid);
            if (!target)
                continue;

            // Debug: Log each NPC we're checking
            if (botAI)
            {
                std::ostringstream out;
                out << "QuestItem: Checking NPC " << target->GetName() << " (entry:" << target->GetEntry() 
                    << ") alive:" << (target->IsAlive() ? "yes" : "no") << " distance:" << bot->GetDistance(target);
                botAI->TellMaster(out.str());
            }

            // Early distance check before expensive spell validation
            float distance = bot->GetDistance(target);
            if (distance >= closestDistance)
            {
                if (botAI)
                    botAI->TellMaster("QuestItem: NPC too far, skipping");
                continue;
            }

            if (!IsTargetValidForSpell(target, spellId, bot, botAI))
            {
                if (botAI)
                    botAI->TellMaster("QuestItem: NPC failed spell validation");
                continue;
            }

            // Target is both valid and closer
            closestDistance = distance;
            bestTarget = target;
            
            if (botAI)
            {
                std::ostringstream out;
                out << "QuestItem: Selected " << target->GetName() << " as best target";
                botAI->TellMaster(out.str());
            }
        }
    }

    return bestTarget;
}

bool QuestItemHelper::IsTargetValidForSpell(Unit* target, uint32 spellId, Player* caster, PlayerbotAI* botAI)
{
    if (!target)
        return false;

    // Check basic spell targeting requirements
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return false;

    // Prevent spam-casting: Check if target already has aura from this spell
    if (target->HasAura(spellId))
    {
        if (botAI)
        {
            std::ostringstream out;
            out << "QuestItem: Target " << target->GetName() << " already has aura " << spellId << " - preventing spam cast";
            botAI->TellMaster(out.str());
        }
        return false;
    }

    // Also check for common quest-related auras that indicate the target has been "used"
    // Many corpse-burning/interaction spells apply temporary visual or state auras
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        const SpellEffectInfo& effect = spellInfo->Effects[i];
        
        // If the spell triggers another spell, check if target has that aura too
        if (effect.Effect == SPELL_EFFECT_TRIGGER_SPELL && effect.TriggerSpell > 0)
        {
            if (target->HasAura(effect.TriggerSpell))
            {
                if (botAI)
                {
                    std::ostringstream out;
                    out << "QuestItem: Target " << target->GetName() << " has triggered aura " << effect.TriggerSpell << " - preventing spam cast";
                    botAI->TellMaster(out.str());
                }
                return false;
            }
        }
    }

    // For self-cast spells, skip the alive check (player might be casting on themselves)
    if (!spellInfo->NeedsExplicitUnitTarget())
    {
        if (botAI)
        {
            std::ostringstream out;
            out << "QuestItem: Self-cast spell " << spellId << " validation for " << target->GetName();
            botAI->TellMaster(out.str());
        }
        return CheckSpellConditions(spellId, target, caster, botAI);
    }

    // For targeted spells, check if target is alive (unless it's a quest item that can target dead units)
    if (!target->IsAlive())
    {
        // Check if this quest item spell can target dead units
        bool canTargetDead = CanQuestSpellTargetDead(spellId);
        
        if (botAI)
        {
            std::ostringstream out;
            out << "QuestItem: Target " << target->GetName() << " is dead, canTargetDead=" << (canTargetDead ? "true" : "false");
            botAI->TellMaster(out.str());
        }
        
        if (!canTargetDead)
        {
            return false;
        }
    }

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

    // Group conditions by ElseGroup for proper OR/AND logic
    std::map<uint32, std::vector<Condition const*>> conditionGroups;
    for (Condition const* condition : conditions)
    {
        conditionGroups[condition->ElseGroup].push_back(condition);
    }
    
    // Check each ElseGroup - if ANY group passes completely, the overall condition passes (OR logic)
    for (auto& [elseGroup, groupConditions] : conditionGroups)
    {
        bool groupPassed = true; // All conditions in this group must pass (AND logic within group)
        
        if (botAI)
        {
            std::ostringstream out;
            out << "QuestItem: Checking ElseGroup " << elseGroup << " with " << groupConditions.size() << " conditions";
            botAI->TellMaster(out.str());
        }
        
        for (Condition const* condition : groupConditions)
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
                    uint32 requiredType = condition->ConditionValue1;
                    uint32 targetType = target->ToCreature()->GetCreatureTemplate()->type;
                    
                    // According to conditions.md: "True if creature_template.type == ConditionValue1"
                    conditionMet = (targetType == requiredType);
                    
                    if (botAI)
                    {
                        std::ostringstream out;
                        out << "QuestItem: CONDITION_CREATURE_TYPE check - target entry:" << target->GetEntry() 
                            << " targetType:" << targetType << " requiredType:" << requiredType
                            << " result:" << (conditionMet ? "PASS" : "FAIL");
                        botAI->TellMaster(out.str());
                    }
                }
                break;
            }
            case CONDITION_OBJECT_ENTRY_GUID:
            {
                uint32 requiredTypeID = condition->ConditionValue1;
                uint32 requiredEntry = condition->ConditionValue2;
                uint32 requiredGUID = condition->ConditionValue3;
                
                // Check if target matches the required TypeID
                bool typeMatches = false;
                switch (requiredTypeID)
                {
                    case TYPEID_UNIT:
                        typeMatches = (target->GetTypeId() == TYPEID_UNIT);
                        break;
                    case TYPEID_PLAYER:
                        typeMatches = (target->GetTypeId() == TYPEID_PLAYER);
                        break;
                    case TYPEID_GAMEOBJECT:
                        typeMatches = (target->GetTypeId() == TYPEID_GAMEOBJECT);
                        break;
                    case TYPEID_CORPSE:
                        typeMatches = (target->GetTypeId() == TYPEID_CORPSE);
                        break;
                    default:
                        typeMatches = false;
                        break;
                }
                
                if (!typeMatches)
                {
                    conditionMet = false;
                }
                else
                {
                    // Check entry requirement (0 = any entry)
                    if (requiredEntry == 0)
                    {
                        conditionMet = true; // Any object of given type
                    }
                    else
                    {
                        conditionMet = (target->GetEntry() == requiredEntry);
                    }
                    
                    // TODO: Check GUID requirement if needed (requiredGUID != 0)
                    // For now, we ignore GUID checking as it's rarely used
                }
                
                if (botAI)
                {
                    std::ostringstream out;
                    out << "QuestItem: CONDITION_OBJECT_ENTRY_GUID check - target typeID:" << target->GetTypeId()
                        << " entry:" << target->GetEntry() << " requiredTypeID:" << requiredTypeID 
                        << " requiredEntry:" << requiredEntry << " result:" << (conditionMet ? "PASS" : "FAIL");
                    botAI->TellMaster(out.str());
                }
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

            // If any condition in this group fails, the whole group fails
            if (!conditionMet)
            {
                groupPassed = false;
                break; // No need to check other conditions in this group
            }
        }
        
        // If this group passed all its conditions, the overall condition is met
        if (groupPassed)
        {
            if (botAI)
            {
                std::ostringstream out;
                out << "QuestItem: ElseGroup " << elseGroup << " PASSED - spell condition met";
                botAI->TellMaster(out.str());
            }
            return true;
        }
        
        if (botAI)
        {
            std::ostringstream out;
            out << "QuestItem: ElseGroup " << elseGroup << " FAILED";
            botAI->TellMaster(out.str());
        }
    }

    // No ElseGroup passed
    if (botAI)
    {
        std::ostringstream out;
        out << "QuestItem: Spell " << spellId << " REJECTED - all condition groups failed";
        botAI->TellMaster(out.str());
    }
    return false;
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
        if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(player))
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
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(player))
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

bool QuestItemHelper::CanQuestSpellTargetDead(uint32 spellId)
{
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return false;

    // Check spell attributes that allow targeting dead units  
    if (spellInfo->HasAttribute(SPELL_ATTR2_ALLOW_DEAD_TARGET) || 
        spellInfo->HasAttribute(SPELL_ATTR3_IGNORE_CASTER_AND_TARGET_RESTRICTIONS))
        return true;

    // Check for implicit target types that indicate corpse targeting
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        const SpellEffectInfo& effect = spellInfo->Effects[i];
        
        // Check implicit targets - some target dead units specifically
        uint32 targetA = effect.TargetA.GetTarget();
        uint32 targetB = effect.TargetB.GetTarget();
        
        // TARGET_UNIT_TARGET_ENEMY (6) with certain spell effects often work on corpses
        // TARGET_UNIT_NEARBY_ENTRY (46) can target dead units with specific entries
        if (targetA == 6 || targetA == 46 || targetB == 6 || targetB == 46)
        {
            // Combined with DUMMY or SCRIPT effects suggests corpse interaction
            if (effect.Effect == SPELL_EFFECT_DUMMY || effect.Effect == SPELL_EFFECT_SCRIPT_EFFECT)
            {
                return true;
            }
        }
        
        // SPELL_EFFECT_DUMMY effects are often used for quest interactions with corpses
        if (effect.Effect == SPELL_EFFECT_DUMMY)
            return true;
            
        // SPELL_EFFECT_SCRIPT_EFFECT is also commonly used for quest spells
        if (effect.Effect == SPELL_EFFECT_SCRIPT_EFFECT)
            return true;
    }

    // Check spell name/description for corpse-related keywords
    if (spellInfo->SpellName[0] && strlen(spellInfo->SpellName[0]) > 0)
    {
        std::string spellName = spellInfo->SpellName[0];
        std::transform(spellName.begin(), spellName.end(), spellName.begin(), ::tolower);
        
        // Look for corpse-related keywords in spell name
        if (spellName.find("burn") != std::string::npos ||
            spellName.find("corpse") != std::string::npos ||
            spellName.find("body") != std::string::npos ||
            spellName.find("remains") != std::string::npos)
        {
            return true;
        }
    }

    return false; // By default, assume spells cannot target dead units
}

bool QuestItemHelper::CanUseQuestItem(PlayerbotAI* botAI, Player* player, uint32 spellId)
{
    if (!player || !botAI)
        return false;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return false;

    // Check for existing spell auras (prevents recasting buffs/summons)
    if (player->HasAura(spellId))
    {
        std::ostringstream out;
        out << "QuestItem: Player already has aura " << spellId << " - preventing recast";
        botAI->TellMaster(out.str());
        return false;
    }

    // Check for summon spells - see if the summoned creature already exists nearby
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        if (spellInfo->Effects[i].Effect == SPELL_EFFECT_SUMMON || 
            spellInfo->Effects[i].Effect == SPELL_EFFECT_SUMMON_PET ||
            spellInfo->Effects[i].Effect == SPELL_EFFECT_SUMMON_OBJECT_WILD)
        {
            uint32 summonEntry = spellInfo->Effects[i].MiscValue;
            if (summonEntry > 0)
            {
                // Check if this summon already exists nearby (within 30 yards)
                bool foundExistingSummon = IsNearCreature(botAI, summonEntry, 30.0f, true);
                
                if (foundExistingSummon)
                {
                    std::ostringstream out;
                    out << "QuestItem: Summon creature " << summonEntry << " already exists nearby - preventing recast";
                    botAI->TellMaster(out.str());
                    return false;
                }
                else
                {
                    std::ostringstream out;
                    out << "QuestItem: No existing summon " << summonEntry << " found - allowing cast";
                    botAI->TellMaster(out.str());
                }
            }
        }
    }

    // Check for other spell effects that should prevent recasting
    // Future: Add checks for:
    // - SPELL_EFFECT_APPLY_AURA with specific aura types
    // - Transformation effects
    // - Vehicle effects
    // - Item enchantments
    // - etc.

    std::ostringstream out;
    out << "QuestItem: Quest item with spell " << spellId << " can be used";
    botAI->TellMaster(out.str());
    return true;
}
