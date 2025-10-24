/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "LootObjectStack.h"

#include "LootMgr.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "Unit.h"
#include <iomanip>
#include <sstream>

#define MAX_LOOT_OBJECT_COUNT 200

LootTarget::LootTarget(ObjectGuid guid) : guid(guid), asOfTime(time(nullptr)) {}

LootTarget::LootTarget(LootTarget const& other)
{
    guid = other.guid;
    asOfTime = other.asOfTime;
}

LootTarget& LootTarget::operator=(LootTarget const& other)
{
    if ((void*)this == (void*)&other)
        return *this;

    guid = other.guid;
    asOfTime = other.asOfTime;

    return *this;
}

bool LootTarget::operator<(LootTarget const& other) const { return guid < other.guid; }

void LootTargetList::shrink(time_t fromTime)
{
    for (std::set<LootTarget>::iterator i = begin(); i != end();)
    {
        if (i->asOfTime <= fromTime)
            erase(i++);
        else
            ++i;
    }
}

LootObject::LootObject(Player* bot, ObjectGuid guid) : guid(), skillId(SKILL_NONE), reqSkillValue(0), reqItem(0)
{
    Refresh(bot, guid);
}

void LootObject::Refresh(Player* bot, ObjectGuid lootGUID)
{
    skillId = SKILL_NONE;
    reqSkillValue = 0;
    reqItem = 0;
    isAccessible = false;
    guid.Clear();

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
    {
        return;
    }

    bool debugLoot = botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT);

    Creature* creature = botAI->GetCreature(lootGUID);
    if (creature && creature->getDeathState() == DeathState::Corpse)
    {
        if (debugLoot)
        {
            std::ostringstream out;
            out << "LootRefresh: Evaluating creature " << creature->GetName() 
                << " (Entry: " << creature->GetEntry() << ")";
            botAI->TellMaster(out.str());
        }

        if (creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
        {
            guid = lootGUID;
            isAccessible = true;  // FIX: Mark creature loot as accessible
            if (debugLoot)
                botAI->TellMaster("LootRefresh: Creature is lootable");
        }
        else
        {
            if (debugLoot)
                botAI->TellMaster("LootRefresh: Creature not lootable - skipping");
            return;
        }

        if (creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE))
        {
            skillId = creature->GetCreatureTemplate()->GetRequiredLootSkill();
            uint32 targetLevel = creature->GetLevel();
            reqSkillValue = targetLevel < 10 ? 1 : targetLevel < 20 ? (targetLevel - 10) * 10 : targetLevel * 5;
            if (botAI->HasSkill((SkillType)skillId) && bot->GetSkillValue(skillId) >= reqSkillValue)
            {
                guid = lootGUID;
                isAccessible = true;  // FIX: Mark skinnable creature as accessible
                if (debugLoot)
                {
                    std::ostringstream out;
                    out << "LootRefresh: Creature skinnable with skill " << skillId
                        << " (req: " << reqSkillValue << ", have: " << bot->GetSkillValue(skillId) << ")";
                    botAI->TellMaster(out.str());
                }
            }
            else if (debugLoot)
            {
                std::ostringstream out;
                out << "LootRefresh: Cannot skin - missing skill " << skillId 
                    << " (req: " << reqSkillValue << ", have: " 
                    << (botAI->HasSkill((SkillType)skillId) ? std::to_string(bot->GetSkillValue(skillId)) : "0") << ")";
                botAI->TellMaster(out.str());
            }
        }

        return;
    }

    GameObject* go = botAI->GetGameObject(lootGUID);
    if (go && go->isSpawned() && go->GetGoState() == GO_STATE_READY)
    {
        if (debugLoot)
        {
            std::ostringstream out;
            out << "LootRefresh: Evaluating gameobject " << go->GetName() 
                << " (Entry: " << go->GetEntry() << ", Type: " << go->GetGoType() << ")";
            botAI->TellMaster(out.str());
        }

        bool onlyHasQuestItems = true;
        bool hasAnyQuestItems = false;
        bool hasNeededQuestItem = false;
        bool hasAnyLootableItem = false;  // Track if there's at least one item bot can loot

        GameObjectQuestItemList const* items = sObjectMgr->GetGameObjectQuestItemList(go->GetEntry());
        for (size_t i = 0; i < MAX_GAMEOBJECT_QUEST_ITEMS; i++)
        {
            if (!items || i >= items->size())
                break;

            uint32 itemId = uint32((*items)[i]);
            if (!itemId)
                continue;

            hasAnyQuestItems = true;

            if (IsNeededForQuest(bot, itemId))
            {
                hasNeededQuestItem = true;
                this->guid = lootGUID;
                if (debugLoot)
                {
                    const ItemTemplate* proto = sObjectMgr->GetItemTemplate(itemId);
                    std::ostringstream out;
                    out << "LootRefresh: Found needed quest item " 
                        << (proto ? proto->Name1 : "Unknown") << " (ID: " << itemId << ")";
                    botAI->TellMaster(out.str());
                }
                break;
            }

            const ItemTemplate* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto)
                continue;

            if (proto->Class != ITEM_CLASS_QUEST)
            {
                onlyHasQuestItems = false;
            }
        }

        // If gameobject has only quest items that bot doesn't need, skip it.
        if (hasAnyQuestItems && onlyHasQuestItems && !hasNeededQuestItem)
        {
            if (debugLoot)
                botAI->TellMaster("LootRefresh: Gameobject has only unneeded quest items - skipping");
            return;
        }

        // Retrieve the correct loot table entry
        uint32 lootEntry = go->GetGOInfo()->GetLootId();
        if (lootEntry == 0)
        {
            return;
        }

        // Check the main loot template
        if (const LootTemplate* lootTemplate = LootTemplates_Gameobject.GetLootFor(lootEntry))
        {
            Loot loot;
            lootTemplate->Process(loot, LootTemplates_Gameobject, 1, bot);

            for (const LootItem& item : loot.items)
            {
                uint32 itemId = item.itemid;
                if (!itemId)
                    continue;

                const ItemTemplate* proto = sObjectMgr->GetItemTemplate(itemId);
                if (!proto)
                    continue;

                if (proto->Class != ITEM_CLASS_QUEST)
                {
                    onlyHasQuestItems = false;
                }

                // FIX: Check if bot already has MaxCount of this item (Unique limit)
                uint32 maxCount = proto->MaxCount;
                if (maxCount == 0 || !bot->HasItemCount(itemId, maxCount, true))
                {
                    // Bot doesn't have max count, so this item is lootable
                    hasAnyLootableItem = true;
                    if (debugLoot)
                    {
                        std::ostringstream out;
                        out << "LootRefresh: Found lootable item " << proto->Name1 << " (ID: " << itemId << ")";
                        if (maxCount > 0)
                            out << " - bot has " << bot->GetItemCount(itemId, true) << "/" << maxCount;
                        botAI->TellMaster(out.str());
                    }
                }
                else if (debugLoot)
                {
                    std::ostringstream out;
                    out << "LootRefresh: Skipping item " << proto->Name1 << " (ID: " << itemId << ") - already have max (" << maxCount << ")";
                    botAI->TellMaster(out.str());
                }

                // If this item references another loot table, process it
                if (const LootTemplate* refLootTemplate = LootTemplates_Reference.GetLootFor(itemId))
                {
                    Loot refLoot;
                    refLootTemplate->Process(refLoot, LootTemplates_Reference, 1, bot);

                    for (const LootItem& refItem : refLoot.items)
                    {
                        uint32 refItemId = refItem.itemid;
                        if (!refItemId)
                            continue;

                        const ItemTemplate* refProto = sObjectMgr->GetItemTemplate(refItemId);
                        if (!refProto)
                            continue;

                        if (refProto->Class != ITEM_CLASS_QUEST)
                        {
                            onlyHasQuestItems = false;
                        }

                        // FIX: Check MaxCount for referenced items too
                        uint32 refMaxCount = refProto->MaxCount;
                        if (refMaxCount == 0 || !bot->HasItemCount(refItemId, refMaxCount, true))
                        {
                            hasAnyLootableItem = true;
                            if (debugLoot)
                            {
                                std::ostringstream out;
                                out << "LootRefresh: Found lootable referenced item " << refProto->Name1 << " (ID: " << refItemId << ")";
                                if (refMaxCount > 0)
                                    out << " - bot has " << bot->GetItemCount(refItemId, true) << "/" << refMaxCount;
                                botAI->TellMaster(out.str());
                            }
                        }
                        else if (debugLoot)
                        {
                            std::ostringstream out;
                            out << "LootRefresh: Skipping referenced item " << refProto->Name1 << " (ID: " << refItemId << ") - already have max (" << refMaxCount << ")";
                            botAI->TellMaster(out.str());
                        }
                    }
                }
            }
        }

        // If gameobject has only quest items that bot doesn't need, skip it.
        if (hasAnyQuestItems && onlyHasQuestItems && !hasNeededQuestItem)
        {
            if (debugLoot)
                botAI->TellMaster("LootRefresh: Gameobject has only unneeded quest items - skipping");
            return;
        }

        // FIX: If gameobject has no lootable items (all maxcount reached), skip it.
        if (!hasAnyLootableItem && !hasNeededQuestItem)
        {
            if (debugLoot)
                botAI->TellMaster("LootRefresh: Gameobject has no lootable items (all unique limits reached) - skipping");
            return;
        }

        // Otherwise, loot it.
        guid = lootGUID;

        uint32 goId = go->GetEntry();
        uint32 lockId = go->GetGOInfo()->GetLockId();
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
        {
            if (debugLoot)
                botAI->TellMaster("LootRefresh: Gameobject accepted - no lock info");
            return;
        }

        if (debugLoot)
        {
            std::ostringstream out;
            out << "LootRefresh: Gameobject has lock ID " << lockId;
            botAI->TellMaster(out.str());
        }

        // Find the most permissive lock (easiest to satisfy) - locks work with OR logic
        bool foundAccessibleLock = false;
        uint32 bestSkillId = SKILL_NONE; // Start with no skill requirement
        uint32 bestReqSkillValue = UINT32_MAX; // Start with impossible requirement
        uint32 bestReqItem = 0;
        
        for (uint8 i = 0; i < 8; ++i)
        {
            switch (lockInfo->Type[i])
            {
                case LOCK_KEY_ITEM:
                    if (lockInfo->Index[i] > 0)
                    {
                        if (debugLoot)
                        {
                            const ItemTemplate* keyProto = sObjectMgr->GetItemTemplate(lockInfo->Index[i]);
                            std::ostringstream out;
                            out << "LootRefresh: Lock option " << (i+1) << " - requires key item " 
                                << (keyProto ? keyProto->Name1 : "Unknown") << " (ID: " << lockInfo->Index[i] << ")";
                            botAI->TellMaster(out.str());
                        }
                        
                        // If bot has this key item, this is the best option (no skill required)
                        if (bot->HasItemCount(lockInfo->Index[i], 1))
                        {
                            bestSkillId = SKILL_NONE;
                            bestReqSkillValue = 0;
                            bestReqItem = lockInfo->Index[i];
                            foundAccessibleLock = true;
                            if (debugLoot)
                                botAI->TellMaster("LootRefresh: Bot has required key - this is best option");
                            break; // Key access is always best, stop checking other locks
                        }
                        else
                        {
                            // Consider this key option if no better option found yet
                            if (!foundAccessibleLock)
                            {
                                bestReqItem = lockInfo->Index[i];
                                bestSkillId = SKILL_NONE;
                                bestReqSkillValue = 0;
                            }
                        }
                    }
                    break;

                case LOCK_KEY_SKILL:
                    {
                        LockType lockType = LockType(lockInfo->Index[i]);
                        SkillType mappedSkill = SkillByLockType(lockType);
                        
                        if (mappedSkill > SKILL_NONE)
                        {
                            uint32 reqSkill = std::max((uint32)1, lockInfo->Skill[i]);
                            if (debugLoot)
                            {
                                std::ostringstream out;
                                out << "LootRefresh: Lock option " << (i+1) << " - requires skill " << mappedSkill 
                                    << " (level " << reqSkill << ")";
                                botAI->TellMaster(out.str());
                            }
                            
                            // Check if bot can satisfy this skill requirement
                            if (botAI->HasSkill((SkillType)mappedSkill) && bot->GetSkillValue(mappedSkill) >= reqSkill)
                            {
                                // This lock can be satisfied - choose it if it's better than current best
                                if (!foundAccessibleLock || mappedSkill == SKILL_NONE || 
                                    (bestSkillId != SKILL_NONE && reqSkill < bestReqSkillValue))
                                {
                                    bestSkillId = mappedSkill;
                                    bestReqSkillValue = reqSkill;
                                    bestReqItem = 0;
                                    foundAccessibleLock = true;
                                    if (debugLoot)
                                        botAI->TellMaster("LootRefresh: Bot can satisfy skill requirement - considering this option");
                                }
                            }
                            else if (debugLoot)
                            {
                                std::ostringstream out;
                                out << "LootRefresh: Bot cannot satisfy skill " << mappedSkill 
                                    << " (have " << (botAI->HasSkill((SkillType)mappedSkill) ? std::to_string(bot->GetSkillValue(mappedSkill)) : "0")
                                    << ", need " << reqSkill << ")";
                                botAI->TellMaster(out.str());
                            }
                        }
                        else if (IsAccessibleLockType(lockType))
                        {
                            if (debugLoot)
                            {
                                std::ostringstream out;
                                out << "LootRefresh: Lock option " << (i+1) << " - accessible lock type " << lockType << " (no skill required)";
                                botAI->TellMaster(out.str());
                            }
                            
                            // No skill required - this is always accessible and beats skill requirements
                            bestSkillId = SKILL_NONE;
                            bestReqSkillValue = 0;
                            bestReqItem = 0;
                            foundAccessibleLock = true;
                            if (debugLoot)
                                botAI->TellMaster("LootRefresh: No skill lock type - this is accessible");
                            break; // No-skill access is very good, but key access would be better
                        }
                        else if (debugLoot)
                        {
                            std::ostringstream out;
                            out << "LootRefresh: Lock option " << (i+1) << " - inaccessible lock type " << lockType;
                            botAI->TellMaster(out.str());
                        }
                    }
                    break;

                default:
                    // LOCK_KEY_NONE (0) and other undefined types - match server behavior
                    if (lockInfo->Type[i] != LOCK_KEY_SKILL)
                    {
                        // SERVER BEHAVIOR: Stop processing remaining lock options for non-skill types
                        // This matches GameObject::GetSpellForLock() early break logic
                        if (debugLoot && lockInfo->Type[i] != 0)
                        {
                            std::ostringstream out;
                            out << "LootRefresh: Lock option " << (i+1) << " - non-skill type " << lockInfo->Type[i] 
                                << " (stopping processing like server)";
                            botAI->TellMaster(out.str());
                        }
                        break; // Stop processing remaining slots like server does
                    }
                    
                    if (debugLoot && lockInfo->Type[i] != 0)
                    {
                        std::ostringstream out;
                        out << "LootRefresh: Lock option " << (i+1) << " - unknown lock type " << lockInfo->Type[i];
                        botAI->TellMaster(out.str());
                    }
                    break;
            }
            
            // If we found the best possible option (no lock or key access), stop checking
            if (foundAccessibleLock && bestSkillId == SKILL_NONE && bestReqItem == 0)
                break;
        }
        
        // Check if we found any actual lock requirements (not just Type 0 entries)
        bool hasActualRequirements = false;
        for (uint8 i = 0; i < 8; ++i)
        {
            if (lockInfo->Type[i] == LOCK_KEY_ITEM || lockInfo->Type[i] == LOCK_KEY_SKILL || lockInfo->Type[i] == LOCK_KEY_SPELL)
            {
                hasActualRequirements = true;
                break;
            }
        }
        
        // Apply the best lock option found, or allow access if no actual requirements exist
        if (foundAccessibleLock || !hasActualRequirements)
        {
            skillId = bestSkillId;
            reqSkillValue = bestReqSkillValue;
            reqItem = bestReqItem;
            isAccessible = true;
            guid = lootGUID;
            
            if (debugLoot)
            {
                std::ostringstream out;
                if (!hasActualRequirements)
                {
                    out << "LootRefresh: No actual lock requirements found (only Type 0 entries) - allowing access";
                }
                else
                {
                    out << "LootRefresh: Selected best lock option - ";
                    if (bestReqItem > 0)
                    {
                        const ItemTemplate* keyProto = sObjectMgr->GetItemTemplate(bestReqItem);
                        out << "key item " << (keyProto ? keyProto->Name1 : "Unknown") << " (ID: " << bestReqItem << ")";
                    }
                    else if (bestSkillId == SKILL_NONE)
                        out << "no requirements";
                    else
                        out << "skill " << bestSkillId << " (level " << bestReqSkillValue << ")";
                }
                botAI->TellMaster(out.str());
            }
        }
        else
        {
            // Object is not accessible - bot cannot satisfy lock requirements
            isAccessible = false;
            if (debugLoot)
            {
                botAI->TellMaster("LootRefresh: No accessible lock options found - bot cannot loot this object");
            }
        }
    }
}

