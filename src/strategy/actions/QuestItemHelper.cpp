/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "QuestItemHelper.h"

#include "ConditionMgr.h"
#include "GameObject.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Object.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "SmartScriptMgr.h"
#include "SpellInfo.h"
#include "Unit.h"
#include "DBCStores.h"

Item* QuestItemHelper::FindBestQuestItem(Player* bot, uint32* outSpellId)
{
    if (!bot)
        return nullptr;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);

    // NEW APPROACH: Find quest item that has valid targets available
    // This prevents selecting items that have no valid targets
    
    std::vector<std::pair<Item*, uint32>> candidateItems;
    
    // Collect all valid quest items from inventory
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;

        uint32 spellId = 0;
        if (IsValidQuestItem(item, &spellId))
        {
            candidateItems.push_back({item, spellId});
        }
    }

    // Also collect from bag slots
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
                candidateItems.push_back({item, spellId});
            }
        }
    }

    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestItem: Found " << candidateItems.size() << " candidate quest items";
        botAI->TellMaster(out.str());
    }

    // Now test each item to see if it has valid targets
    for (auto& [item, spellId] : candidateItems)
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            ItemTemplate const* template_ptr = item->GetTemplate();
            std::ostringstream out;
            if (template_ptr)
                out << "QuestItem: Testing item " << template_ptr->Name1 << " (ID:" << item->GetEntry() << ") with spell " << spellId;
            else
                out << "QuestItem: Testing quest item (no template) with spell " << spellId;
            botAI->TellMaster(out.str());
        }

        // Check if this quest item is needed
        if (!IsQuestItemNeeded(bot, item, spellId))
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                ItemTemplate const* template_ptr = item->GetTemplate();
                std::ostringstream out;
                if (template_ptr)
                    out << "QuestItem: Skipping " << template_ptr->Name1 << " - not needed for active quests";
                else
                    out << "QuestItem: Skipping quest item - not needed for active quests";
                botAI->TellMaster(out.str());
            }
            continue;
        }

        // Check if item can be used (not on cooldown, etc)
        if (!CanUseQuestItem(botAI, bot, spellId))
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                ItemTemplate const* template_ptr = item->GetTemplate();
                std::ostringstream out;
                if (template_ptr)
                    out << "QuestItem: Skipping " << template_ptr->Name1 << " - usage prevented";
                else
                    out << "QuestItem: Skipping quest item - usage prevented";
                botAI->TellMaster(out.str());
            }
            continue;
        }

        // Most importantly: Check if there are valid targets for this specific spell
        WorldObject* testTarget = FindBestTargetForQuestItem(botAI, spellId, item);
        if (testTarget)
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                ItemTemplate const* template_ptr = item->GetTemplate();
                std::ostringstream out;
                if (template_ptr)
                    out << "QuestItem: Selected " << template_ptr->Name1 << " - found valid target " << testTarget->GetName();
                else
                    out << "QuestItem: Selected quest item - found valid target " << testTarget->GetName();
                botAI->TellMaster(out.str());
            }
            
            if (outSpellId)
                *outSpellId = spellId;
            return item;
        }
        else
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                ItemTemplate const* template_ptr = item->GetTemplate();
                std::ostringstream out;
                if (template_ptr)
                    out << "QuestItem: Skipping " << template_ptr->Name1 << " - no valid targets found";
                else
                    out << "QuestItem: Skipping quest item - no valid targets found";
                botAI->TellMaster(out.str());
            }
        }
    }

    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        botAI->TellMaster("QuestItem: No quest items with valid targets found");
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

    // Check if the item has an associated spell that we can cast
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        uint32 spellId = itemTemplate->Spells[i].SpellId;
        if (spellId > 0)
        {
            // Quest-driven approach: Accept items with spells if they're needed for active quests
            // This handles items like "Tender Strider Meat" that have spells but no PLAYERCAST flag
            if (outSpellId)
                *outSpellId = spellId;
            return true;
        }
    }
    
    // Legacy check: Items with PLAYERCAST flag (kept for compatibility)
    if (itemTemplate->Flags & ITEM_FLAG_PLAYERCAST)
    {
        // Item has PLAYERCAST but no spell - might be a special case
        return true;
    }

    return false;
}