bool LootObject::IsNeededForQuest(Player* bot, uint32 itemId)
{
    for (int qs = 0; qs < MAX_QUEST_LOG_SIZE; ++qs)
    {
        uint32 questId = bot->GetQuestSlotQuestId(qs);
        if (questId == 0)
            continue;

        QuestStatusData& qData = bot->getQuestStatusMap()[questId];
        if (qData.Status != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* qInfo = sObjectMgr->GetQuestTemplate(questId);
        if (!qInfo)
            continue;

        for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            if (!qInfo->RequiredItemCount[i] || (qInfo->RequiredItemCount[i] - qData.ItemCount[i]) <= 0)
                continue;

            if (qInfo->RequiredItemId[i] != itemId)
                continue;

            return true;
        }
    }

    return false;
}

bool LootObject::IsAccessibleLockType(LockType lockType)
{
    // These lock types don't require specific skills but should be accessible to players
    // Based on analysis of LockType.dbc and AzerothCore's SkillByLockType function
    switch (lockType)
    {
        case LOCKTYPE_OPEN:                    // 5  - Basic opening
        case LOCKTYPE_TREASURE:                // 6  - Treasure chests
        case LOCKTYPE_CLOSE:                   // 8  - Closing objects
        case LOCKTYPE_QUICK_OPEN:              // 10 - Quick opening
        case LOCKTYPE_QUICK_CLOSE:             // 11 - Quick closing
        case LOCKTYPE_OPEN_TINKERING:          // 12 - Engineering opening
        case LOCKTYPE_OPEN_KNEELING:           // 13 - Kneeling opening (quest objects)
        case LOCKTYPE_OPEN_ATTACKING:          // 14 - Combat opening
        case LOCKTYPE_BLASTING:                // 16 - Explosive opening
        case LOCKTYPE_SLOW_OPEN:               // 17 - Slow opening (visual effect)
        case LOCKTYPE_SLOW_CLOSE:              // 18 - Slow closing (visual effect)
        case LOCKTYPE_OPEN_FROM_VEHICLE:       // 21 - Vehicle opening
            return true;

        // These lock types should NOT be accessible without proper skills/items
        case LOCKTYPE_PICKLOCK:                // 1  - Requires lockpicking skill
        case LOCKTYPE_HERBALISM:               // 2  - Requires herbalism skill
        case LOCKTYPE_MINING:                  // 3  - Requires mining skill
        case LOCKTYPE_DISARM_TRAP:             // 4  - Requires trap disarming (not implemented)
        case LOCKTYPE_CALCIFIED_ELVEN_GEMS:    // 7  - Special case (not implemented)
        case LOCKTYPE_ARM_TRAP:                // 9  - Trap arming (not implemented)
        case LOCKTYPE_GAHZRIDIAN:              // 15 - Special case (not implemented)
        case LOCKTYPE_FISHING:                 // 19 - Requires fishing skill
        case LOCKTYPE_INSCRIPTION:             // 20 - Requires inscription skill
        default:
            return false;
    }
}