WorldObject* QuestItemHelper::FindBestTargetForQuestItem(PlayerbotAI* botAI, uint32 spellId, Item* questItem)
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
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream debugOut;
        debugOut << "QuestItem: Spell " << spellId << " targets=" << spellInfo->Targets << " needsTarget=" << spellInfo->NeedsExplicitUnitTarget();
        botAI->TellMaster(debugOut.str());
    }

    // NEW: Check for OPEN_LOCK spells first - these need gameobject targets, not self-cast
    if (IsOpenLockSpell(spellId) && questItem)
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            botAI->TellMaster("QuestItem: Detected OPEN_LOCK spell, looking for gameobject targets");
        
        WorldObject* gameObjectTarget = FindGameObjectForLockSpell(botAI, spellId, questItem);
        if (gameObjectTarget)
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                botAI->TellMaster("QuestItem: Found gameobject target for OPEN_LOCK spell");
            return gameObjectTarget;
        }
        else
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                botAI->TellMaster("QuestItem: No gameobject found for OPEN_LOCK spell");
            return nullptr;
        }
    }

    // If spell doesn't need an explicit target, return the bot as self-target
    if (!spellInfo->NeedsExplicitUnitTarget())
    {
        // Check location requirements for self-cast spells (like Plaguehound Cage requiring Bleeding Vale)
        if (!CheckSpellLocationRequirements(bot, spellId))
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestItem: Self-cast spell " << spellId << " failed location requirements";
                botAI->TellMaster(out.str());
            }
            return nullptr;
        }
        
        // Check spell conditions for self-cast spells (like Kyle proximity for Tender Strider Meat)
        if (!CheckSpellConditions(spellId, bot, bot, botAI))
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestItem: Self-cast spell " << spellId << " failed spell conditions";
                botAI->TellMaster(out.str());
            }
            return nullptr;
        }
        
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            botAI->TellMaster("QuestItem: Spell is self-cast, using bot as target");
        return bot;
    }

    Unit* bestTarget = nullptr;
    float closestDistance = sPlayerbotAIConfig->grindDistance; // Reuse grind distance for quest target search

    // First, try to find targets using database conditions (quest-aware targeting)
    WorldObject* conditionTarget = FindTargetUsingSpellConditions(botAI, spellId);
    if (conditionTarget)
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            botAI->TellMaster("QuestItem: Found target using spell conditions");
        return conditionTarget;
    }
    
    // Fallback to traditional targeting if no conditions found
    GuidVector targets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
    
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
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
        
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
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
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
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
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    botAI->TellMaster("QuestItem: NPC too far, skipping");
                continue;
            }

            if (!IsTargetValidForSpell(target, spellId, bot, botAI))
            {
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestItem: NPC " << target->GetName() << " (entry:" << target->GetEntry() << ") failed spell validation";
                    botAI->TellMaster(out.str());
                }
                continue;
            }

            // Target is both valid and closer
            closestDistance = distance;
            bestTarget = target;
            
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
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
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Target " << target->GetName() << " already has aura " << spellId << " - preventing spam cast";
            botAI->TellMaster(out.str());
        }
        return false;
    }

    // Check for friendly spells being cast on hostile targets
    if (caster && spellInfo->IsPositive() && target->GetTypeId() != TYPEID_PLAYER)
    {
        // If this is a beneficial spell and target is hostile to caster, reject it
        if (target->IsHostileTo(caster))
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestItem: Cannot cast beneficial spell " << spellId << " on hostile target " << target->GetName();
                botAI->TellMaster(out.str());
            }
            return false;
        }
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
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestItem: Target " << target->GetName() << " has triggered aura " << effect.TriggerSpell << " - preventing spam cast";
                    botAI->TellMaster(out.str());
                }
                return false;
            }
        }
    }

    // Additional spam prevention: Check for any aura that might indicate "burning" or "used" state
    // Look for any fire, burning, or interaction auras on the target
    Unit::AuraApplicationMap const& auras = target->GetAppliedAuras();
    for (auto itr = auras.begin(); itr != auras.end(); ++itr)
    {
        SpellInfo const* auraSpell = itr->second->GetBase()->GetSpellInfo();
        if (!auraSpell)
            continue;
            
        // Check if this looks like a burning/fire/interaction effect
        std::string spellName = auraSpell->SpellName[0] ? auraSpell->SpellName[0] : "";
        std::transform(spellName.begin(), spellName.end(), spellName.begin(), ::tolower);
        
        if (spellName.find("burn") != std::string::npos || 
            spellName.find("fire") != std::string::npos ||
            spellName.find("flame") != std::string::npos ||
            spellName.find("interact") != std::string::npos)
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestItem: Target " << target->GetName() << " has burning/interaction aura '" 
                    << spellName << "' - preventing spam cast";
                botAI->TellMaster(out.str());
            }
            return false;
        }
    }

    // For self-cast spells, skip the alive check (player might be casting on themselves)
    if (!spellInfo->NeedsExplicitUnitTarget())
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
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
        
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
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

    // Check spell-specific conditions FIRST (aura requirements, creature type, etc.)
    // This ensures only valid targets (like Alliance/Forsaken Corpses) are considered
    if (!CheckSpellConditions(spellId, target, caster, botAI))
    {
        return false;
    }

    // AFTER spell conditions pass, check if using the quest item would provide quest progress
    if (!WouldProvideQuestCredit(caster, target, spellId))
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Target " << target->GetName() << " would not provide quest credit - skipping";
            botAI->TellMaster(out.str());
        }
        return false;
    }

    // Check if we've used this quest item on this specific target recently (prevent spam)
    // But don't record usage yet - that happens when spell is actually cast
    if (!CanUseQuestItemOnTarget(botAI, target, spellId))
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Target " << target->GetName() << " used recently - preventing spam";
            botAI->TellMaster(out.str());
        }
        return false;
    }

    // All checks passed
    return true;
}

bool QuestItemHelper::CheckSpellConditions(uint32 spellId, Unit* target, Player* caster, PlayerbotAI* botAI)
{
    if (!target)
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
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
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Spell " << spellId << " failed location requirements";
            botAI->TellMaster(out.str());
        }
        return false;
    }

    // Query conditions table for this spell to find required target conditions
    ConditionList conditions = sConditionMgr->GetConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_SPELL, spellId);
    
    // Debug spell conditions
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestItem: Found " << conditions.size() << " SPELL conditions for spell " << spellId;
        botAI->TellMaster(out.str());
    }
    
    // Also check spell implicit target conditions for self-cast spells (Kyle proximity check)
    // These are stored directly in SpellInfo->Effects[i].ImplicitTargetConditions
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (spellInfo)
    {
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (spellInfo->Effects[i].ImplicitTargetConditions)
            {
                ConditionList const* implicitConditions = spellInfo->Effects[i].ImplicitTargetConditions;
                
                // Debug implicit conditions
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestItem: Found " << implicitConditions->size() << " SPELL_IMPLICIT_TARGET conditions for spell " << spellId << " effect " << (int)i;
                    botAI->TellMaster(out.str());
                }
                
                // Merge the implicit target conditions
                conditions.insert(conditions.end(), implicitConditions->begin(), implicitConditions->end());
            }
        }
    }
    
    // Debug output for conditions found
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
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
        
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Checking ElseGroup " << elseGroup << " with " << groupConditions.size() << " conditions";
            botAI->TellMaster(out.str());
        }
        
        for (Condition const* condition : groupConditions)
        {
            bool conditionMet = false;

        // Debug output for each condition
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
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
                    // Normal case: checking if the target creature matches the required type
                    uint32 requiredType = condition->ConditionValue1;
                    uint32 targetType = target->ToCreature()->GetCreatureTemplate()->type;
                    
                    // According to conditions.md: "True if creature_template.type == ConditionValue1"
                    conditionMet = (targetType == requiredType);
                    
                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "QuestItem: CONDITION_CREATURE_TYPE check - target entry:" << target->GetEntry() 
                            << " targetType:" << targetType << " requiredType:" << requiredType
                            << " result:" << (conditionMet ? "PASS" : "FAIL");
                        botAI->TellMaster(out.str());
                    }
                }
                else if (target->GetTypeId() == TYPEID_PLAYER && condition->ConditionValue2 > 0)
                {
                    // Special case: for self-cast spells, check if a nearby creature of the specified entry exists
                    // ConditionValue1 = creature type, ConditionValue2 = specific creature entry
                    uint32 requiredType = condition->ConditionValue1;
                    uint32 requiredEntry = condition->ConditionValue2;
                    
                    // Search for nearby creatures matching the entry and type
                    GuidVector npcs = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
                    
                    for (ObjectGuid guid : npcs)
                    {
                        Unit* nearbyUnit = botAI->GetUnit(guid);
                        if (!nearbyUnit || nearbyUnit->GetEntry() != requiredEntry)
                            continue;
                            
                        if (nearbyUnit->GetTypeId() == TYPEID_UNIT)
                        {
                            uint32 creatureType = nearbyUnit->ToCreature()->GetCreatureTemplate()->type;
                            if (creatureType == requiredType)
                            {
                                // Check distance using spell range with buffer for reliable casting
                                float distance = caster->GetDistance(nearbyUnit);
                                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                                float maxDistance = spellInfo ? (spellInfo->GetMaxRange() - 2.0f) : (INTERACTION_DISTANCE - 2.0f);
                                if (maxDistance <= 0.0f)
                                    maxDistance = 1.0f; // Minimum safe distance
                                
                                if (distance <= maxDistance)
                                {
                                    conditionMet = true;
                                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                                    {
                                        std::ostringstream out;
                                        out << "QuestItem: CONDITION_CREATURE_TYPE proximity check - found " << nearbyUnit->GetName() 
                                            << " (entry:" << requiredEntry << " type:" << creatureType << ") at distance " 
                                            << distance << " (max:" << maxDistance << ") = PASS";
                                        botAI->TellMaster(out.str());
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    
                    if (!conditionMet && botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "QuestItem: CONDITION_CREATURE_TYPE proximity check - no valid creature found"
                            << " (type:" << requiredType << " entry:" << requiredEntry << ") = FAIL";
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
                
                if (!typeMatches && target->GetTypeId() == TYPEID_PLAYER && requiredTypeID == TYPEID_UNIT)
                {
                    // Special case: for self-cast spells, check if a nearby unit of the specified entry exists
                    // This handles cases like Kyle proximity check for Tender Strider Meat
                    GuidVector npcs = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
                    
                    for (ObjectGuid guid : npcs)
                    {
                        Unit* nearbyUnit = botAI->GetUnit(guid);
                        if (!nearbyUnit || nearbyUnit->GetEntry() != requiredEntry)
                            continue;
                            
                        // Check distance using spell range with buffer for reliable casting
                        float distance = caster->GetDistance(nearbyUnit);
                        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                        float maxDistance = spellInfo ? (spellInfo->GetMaxRange() - 2.0f) : (INTERACTION_DISTANCE - 2.0f);
                        if (maxDistance <= 0.0f)
                            maxDistance = 1.0f; // Minimum safe distance
                        
                        if (distance <= maxDistance)
                        {
                            conditionMet = true;
                            break;
                        }
                    }
                }
                else if (!typeMatches)
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
                
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    if (target->GetTypeId() == TYPEID_PLAYER && requiredTypeID == TYPEID_UNIT)
                    {
                        out << "QuestItem: CONDITION_OBJECT_ENTRY_GUID proximity check - looked for unit entry " << requiredEntry 
                            << " near player, result:" << (conditionMet ? "PASS" : "FAIL");
                    }
                    else
                    {
                        out << "QuestItem: CONDITION_OBJECT_ENTRY_GUID direct check - target typeID:" << target->GetTypeId()
                            << " entry:" << target->GetEntry() << " requiredTypeID:" << requiredTypeID 
                            << " requiredEntry:" << requiredEntry << " result:" << (conditionMet ? "PASS" : "FAIL");
                    }
                    botAI->TellMaster(out.str());
                }
                break;
            }
            case CONDITION_NEAR_CREATURE:
            {
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
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
                    
                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "QuestItem: CONDITION_AREAID check - playerArea:" << playerAreaId 
                            << " requiredArea:" << requiredAreaId << " result:" << (conditionMet ? "PASS" : "FAIL");
                        botAI->TellMaster(out.str());
                    }
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
                    
                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "QuestItem: CONDITION_ZONEID check - playerZone:" << playerZoneId 
                            << " requiredZone:" << requiredZoneId << " result:" << (conditionMet ? "PASS" : "FAIL");
                        botAI->TellMaster(out.str());
                    }
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
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
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
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestItem: ElseGroup " << elseGroup << " PASSED - spell condition met";
                botAI->TellMaster(out.str());
            }
            return true;
        }
        
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: ElseGroup " << elseGroup << " FAILED";
            botAI->TellMaster(out.str());
        }
    }

    // No ElseGroup passed
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
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

    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream debugOut;
        debugOut << "QuestItem: IsNearCreature looking for entry " << creatureEntry 
                 << " within " << maxDistance << "y, requireAlive:" << (requireAlive ? "true" : "false");
        botAI->TellMaster(debugOut.str());
    }

    // Use the existing playerbots infrastructure to get nearby NPCs
    GuidVector npcs = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
    
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream debugOut;
        debugOut << "QuestItem: Scanning " << npcs.size() << " nearby NPCs";
        botAI->TellMaster(debugOut.str());
    }
    
    int foundCount = 0;
    int validCount = 0;
    
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
            
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestItem: Found creature " << creatureEntry << " (" << unit->GetName() 
                    << ") at " << distance << "y, alive:" << (alive ? "true" : "false");
                botAI->TellMaster(out.str());
            }
            
            // Check distance requirement
            if (distance > maxDistance)
            {
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestItem: Creature too far (" << distance << " > " << maxDistance << ")";
                    botAI->TellMaster(out.str());
                }
                continue;
            }
                
            // Check alive/dead requirement
            bool aliveRequirementMet = false;
            if (requireAlive && alive)
            {
                aliveRequirementMet = true;
            }
            else if (!requireAlive)
            {
                // When requireAlive is false, we accept both alive and dead creatures
                // This is because ConditionValue3 == 0 means "don't care about alive status"
                aliveRequirementMet = true;
            }
            
            if (aliveRequirementMet)
            {
                validCount++;
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestItem: FOUND valid creature " << unit->GetName() 
                        << " (entry:" << creatureEntry << ") at " << distance << "y";
                    botAI->TellMaster(out.str());
                }
                return true;
            }
            else
            {
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestItem: Creature " << unit->GetName() 
                        << " doesn't meet alive requirement (alive:" << (alive ? "true" : "false") 
                        << ", required:" << (requireAlive ? "alive" : "any") << ")";
                    botAI->TellMaster(out.str());
                }
            }
        }
    }


    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream finalOut;
        finalOut << "QuestItem: No valid creature found. Total matching entry: " << foundCount 
                 << ", valid distance+status: " << validCount;
        botAI->TellMaster(finalOut.str());
    }
    return false;
}