bool LootObject::IsStillValid(Player* bot) const
{
    if (IsEmpty())
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;

    // Check if creature is still valid and lootable (without modifying state)
    Creature* creature = botAI->GetCreature(guid);
    if (creature && creature->getDeathState() == DeathState::Corpse)
    {
        // Check if still lootable or skinnable
        if (creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
            return true;

        if (creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE) && skillId != SKILL_NONE)
            return true;
    }

    // Check if game object is still valid and spawned (without modifying state)
    GameObject* go = botAI->GetGameObject(guid);
    if (go && go->isSpawned() && go->GetGoState() == GO_STATE_READY)
        return true;

    return false;
}

WorldObject* LootObject::GetWorldObject(Player* bot)
{
    // Check if loot is still valid without modifying state
    if (!IsStillValid(bot))
        return nullptr;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
    {
        return nullptr;
    }

    Creature* creature = botAI->GetCreature(guid);
    if (creature && creature->getDeathState() == DeathState::Corpse && creature->IsInWorld())
        return creature;

    GameObject* go = botAI->GetGameObject(guid);
    if (go && go->isSpawned() && go->IsInWorld())
        return go;

    return nullptr;
}

LootObject::LootObject(LootObject const& other)
{
    guid = other.guid;
    skillId = other.skillId;
    reqSkillValue = other.reqSkillValue;
    reqItem = other.reqItem;
}

bool LootObject::IsLootPossible(Player* bot)
{
    if (IsEmpty() || !bot)
        return false;

    WorldObject* worldObj = GetWorldObject(bot);  // Store result to avoid multiple calls
    if (!worldObj)
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
    {
        return false;
    }

    bool debugLoot = botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT);
    
    if (debugLoot)
    {
        std::ostringstream out;
        out << "LootPossible: Checking " << worldObj->GetName() 
            << " (GUID: " << guid.ToString() << ")";
        botAI->TellMaster(out.str());
    }
    
    if (reqItem && !bot->HasItemCount(reqItem, 1))
    {
        if (debugLoot)
        {
            const ItemTemplate* keyProto = sObjectMgr->GetItemTemplate(reqItem);
            std::ostringstream out;
            out << "LootPossible: Missing required key " 
                << (keyProto ? keyProto->Name1 : "Unknown") << " (ID: " << reqItem << ")";
            botAI->TellMaster(out.str());
        }
        return false;
    }

    
    if (!bot->IsInWater() && (abs(worldObj->GetPositionZ() - bot->GetPositionZ()) > INTERACTION_DISTANCE - 2.0f))
    {
        Map* map = bot->GetMap();
        const float x = worldObj->GetPositionX();
        const float y = worldObj->GetPositionY();
        const float z = worldObj->GetPositionZ();
        
        // Check if loot is in water - if so, bot can swim to it
        bool lootInWater = map->IsInWater(bot->GetPhaseMask(), x, y, z, bot->GetCollisionHeight());
        
        if (!lootInWater)
        {
            // Only apply strict pathfinding check for non-water loot
            float destX = x;
            float destY = y;
            float destZ = z;
            if (!map->CanReachPositionAndGetValidCoords(bot, destX, destY, destZ))
            {
                if (debugLoot)
                    botAI->TellMaster("LootPossible: Cannot reach target - pathfinding failed");
                return false;
            }
        }
        else if (debugLoot)
        {
            botAI->TellMaster("LootPossible: Loot in water - allowing swimming access");
        }
        // If loot is in water, allow bot to attempt swimming to it regardless of pathfinding
    }

    Creature* creature = botAI->GetCreature(guid);
    if (creature && creature->getDeathState() == DeathState::Corpse)
    {
        if (!bot->isAllowedToLoot(creature) && skillId != SKILL_SKINNING)
        {
            if (debugLoot)
                botAI->TellMaster("LootPossible: Not allowed to loot creature");
            return false;
        }
    }

    // Prevent bot from running to chests that are unlootable (e.g. Gunship Armory before completing the event) or on
    // respawn time
    GameObject* go = botAI->GetGameObject(guid);
    if (go && (go->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_INTERACT_COND | GO_FLAG_NOT_SELECTABLE) || !go->isSpawned()))
    {
        bool canLootForQuest = false;

        // Only check for chest/goober types!
        if (go->GetGoType() == GAMEOBJECT_TYPE_CHEST || go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
        {
            uint32 questId = 0;
            uint32 lootId = 0;

            if (go->GetGoType() == GAMEOBJECT_TYPE_CHEST)
            {
                questId = go->GetGOInfo()->chest.questId;
                lootId = go->GetGOInfo()->GetLootId();
            }
            else if (go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
            {
                questId = go->GetGOInfo()->goober.questId;
                lootId = go->GetGOInfo()->GetLootId();
            }

            if ((questId && bot->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE) ||
                LootTemplates_Gameobject.HaveQuestLootForPlayer(lootId, bot))
            {
                canLootForQuest = true;
            }
        }

        if (!canLootForQuest)
        {
            return false;
        }
    }

    // Use the accessibility result calculated during Refresh()
    if (!isAccessible)
    {
        if (debugLoot)
        {
            if (skillId == SKILL_NONE)
                botAI->TellMaster("LootPossible: Object not accessible (no lock requirements found)");
            else
            {
                std::ostringstream out;
                out << "LootPossible: Object not accessible (skill " << skillId;
                if (reqSkillValue > 0)
                {
                    uint32 skillValue = botAI->HasSkill((SkillType)skillId) ? uint32(bot->GetSkillValue(skillId)) : 0;
                    out << " - have " << skillValue << ", need " << reqSkillValue;
                }
                out << ")";
                botAI->TellMaster(out.str());
            }
        }
        return false;
    }

    // Check for required tools (real-time check since tool availability can change)
    if (skillId == SKILL_MINING && !bot->HasItemCount(756, 1) && !bot->HasItemCount(778, 1) &&
        !bot->HasItemCount(1819, 1) && !bot->HasItemCount(1893, 1) && !bot->HasItemCount(1959, 1) &&
        !bot->HasItemCount(2901, 1) && !bot->HasItemCount(9465, 1) && !bot->HasItemCount(20723, 1) &&
        !bot->HasItemCount(40772, 1) && !bot->HasItemCount(40892, 1) && !bot->HasItemCount(40893, 1))
    {
        if (debugLoot)
            botAI->TellMaster("LootPossible: Mining skill available but missing mining pick");
        return false;  // Bot is missing a mining pick
    }

    if (skillId == SKILL_SKINNING && !bot->HasItemCount(7005, 1) && !bot->HasItemCount(40772, 1) &&
        !bot->HasItemCount(40893, 1) && !bot->HasItemCount(12709, 1) && !bot->HasItemCount(19901, 1))
    {
        if (debugLoot)
            botAI->TellMaster("LootPossible: Skinning skill available but missing skinning knife");
        return false;  // Bot is missing a skinning knife
    }

    if (debugLoot)
    {
        if (skillId == SKILL_NONE)
            botAI->TellMaster("LootPossible: Object accessible (no skill required)");
        else
        {
            std::ostringstream out;
            out << "LootPossible: Object accessible (skill " << skillId << " satisfied)";
            botAI->TellMaster(out.str());
        }
    }

    return true;
}

bool LootObjectStack::Add(ObjectGuid guid)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    
    if (availableLoot.size() >= MAX_LOOT_OBJECT_COUNT)
    {
        if (botAI && botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "LootStack: Shrinking loot list (size: " << availableLoot.size() << " >= max: " << MAX_LOOT_OBJECT_COUNT << ")";
            botAI->TellMaster(out.str());
        }
        availableLoot.shrink(time(nullptr) - 30);
    }

    if (availableLoot.size() >= MAX_LOOT_OBJECT_COUNT)
    {
        if (botAI && botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "LootStack: Clearing all loot (still size: " << availableLoot.size() << " >= max: " << MAX_LOOT_OBJECT_COUNT << ")";
            botAI->TellMaster(out.str());
        }
        availableLoot.clear();
    }

    if (!availableLoot.insert(guid).second)
    {
        if (botAI && botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT))
        {
            WorldObject* obj = ObjectAccessor::GetWorldObject(*bot, guid);
            std::ostringstream out;
            out << "LootStack: Duplicate loot target " << (obj ? obj->GetName() : "Unknown") << " (GUID: " << guid.ToString() << ")";
            botAI->TellMaster(out.str());
        }
        return false;
    }

    if (botAI && botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT))
    {
        WorldObject* obj = ObjectAccessor::GetWorldObject(*bot, guid);
        std::ostringstream out;
        out << "LootStack: Added loot target " << (obj ? obj->GetName() : "Unknown") << " (GUID: " << guid.ToString() << ") - total: " << availableLoot.size();
        botAI->TellMaster(out.str());
    }

    return true;
}