bool QuestItemHelper::CheckSpellLocationRequirements(Player* player, uint32 spellId)
{
    if (!player)
        return false;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return true; // No spell info means no restrictions

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);

    // Get current location info
    uint32 mapId = player->GetMapId();
    uint32 zoneId = player->GetZoneId();
    uint32 areaId = player->GetAreaId();
    
    // Check for specific area requirements
    if (spellInfo->AreaGroupId > 0)
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Spell " << spellId << " requires AreaGroupId " << spellInfo->AreaGroupId 
                << " - player is in Area:" << areaId << " Zone:" << zoneId << " Map:" << mapId;
            botAI->TellMaster(out.str());
        }
    }
    
    // Check RequiresSpellFocus (similar to Spell.cpp implementation)
    if (spellInfo->RequiresSpellFocus)
    {
        bool foundSpellFocus = false;
        
        // Use existing playerbot infrastructure to find nearby gameobjects
        GuidVector nearbyGameObjects = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest game objects")->Get();
        
        for (ObjectGuid goGuid : nearbyGameObjects)
        {
            GameObject* go = botAI->GetGameObject(goGuid);
            if (!go)
                continue;
                
            // Check if this gameobject is a spell focus object with matching focus ID
            if (go->GetGoType() == GAMEOBJECT_TYPE_SPELL_FOCUS && 
                go->isSpawned() &&
                go->GetGOInfo()->spellFocus.focusId == spellInfo->RequiresSpellFocus)
            {
                // Check if player is within the spell focus range
                float dist = (float)((go->GetGOInfo()->spellFocus.dist) / 2);
                float requiredRange = dist - 2.0f; // Add -2.0f buffer for reliable casting
                if (requiredRange <= 0.0f)
                    requiredRange = 0.5f; // Minimum safe distance
                    
                if (go->IsWithinDistInMap(player, requiredRange))
                {
                    foundSpellFocus = true;
                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "QuestItem: Spell " << spellId << " found required spell focus: " << go->GetName() 
                            << " (focus ID " << spellInfo->RequiresSpellFocus << ") at distance " << player->GetDistance(go);
                        botAI->TellMaster(out.str());
                    }
                    break;
                }
                else
                {
                    // Store the spell focus object as a movement target so the bot can move to it
                    botAI->GetAiObjectContext()->GetValue<ObjectGuid>("spell focus target")->Set(go->GetGUID());
                    
                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "QuestItem: Spell " << spellId << " found spell focus " << go->GetName()
                            << " but out of range (" << player->GetDistance(go) << " > " << requiredRange 
                            << "). Moving to spell focus object.";
                        botAI->TellMaster(out.str());
                    }
                    return false; // Return false so quest item isn't used yet, movement action will handle getting closer
                }
            }
        }
        
        if (!foundSpellFocus)
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestItem: Spell " << spellId << " requires spell focus " << spellInfo->RequiresSpellFocus << " but none found nearby";
                botAI->TellMaster(out.str());
            }
            
            // Clear any previous spell focus target since none was found
            botAI->GetAiObjectContext()->GetValue<ObjectGuid>("spell focus target")->Set(ObjectGuid::Empty);
            return false;
        }
    }
    
    // Use the built-in SpellInfo location checking
    SpellCastResult locationResult = spellInfo->CheckLocation(mapId, zoneId, areaId, player);
    if (locationResult != SPELL_CAST_OK)
    {
        // Provide more detailed error messages
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Spell " << spellId << " location check FAILED. ";
            
            switch (locationResult)
            {
                case SPELL_FAILED_INCORRECT_AREA:
                    out << "Wrong area - requires area/zone restriction";
                    break;
                case SPELL_FAILED_REQUIRES_AREA:
                    out << "Requires specific area";
                    break;
                default:
                    out << "Location error code " << locationResult;
                    break;
            }
            
            out << " (Map:" << mapId << " Zone:" << zoneId << " Area:" << areaId;
            if (spellInfo->AreaGroupId > 0)
                out << " RequiredAreaGroup:" << spellInfo->AreaGroupId;
            out << ")";
            botAI->TellMaster(out.str());
        }
        return false; // Player is not in a valid location for this spell
    }

    // Debug output for successful location check
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestItem: Spell " << spellId << " location check PASSED";
        if (spellInfo->AreaGroupId > 0)
            out << " (AreaGroup:" << spellInfo->AreaGroupId << ")";
        botAI->TellMaster(out.str());
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
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Player already has aura " << spellId << " - preventing recast";
            botAI->TellMaster(out.str());
        }
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
                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "QuestItem: Summon creature " << summonEntry << " already exists nearby - preventing recast";
                        botAI->TellMaster(out.str());
                    }
                    return false;
                }
                else
                {
                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "QuestItem: No existing summon " << summonEntry << " found - allowing cast";
                        botAI->TellMaster(out.str());
                    }
                }
            }
        }
    }

    // Check for spell reagent requirements
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; i++)
    {
        if (spellInfo->ReagentCount[i] > 0 && spellInfo->Reagent[i])
        {
            uint32 requiredReagentId = spellInfo->Reagent[i];
            uint32 requiredCount = spellInfo->ReagentCount[i];
            uint32 currentCount = player->GetItemCount(requiredReagentId, false);
            
            if (currentCount < requiredCount)
            {
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    ItemTemplate const* reagentTemplate = sObjectMgr->GetItemTemplate(requiredReagentId);
                    std::ostringstream out;
                    out << "QuestItem: Missing reagent for spell " << spellId << " - need " 
                        << requiredCount << " of " << (reagentTemplate ? reagentTemplate->Name1 : "Unknown Item")
                        << " (ID:" << requiredReagentId << "), have " << currentCount;
                    botAI->TellMaster(out.str());
                }
                return false;
            }
        }
    }

    // Check if the casting item itself will be consumed (spellcharges == -1)
    // If so, we need to ensure we have enough of that item beyond what's needed as reagents
    // Get the item that will be used for casting - passed through the calling context
    // We need to find which quest item is being evaluated for this spell
    uint32 castingItemId = 0;
    
    // Find the casting item by checking which quest item has this spell
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        Item* testItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!testItem)
            continue;
        
        const ItemTemplate* testTemplate = testItem->GetTemplate();
        if (!testTemplate)
            continue;
            
        // Check if this item has the spell we're validating
        for (uint8 j = 0; j < MAX_ITEM_PROTO_SPELLS; ++j)
        {
            if (testTemplate->Spells[j].SpellId == spellId)
            {
                castingItemId = testTemplate->ItemId;
                
                // Check if this item will be consumed (spellcharges == -1)
                if (testTemplate->Spells[j].SpellCharges == -1)
                {
                    uint32 currentCount = player->GetItemCount(castingItemId, false);
                    uint32 totalRequired = 1; // Need at least 1 for casting
                    
                    // If this casting item is also used as a reagent, we need additional copies
                    for (uint8 k = 0; k < MAX_SPELL_REAGENTS; k++)
                    {
                        if (spellInfo->ReagentCount[k] > 0 && spellInfo->Reagent[k] == castingItemId)
                        {
                            totalRequired += spellInfo->ReagentCount[k];
                            break;
                        }
                    }
                    
                    if (currentCount < totalRequired)
                    {
                        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                        {
                            std::ostringstream out;
                            out << "QuestItem: Insufficient casting item for spell " << spellId 
                                << " - need " << totalRequired << " of " << testTemplate->Name1
                                << " (ID:" << castingItemId << "), have " << currentCount 
                                << " (1 for casting + reagents)";
                            botAI->TellMaster(out.str());
                        }
                        return false;
                    }
                }
                break;
            }
        }
        
        if (castingItemId != 0)
            break;
    }
    
    // Also check bag slots for the casting item
    if (castingItemId == 0)
    {
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        {
            Bag* pBag = (Bag*)player->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
            if (!pBag)
                continue;

            for (uint32 slot = 0; slot < pBag->GetBagSize(); ++slot)
            {
                Item* testItem = pBag->GetItemByPos(slot);
                if (!testItem)
                    continue;
                    
                const ItemTemplate* testTemplate = testItem->GetTemplate();
                if (!testTemplate)
                    continue;
                    
                // Check if this item has the spell we're validating
                for (uint8 j = 0; j < MAX_ITEM_PROTO_SPELLS; ++j)
                {
                    if (testTemplate->Spells[j].SpellId == spellId)
                    {
                        castingItemId = testTemplate->ItemId;
                        
                        // Check if this item will be consumed (spellcharges == -1)
                        if (testTemplate->Spells[j].SpellCharges == -1)
                        {
                            uint32 currentCount = player->GetItemCount(castingItemId, false);
                            uint32 totalRequired = 1; // Need at least 1 for casting
                            
                            // If this casting item is also used as a reagent, we need additional copies
                            for (uint8 k = 0; k < MAX_SPELL_REAGENTS; k++)
                            {
                                if (spellInfo->ReagentCount[k] > 0 && spellInfo->Reagent[k] == castingItemId)
                                {
                                    totalRequired += spellInfo->ReagentCount[k];
                                    break;
                                }
                            }
                            
                            if (currentCount < totalRequired)
                            {
                                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                                {
                                    std::ostringstream out;
                                    out << "QuestItem: Insufficient casting item for spell " << spellId 
                                        << " - need " << totalRequired << " of " << testTemplate->Name1
                                        << " (ID:" << castingItemId << "), have " << currentCount 
                                        << " (1 for casting + reagents)";
                                    botAI->TellMaster(out.str());
                                }
                                return false;
                            }
                        }
                        break;
                    }
                }
                
                if (castingItemId != 0)
                    break;
            }
            
            if (castingItemId != 0)
                break;
        }
    }

    // Check for other spell effects that should prevent recasting
    // Future: Add checks for:
    // - SPELL_EFFECT_APPLY_AURA with specific aura types
    // - Transformation effects
    // - Vehicle effects
    // - Item enchantments
    // - etc.

    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestItem: Quest item with spell " << spellId << " can be used";
        botAI->TellMaster(out.str());
    }
    return true;
}

bool QuestItemHelper::IsQuestItemNeeded(Player* player, Item* item, uint32 spellId)
{
    if (!player || !item)
        return false;

    const ItemTemplate* itemTemplate = item->GetTemplate();
    if (!itemTemplate)
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
    
    // NEW APPROACH: Check if this specific quest item is needed for any incomplete quest
    // by validating that the spell could actually provide quest credit
    
    bool itemNeededForActiveQuest = false;
    
    // Check all active incomplete quests
    for (uint32 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = player->GetQuestSlotQuestId(slot);
        if (questId == 0)
            continue;

        QuestStatus questStatus = player->GetQuestStatus(questId);
        if (questStatus != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Checking if item " << itemTemplate->Name1 << " is needed for quest " << questId << " (" << quest->GetTitle() << ")";
            botAI->TellMaster(out.str());
        }

        // Check if this quest has incomplete objectives that could potentially be completed by this item
        bool questNeedsProgress = false;
        
        // Check traditional kill/interact objectives
        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            if (quest->RequiredNpcOrGo[i] == 0)
                continue;
                
            uint32 reqCount = quest->RequiredNpcOrGoCount[i];
            if (reqCount == 0)
                continue;
                
            uint32 currentCount = player->GetQuestSlotCounter(slot, i);
            if (currentCount < reqCount)
            {
                questNeedsProgress = true;
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestItem: Quest " << questId << " objective " << (int)i << " needs progress: " << currentCount << "/" << reqCount;
                    botAI->TellMaster(out.str());
                }
                break;
            }
        }
        
        // Check CAST quest objectives (many quest items use this mechanism)
        if (!questNeedsProgress && quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_CAST))
        {
            for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            {
                uint32 reqCount = quest->RequiredNpcOrGoCount[i];
                if (reqCount == 0)
                    continue;
                    
                uint32 currentCount = player->GetQuestSlotCounter(slot, i);
                if (currentCount < reqCount)
                {
                    questNeedsProgress = true;
                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "QuestItem: CAST quest " << questId << " objective " << (int)i << " needs progress: " << currentCount << "/" << reqCount;
                        botAI->TellMaster(out.str());
                    }
                    break;
                }
            }
        }
        
        // Also check ItemDrop requirements (like Kyle's Gone Missing quest)
        // This handles quests where items are required via ItemDrop rather than RequiredItemId
        for (uint8 i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
        {
            if (quest->ItemDrop[i] == itemTemplate->ItemId)
            {
                questNeedsProgress = true;
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestItem: Item " << itemTemplate->Name1 << " is required for quest " << questId << " (ItemDrop)";
                    botAI->TellMaster(out.str());
                }
                break;
            }
        }
        
        // If this quest needs progress, check if our spell could potentially help
        if (questNeedsProgress)
        {
            // Validate that the spell from this item could actually provide quest credit
            // by checking spell conditions and target availability
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (spellInfo)
            {
                // Check if spell location requirements are met
                if (CheckSpellLocationRequirements(player, spellId))
                {
                    // For self-cast spells, only consider quest items or items with quest-specific properties
                    if (!spellInfo->NeedsExplicitUnitTarget())
                    {
                        // Check if this spell actually has quest-related effects rather than just being a consumable
                        bool isActualQuestItem = false;
                        
                        // Method 1: Check if it's a quest item class
                        if (itemTemplate->Class == ITEM_CLASS_QUEST)
                        {
                            isActualQuestItem = true;
                        }
                        else
                        {
                            // Method 2: Check if the spell has quest-specific effects (summons, quest credit, etc.)
                            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                            {
                                uint32 effect = spellInfo->Effects[i].Effect;
                                if (effect == SPELL_EFFECT_SUMMON_OBJECT_WILD ||    // Creates gameobjects (common for quest items)
                                    effect == SPELL_EFFECT_SUMMON_OBJECT_SLOT1 ||
                                    effect == SPELL_EFFECT_SUMMON_OBJECT_SLOT2 ||
                                    effect == SPELL_EFFECT_SUMMON_OBJECT_SLOT3 ||
                                    effect == SPELL_EFFECT_SUMMON_OBJECT_SLOT4 ||
                                    effect == SPELL_EFFECT_QUEST_COMPLETE ||       // Directly completes quests
                                    effect == SPELL_EFFECT_SEND_EVENT ||           // Triggers quest events
                                    effect == SPELL_EFFECT_KILL_CREDIT ||          // Gives kill credit
                                    effect == SPELL_EFFECT_CREATE_ITEM)            // Creates quest items
                                {
                                    isActualQuestItem = true;
                                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                                    {
                                        std::ostringstream out;
                                        out << "QuestItem: " << itemTemplate->Name1 << " has quest effect " << effect << " on spell " << spellId;
                                        botAI->TellMaster(out.str());
                                    }
                                    break;
                                }
                            }
                        }
                        
                        if (isActualQuestItem)
                        {
                            itemNeededForActiveQuest = true;
                            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                            {
                                std::ostringstream out;
                                out << "QuestItem: Self-cast item " << itemTemplate->Name1 << " might be needed for quest " << questId;
                                botAI->TellMaster(out.str());
                            }
                            break;
                        }
                        else if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                        {
                            std::ostringstream out;
                            out << "QuestItem: Skipping regular consumable " << itemTemplate->Name1 << " - no quest effects (class:" << itemTemplate->Class << " spell:" << spellId << ")";
                            botAI->TellMaster(out.str());
                        }
                    }
                    else
                    {
                        // For targeted spells, check if there are any valid targets that could provide quest credit
                        // We do a quick check for nearby creatures that match quest objectives
                        bool foundPotentialTarget = false;
                        
                        // Check traditional objectives
                        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                        {
                            uint32 requiredEntry = quest->RequiredNpcOrGo[i];
                            if (requiredEntry == 0)
                                continue;
                                
                            uint32 reqCount = quest->RequiredNpcOrGoCount[i];
                            uint32 currentCount = player->GetQuestSlotCounter(slot, i);
                            
                            if (currentCount >= reqCount)
                                continue; // This objective is complete
                            
                            // Check if we can find creatures matching this entry nearby
                            if (IsNearCreature(botAI, requiredEntry, sPlayerbotAIConfig->grindDistance, false))
                            {
                                foundPotentialTarget = true;
                                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                                {
                                    std::ostringstream out;
                                    out << "QuestItem: Found potential target (entry " << requiredEntry << ") for quest " << questId << " objective " << (int)i;
                                    botAI->TellMaster(out.str());
                                }
                                break;
                            }
                            
                            // ALSO check for creatures that give KillCredit for this entry (trigger creature system)
                            if (CheckForKillCreditCreatures(botAI, requiredEntry))
                            {
                                foundPotentialTarget = true;
                                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                                {
                                    std::ostringstream out;
                                    out << "QuestItem: Found KillCredit target for entry " << requiredEntry << " for quest " << questId << " objective " << (int)i;
                                    botAI->TellMaster(out.str());
                                }
                                break;
                            }
                        }
                        
                        if (foundPotentialTarget)
                        {
                            itemNeededForActiveQuest = true;
                            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                            {
                                std::ostringstream out;
                                out << "QuestItem: Targeted item " << itemTemplate->Name1 << " might be needed for quest " << questId;
                                botAI->TellMaster(out.str());
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    if (!itemNeededForActiveQuest)
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Item " << itemTemplate->Name1 << " not needed for any active quest";
            botAI->TellMaster(out.str());
        }
        return false;
    }

    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestItem: Item " << itemTemplate->Name1 << " is needed for active quest progress";
        botAI->TellMaster(out.str());
    }
    return true;
}

bool QuestItemHelper::WouldProvideQuestCredit(Player* player, Unit* target, uint32 spellId)
{
    if (!player || !target)
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
    uint32 targetEntry = target->GetEntry();
    
    // Check all active quests to see if this target would provide credit
    for (uint32 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = player->GetQuestSlotQuestId(slot);
        if (questId == 0)
            continue;

        QuestStatus questStatus = player->GetQuestStatus(questId);
        if (questStatus != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Checking quest " << questId << " (" << quest->GetTitle() << ") - CAST flag:" 
                << (quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_CAST) ? "YES" : "NO");
            botAI->TellMaster(out.str());
        }

        // Check if this is a casting-based quest (many quest items use this instead of creature kills)
        if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_CAST))
        {
            // For casting quests, we still need to validate spell conditions first
            // Only check progress if the target would be valid for the spell conditions
            
            bool questNeedsProgress = false;
            for (uint8 j = 0; j < QUEST_OBJECTIVES_COUNT; ++j)
            {
                uint32 requiredCount = quest->RequiredNpcOrGoCount[j];
                if (requiredCount == 0)
                    continue;
                    
                uint32 currentCount = player->GetQuestSlotCounter(slot, j);
                
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestItem: CAST quest " << questId << " objective " << (int)j << " progress: " << currentCount << "/" << requiredCount;
                    botAI->TellMaster(out.str());
                }
                
                if (currentCount < requiredCount)
                {
                    questNeedsProgress = true;
                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "QuestItem: CAST quest " << questId << " needs " << (requiredCount - currentCount) << " more casts";
                        botAI->TellMaster(out.str());
                    }
                    break;
                }
            }
            
            // For CAST quests, we found the quest needs progress
            // But we should not return true here - the caller should validate spell conditions first
            // This function is called AFTER spell validation, so if we reach here, conditions passed
            if (questNeedsProgress)
            {
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestItem: Target " << target->GetName() << " WOULD provide quest credit for CAST quest " << questId;
                    botAI->TellMaster(out.str());
                }
                return true;
            }
        }
        
        // Check traditional creature/gameobject kill objectives
        for (uint8 j = 0; j < QUEST_OBJECTIVES_COUNT; ++j)
        {
            uint32 requiredEntry = quest->RequiredNpcOrGo[j];
            if (requiredEntry == 0)
                continue;
                
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestItem: Quest " << questId << " objective " << j << " requires entry:" << requiredEntry;
                botAI->TellMaster(out.str());
            }
                
            // Check if this objective matches our target entry
            if (requiredEntry == targetEntry)
            {
                uint32 requiredCount = quest->RequiredNpcOrGoCount[j];
                uint32 currentCount = player->GetQuestSlotCounter(slot, j);
                
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestItem: Quest " << questId << " objective " << j << " - target entry:" << targetEntry 
                        << " current:" << currentCount << " required:" << requiredCount;
                    botAI->TellMaster(out.str());
                }
                
                // If current count is less than required, this target would provide credit
                if (currentCount < requiredCount)
                {
                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "QuestItem: Target " << target->GetName() << " WOULD provide quest credit for quest " << questId;
                        botAI->TellMaster(out.str());
                    }
                    return true;
                }
                else
                {
                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "QuestItem: Target " << target->GetName() << " objective already complete (" << currentCount << "/" << requiredCount << ")";
                        botAI->TellMaster(out.str());
                    }
                }
            }
        }
    }

    // No active quest would get credit from this target
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestItem: Target " << target->GetName() << " (entry:" << targetEntry << ") would NOT provide quest credit";
        botAI->TellMaster(out.str());
    }
    return false;
}