void LootObjectStack::Remove(ObjectGuid guid)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    
    LootTargetList::iterator i = availableLoot.find(guid);
    if (i != availableLoot.end())
    {
        if (botAI && botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT))
        {
            WorldObject* obj = ObjectAccessor::GetWorldObject(*bot, guid);
            std::ostringstream out;
            out << "LootStack: Removed loot target " << (obj ? obj->GetName() : "Unknown") << " (GUID: " << guid.ToString() << ") - remaining: " << (availableLoot.size() - 1);
            botAI->TellMaster(out.str());
        }
        availableLoot.erase(i);
    }
    else if (botAI && botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "LootStack: Attempted to remove non-existent loot target (GUID: " << guid.ToString() << ")";
        botAI->TellMaster(out.str());
    }
}

void LootObjectStack::Clear() 
{ 
    availableLoot.clear(); 
    pendingLoot.clear(); 
    partiallyLootedObjects.clear();
}

bool LootObjectStack::CanLoot(float maxDistance)
{
    LootObject nearest = GetNearest(maxDistance);
    return !nearest.IsEmpty();
}

LootObject LootObjectStack::GetLoot(float maxDistance)
{
    LootObject nearest = GetNearest(maxDistance);
    return nearest.IsEmpty() ? LootObject() : nearest;
}