// Shared static map to track quest item usage per target GUID
static std::map<std::string, time_t> s_questItemUsageTracker;

// PendingQuestItemCast struct now defined in QuestItemHelper.h
// Individual bot maps are now stored as member variables in PlayerbotAI

bool QuestItemHelper::CanUseQuestItemOnTarget(PlayerbotAI* botAI, WorldObject* target, uint32 spellId)
{
    if (!botAI || !target)
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    // Create a unique key for spell + target combination (bot GUID no longer needed since each bot has its own map)
    std::string key = std::to_string(spellId) + "_" + target->GetGUID().ToString();
    
    time_t currentTime = time(nullptr);
    
    // 30 second cooldown per target as suggested
    const time_t COOLDOWN_SECONDS = 30;
    
    // Check if this bot has used this quest item on this target recently
    auto it = s_questItemUsageTracker.find(key);
    if (it != s_questItemUsageTracker.end())
    {
        time_t lastUsed = it->second;
        time_t timeSinceLastUse = currentTime - lastUsed;
        
        if (timeSinceLastUse < COOLDOWN_SECONDS)
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestItem: Target " << target->GetName() << " (GUID:" << target->GetGUID().ToString() 
                    << ") used " << timeSinceLastUse << "s ago - " << (COOLDOWN_SECONDS - timeSinceLastUse) << "s remaining";
                botAI->TellMaster(out.str());
            }
            return false;
        }
    }
    
    // Also check if there's a pending cast for this target
    auto pendingIt = botAI->GetPendingQuestItemCasts().find(key);
    if (pendingIt != botAI->GetPendingQuestItemCasts().end())
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Target " << target->GetName() << " (GUID:" << target->GetGUID().ToString() 
                << ") has pending cast - waiting for server response";
            botAI->TellMaster(out.str());
        }
        return false;
    }
    
    // Target can be used - but don't record usage yet (that happens when spell is actually cast)
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestItem: Target " << target->GetName() << " (GUID:" << target->GetGUID().ToString() 
            << ") available for use";
        botAI->TellMaster(out.str());
    }
    
    return true;
}