LootObject LootObjectStack::GetNearest(float maxDistance)
{
    availableLoot.shrink(time(nullptr) - 30);

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    bool debugLoot = botAI && botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT);
    
    if (debugLoot && !availableLoot.empty())
    {
        std::ostringstream out;
        out << "LootStack: Evaluating " << availableLoot.size() << " loot targets";
        if (maxDistance > 0)
            out << " (max distance: " << maxDistance << "yd)";
        botAI->TellMaster(out.str());
    }

    LootObject nearest;
    float nearestDistance = std::numeric_limits<float>::max();
    uint32 evaluatedCount = 0;
    uint32 skippedDistance = 0;
    uint32 skippedLootPossible = 0;

    LootTargetList safeCopy(availableLoot);
    for (LootTargetList::iterator i = safeCopy.begin(); i != safeCopy.end(); i++)
    {
        ObjectGuid guid = i->guid;

        // Skip partially looted objects
        if (IsPartiallyLooted(guid))
            continue;

        // Skip pending loot (safety check - shouldn't happen with MarkAsPending fix, but belt & suspenders)
        if (IsPending(guid))
            continue;

        WorldObject* worldObj = ObjectAccessor::GetWorldObject(*bot, guid);
        if (!worldObj)
            continue;

        float distance = bot->GetDistance(worldObj);
        evaluatedCount++;

        if (distance >= nearestDistance || (maxDistance && distance > maxDistance))
        {
            if (debugLoot)
                skippedDistance++;
            continue;
        }

        LootObject lootObject(bot, guid);

        if (!lootObject.IsLootPossible(bot))
        {
            if (debugLoot)
                skippedLootPossible++;
            continue;
        }

        if (debugLoot)
        {
            std::ostringstream out;
            out << "LootStack: Selected " << worldObj->GetName() 
                << " at " << std::fixed << std::setprecision(1) << distance << "yd";
            botAI->TellMaster(out.str());
        }

        nearestDistance = distance;
        nearest = lootObject;
    }

    if (debugLoot)
    {
        std::ostringstream out;
        out << "LootStack: Evaluated " << evaluatedCount << " targets";
        if (skippedDistance > 0)
            out << ", skipped " << skippedDistance << " (distance)";
        if (skippedLootPossible > 0)
            out << ", skipped " << skippedLootPossible << " (not lootable)";
        if (!nearest.IsEmpty())
            out << " - selected target at " << std::fixed << std::setprecision(1) << nearestDistance << "yd";
        else
            out << " - no valid loot found";
        botAI->TellMaster(out.str());
    }

    return nearest;
}

void LootObjectStack::MarkAsPending(ObjectGuid guid)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);

    // Add to pending loot list AND remove from available loot
    pendingLoot.insert(LootTarget(guid));
    availableLoot.erase(guid);  // FIX: Prevent duplicate selection

    if (botAI && botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT))
    {
        WorldObject* obj = ObjectAccessor::GetWorldObject(*bot, guid);
        std::ostringstream out;
        out << "LootStack: Marked loot target as pending " << (obj ? obj->GetName() : "Unknown") << " (GUID: " << guid.ToString() << ")";
        botAI->TellMaster(out.str());
    }
}

void LootObjectStack::MarkAsCompleted(ObjectGuid guid)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);

    // Remove from both available and pending lists
    availableLoot.erase(guid);
    pendingLoot.erase(guid);

    if (botAI && botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT))
    {
        WorldObject* obj = ObjectAccessor::GetWorldObject(*bot, guid);
        std::ostringstream out;
        out << "LootStack: Marked loot target as completed " << (obj ? obj->GetName() : "Unknown") << " (GUID: " << guid.ToString() << ")";
        botAI->TellMaster(out.str());
    }
}

void LootObjectStack::ProcessPendingTimeouts()
{
    time_t currentTime = time(nullptr);
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    bool debugLoot = botAI && botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT);

    for (auto it = pendingLoot.begin(); it != pendingLoot.end();)
    {
        // If pending loot is older than 10 seconds, move it back to available
        if (currentTime - it->asOfTime > 10)
        {
            ObjectGuid guid = it->guid;

            // Check if the object still exists and is valid
            WorldObject* obj = ObjectAccessor::GetWorldObject(*bot, guid);
            if (obj)
            {
                // Move back to available loot
                availableLoot.insert(LootTarget(guid));

                if (debugLoot)
                {
                    std::ostringstream out;
                    out << "LootStack: Pending loot timeout, moved back to available: " << obj->GetName() << " (GUID: " << guid.ToString() << ")";
                    botAI->TellMaster(out.str());
                }
            }

            it = pendingLoot.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool LootObjectStack::IsPending(ObjectGuid guid) const
{
    return pendingLoot.find(guid) != pendingLoot.end();
}

void LootObjectStack::MarkAsPartiallyLooted(ObjectGuid guid)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);

    // Remove from available and pending, add to partially looted
    availableLoot.erase(guid);
    pendingLoot.erase(guid);
    partiallyLootedObjects.insert(LootTarget(guid));

    if (botAI && botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT))
    {
        WorldObject* obj = ObjectAccessor::GetWorldObject(*bot, guid);
        std::ostringstream out;
        out << "LootStack: Marked as partially looted " << (obj ? obj->GetName() : "Unknown") << " (GUID: " << guid.ToString() << ")";
        botAI->TellMaster(out.str());
    }
}