void QuestItemHelper::RecordQuestItemUsage(PlayerbotAI* botAI, WorldObject* target, uint32 spellId)
{
    if (!botAI || !target)
        return;

    Player* bot = botAI->GetBot();
    if (!bot)
        return;

    // Create a unique key for this bot + spell + target combination  
    std::string key = bot->GetGUID().ToString() + "_" + std::to_string(spellId) + "_" + target->GetGUID().ToString();
    
    time_t currentTime = time(nullptr);
    
    // Record the usage time in the shared map
    s_questItemUsageTracker[key] = currentTime;
    
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestItem: Target " << target->GetName() << " (GUID:" << target->GetGUID().ToString() 
            << ") marked as used - 30s cooldown started";
        botAI->TellMaster(out.str());
    }
}

void QuestItemHelper::RecordPendingQuestItemCast(PlayerbotAI* botAI, WorldObject* target, uint32 spellId)
{
    if (!botAI || !target)
        return;

    Player* bot = botAI->GetBot();
    if (!bot)
        return;

    // Create a unique key for spell + target combination (bot GUID no longer needed since each bot has its own map)
    std::string key = std::to_string(spellId) + "_" + target->GetGUID().ToString();
    
    time_t currentTime = time(nullptr);
    
    // Store the pending cast
    PendingQuestItemCast pending;
    pending.key = key;
    pending.target = target;
    pending.targetGuid = target->GetGUID();
    pending.castTime = currentTime;
    
    botAI->GetPendingQuestItemCasts()[key] = pending;
    
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestItem: Recording pending cast on " << target->GetName() 
            << " (GUID:" << target->GetGUID().ToString() << ") with spell " << spellId;
        botAI->TellMaster(out.str());
    }
}

void QuestItemHelper::OnQuestItemSpellFailed(PlayerbotAI* botAI, uint32 spellId)
{
    if (!botAI)
        return;

    Player* bot = botAI->GetBot();
    if (!bot)
        return;

    // Find and remove any pending casts for this spell from this bot's map
    std::string spellIdStr = std::to_string(spellId);
    
    auto it = botAI->GetPendingQuestItemCasts().begin();
    while (it != botAI->GetPendingQuestItemCasts().end())
    {
        const std::string& key = it->first;
        
        // Check if this pending cast is for this spell (key starts with spellId_)
        if (key.find(spellIdStr + "_") == 0)
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestItem: Spell " << spellId << " failed - removing pending cast for target " 
                    << it->second.targetGuid.ToString();
                botAI->TellMaster(out.str());
            }
            
            it = botAI->GetPendingQuestItemCasts().erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void QuestItemHelper::ProcessPendingQuestItemCasts(PlayerbotAI* botAI)
{
    if (!botAI)
        return;

    time_t currentTime = time(nullptr);
    const time_t PENDING_TIMEOUT = 5; // 5 seconds timeout for pending casts
    
    auto it = botAI->GetPendingQuestItemCasts().begin();
    while (it != botAI->GetPendingQuestItemCasts().end())
    {
        const PendingQuestItemCast& pending = it->second;
        time_t timeSinceCast = currentTime - pending.castTime;
        
        if (timeSinceCast >= PENDING_TIMEOUT)
        {
            // Cast is old enough - assume it succeeded and convert to cooldown
            // Look up target by GUID instead of using potentially dangling pointer
            Unit* target = nullptr;
            if (pending.targetGuid)
            {
                target = ObjectAccessor::GetUnit(*botAI->GetBot(), pending.targetGuid);
            }
            
            if (target)
            {
                // Extract spell ID from key for recording usage
                size_t firstUnderscore = pending.key.find('_');
                size_t secondUnderscore = pending.key.find('_', firstUnderscore + 1);
                if (firstUnderscore != std::string::npos && secondUnderscore != std::string::npos)
                {
                    std::string spellIdStr = pending.key.substr(firstUnderscore + 1, secondUnderscore - firstUnderscore - 1);
                    uint32 spellId = std::stoul(spellIdStr);
                    
                    // Record the cooldown
                    s_questItemUsageTracker[pending.key] = currentTime;
                    
                    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                    {
                        std::ostringstream out;
                        out << "QuestItem: Pending cast timeout - assuming success, starting cooldown for " 
                            << target->GetName() << " (GUID:" << target->GetGUID().ToString() << ")";
                        botAI->TellMaster(out.str());
                    }
                }
            }
            
            it = botAI->GetPendingQuestItemCasts().erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool QuestItemHelper::CheckForKillCreditCreatures(PlayerbotAI* botAI, uint32 killCreditEntry)
{
    if (!botAI)
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    // Get nearby NPCs and check if any give KillCredit for the required entry
    GuidVector npcs = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
    
    for (ObjectGuid guid : npcs)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || unit->GetTypeId() != TYPEID_UNIT)
            continue;

        Creature* creature = unit->ToCreature();
        if (!creature)
            continue;

        CreatureTemplate const* creatureTemplate = creature->GetCreatureTemplate();
        if (!creatureTemplate)
            continue;

        float distance = bot->GetDistance(creature);
        if (distance > sPlayerbotAIConfig->grindDistance)
            continue;

        // Check if this creature gives KillCredit for our required entry
        if (creatureTemplate->KillCredit[0] == killCreditEntry || creatureTemplate->KillCredit[1] == killCreditEntry)
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestItem: Found KillCredit creature " << creature->GetName() << " (entry:" << creature->GetEntry() 
                    << ") gives credit for " << killCreditEntry << " at distance " << distance;
                botAI->TellMaster(out.str());
            }
            return true;
        }
    }

    return false;
}

WorldObject* QuestItemHelper::FindTargetUsingSpellConditions(PlayerbotAI* botAI, uint32 spellId)
{
    if (!botAI)
        return nullptr;
        
    Player* bot = botAI->GetBot();
    if (!bot)
        return nullptr;
        
    // Query conditions for spell implicit targets (type 13) and spell casting (type 17)
    // Type 13 is used for some spells, type 17 is used for quest item spells like parachutes
    ConditionList conditions = sConditionMgr->GetConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_SPELL_IMPLICIT_TARGET, spellId);
    ConditionList spellConditions = sConditionMgr->GetConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_SPELL, spellId);

    // Merge both condition lists
    conditions.insert(conditions.end(), spellConditions.begin(), spellConditions.end());

    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestItem: Spell " << spellId << " has " << conditions.size() << " conditions (type 13 + type 17)";
        botAI->TellMaster(out.str());
    }
    
    for (Condition const* condition : conditions)
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Found condition type " << condition->ConditionType << " values: " << condition->ConditionValue1 << ", " << condition->ConditionValue2 << ", " << condition->ConditionValue3;
            botAI->TellMaster(out.str());
        }
        
        if (condition->ConditionType == CONDITION_CREATURE_TYPE)
        {
            uint32 requiredCreatureType = condition->ConditionValue1;
            uint32 requiredCreatureEntry = condition->ConditionValue2;
            
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "QuestItem: Looking for creature type " << requiredCreatureType << " entry " << requiredCreatureEntry;
                botAI->TellMaster(out.str());
            }
            
            // Search for the specific creature entry
            GuidVector npcs = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
            
            for (ObjectGuid guid : npcs)
            {
                Unit* target = botAI->GetUnit(guid);
                if (!target || target->GetEntry() != requiredCreatureEntry)
                    continue;
                    
                // Check distance using spell range with buffer for reliable casting
                float distance = bot->GetDistance(target);
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                float maxDistance = spellInfo ? (spellInfo->GetMaxRange() - 2.0f) : (INTERACTION_DISTANCE - 2.0f);
                if (maxDistance <= 0.0f)
                    maxDistance = 1.0f; // Minimum safe distance
                
                if (distance > maxDistance)
                    continue;
                    
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestItem: Found required creature " << target->GetName() << " (entry " << requiredCreatureEntry << ") at distance " << distance << " (max: " << maxDistance << ")";
                    botAI->TellMaster(out.str());
                }
                
                return target;
            }
        }
    }
    
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        botAI->TellMaster("QuestItem: No valid targets found using spell conditions");
        
    return nullptr;
}

bool QuestItemHelper::IsOpenLockSpell(uint32 spellId)
{
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return false;
    
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        if (spellInfo->Effects[i].Effect == SPELL_EFFECT_OPEN_LOCK)
            return true;
    }
    
    return false;
}

WorldObject* QuestItemHelper::FindGameObjectForLockSpell(PlayerbotAI* botAI, uint32 spellId, Item* questItem)
{
    if (!botAI || !questItem || !IsOpenLockSpell(spellId))
        return nullptr;
        
    Player* bot = botAI->GetBot();
    if (!bot)
        return nullptr;
    
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestItem: Looking for gameobject that can be unlocked with item " << questItem->GetEntry();
        botAI->TellMaster(out.str());
    }
    
    // Search nearby gameobjects for matching locks
    GuidVector nearbyGameObjects = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest game objects")->Get();
    
    for (ObjectGuid goGuid : nearbyGameObjects)
    {
        GameObject* go = botAI->GetGameObject(goGuid);
        if (!go || !go->isSpawned())
            continue;
            
        // Check if bot is within quest search range (larger than interaction range for movement targeting)
        float searchRange = sPlayerbotAIConfig->grindDistance; // Use grind distance for quest target search
        float distance = bot->GetDistance(go);
        
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Distance to " << go->GetName() << ": " 
                << std::fixed << std::setprecision(2) << distance << " yards (search range: " << searchRange << ")";
            botAI->TellMaster(out.str());
        }
        
        if (distance > searchRange)
            continue;
            
        uint32 lockId = go->GetGOInfo()->GetLockId();
        if (!lockId)
            continue;
            
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "QuestItem: Checking gameobject " << go->GetName() << " (entry " << go->GetEntry() << ") with lock " << lockId;
            botAI->TellMaster(out.str());
        }
        
        LockEntry const* lock = sLockStore.LookupEntry(lockId);
        if (!lock)
        {
            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                botAI->TellMaster("QuestItem: Lock entry not found in DBC");
            continue;
        }
        
        // Check if this gameobject's lock requires our quest item
        for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
        {
            if (lock->Type[i] == LOCK_KEY_ITEM && 
                lock->Index[i] == questItem->GetEntry())
            {
                if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "QuestItem: Found matching gameobject " << go->GetName() << " that requires item " << questItem->GetEntry();
                    botAI->TellMaster(out.str());
                }
                
                // Return as WorldObject* - can be cast to GameObject* in the action
                return static_cast<WorldObject*>(go);
            }
        }
    }
    
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        botAI->TellMaster("QuestItem: No matching gameobject found for lock spell");
    
    return nullptr;
}