void LootObjectStack::ProcessPartialLootExpiry()
{
    time_t currentTime = time(nullptr);
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    bool debugLoot = botAI && botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT);

    for (auto it = partiallyLootedObjects.begin(); it != partiallyLootedObjects.end();)
    {
        // If partially looted object is older than 3 minutes (180 seconds), make it available again
        if (currentTime - it->asOfTime > 180)
        {
            ObjectGuid guid = it->guid;

            // Check if the object still exists and is valid
            WorldObject* obj = ObjectAccessor::GetWorldObject(*bot, guid);
            if (obj)
            {
                // Move back to available loot
                availableLoot.insert(LootTarget(guid));

                if (debugLoot)
                {
                    std::ostringstream out;
                    out << "LootStack: Partially looted object expired, moved back to available: " << obj->GetName() << " (GUID: " << guid.ToString() << ")";
                    botAI->TellMaster(out.str());
                }
            }

            it = partiallyLootedObjects.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void LootObjectStack::ClearPartialLootOnBagSpaceChange()
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    uint8 currentBagSpace = botAI->GetAiObjectContext()->GetValue<uint8>("bag space")->Get();

    // Check if bag space changed
    if (currentBagSpace != lastBagSpaceCheck)
    {
        lastBagSpaceCheck = currentBagSpace;

        // If bag space improved significantly (below 80% threshold)
        if (currentBagSpace < 80) // Below the 80% restriction threshold
        {
            bool debugLoot = botAI && botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT);

            if (debugLoot && !partiallyLootedObjects.empty())
            {
                std::ostringstream out;
                out << "LootStack: Bag space improved, clearing " << partiallyLootedObjects.size() << " partially looted objects";
                botAI->TellMaster(out.str());
            }

            // Move all partially looted objects back to available
            for (const auto& lootTarget : partiallyLootedObjects)
            {
                ObjectGuid guid = lootTarget.guid;
                WorldObject* obj = ObjectAccessor::GetWorldObject(*bot, guid);
                if (obj)
                {
                    availableLoot.insert(LootTarget(guid));
                }
            }

            partiallyLootedObjects.clear();
        }
    }
}

bool LootObjectStack::IsPartiallyLooted(ObjectGuid guid) const
{
    return partiallyLootedObjects.find(guid) != partiallyLootedObjects.end();
}
