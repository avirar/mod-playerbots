/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "LootObjectStack.h"

#include "LootMgr.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "Unit.h"

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
    guid.Clear();

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
    {
        return;
    }

    Creature* creature = botAI->GetCreature(lootGUID);
    if (creature && creature->getDeathState() == DeathState::Corpse)
    {
        if (creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
        {
            guid = lootGUID;
        }
        else
        {
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
            }
        }

        return;
    }

    GameObject* go = botAI->GetGameObject(lootGUID);
    if (go && go->isSpawned() && go->GetGoState() == GO_STATE_READY)
    {
        bool onlyHasQuestItems = true;
        bool hasAnyQuestItems = false;
        bool hasNeededQuestItem = false;

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
                    }
                }
            }
        }

        // If gameobject has only quest items that bot doesn't need, skip it.
        if (hasAnyQuestItems && onlyHasQuestItems && !hasNeededQuestItem)
        {
            return;
        }

        // Otherwise, loot it.
        guid = lootGUID;

        uint32 goId = go->GetEntry();
        uint32 lockId = go->GetGOInfo()->GetLockId();
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
        {
            return;
        }

        for (uint8 i = 0; i < 8; ++i)
        {
            switch (lockInfo->Type[i])
            {
                case LOCK_KEY_ITEM:
                    if (lockInfo->Index[i] > 0)
                    {
                        reqItem = lockInfo->Index[i];
                        guid = lootGUID;
                    }
                    break;

                case LOCK_KEY_SKILL:
                    {
                        LockType lockType = LockType(lockInfo->Index[i]);
                        SkillType mappedSkill = SkillByLockType(lockType);
                        
                        if (mappedSkill > SKILL_NONE)
                        {
                            // Standard skill-based lock (lockpicking, mining, herbalism, etc.)
                            skillId = mappedSkill;
                            reqSkillValue = std::max((uint32)1, lockInfo->Skill[i]);
                            guid = lootGUID;
                        }
                        else if (IsAccessibleLockType(lockType))
                        {
                            // Special lock types that don't require skills but should be accessible
                            // (like LOCKTYPE_OPEN_KNEELING, LOCKTYPE_OPEN, LOCKTYPE_TREASURE, etc.)
                            skillId = SKILL_NONE; // No skill required
                            reqSkillValue = 0;
                            guid = lootGUID;
                        }
                        // If lock type is not accessible (like LOCKTYPE_DISARM_TRAP without mapping), 
                        // don't set guid - bot won't try to loot it
                    }
                    break;

                case LOCK_KEY_NONE:
                    guid = lootGUID;
                    break;
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

WorldObject* LootObject::GetWorldObject(Player* bot)
{
    Refresh(bot, guid);

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
    
    // Debug: Log when checking if loot is possible
    std::ostringstream stream;
    if (reqItem && !bot->HasItemCount(reqItem, 1))
    {
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
                return false;
            }
        }
        // If loot is in water, allow bot to attempt swimming to it regardless of pathfinding
    }

    Creature* creature = botAI->GetCreature(guid);
    if (creature && creature->getDeathState() == DeathState::Corpse)
    {
        if (!bot->isAllowedToLoot(creature) && skillId != SKILL_SKINNING)
        {
            return false;
        }
    }

    // Prevent bot from running to chests that are unlootable (e.g. Gunship Armory before completing the event)
    GameObject* go = botAI->GetGameObject(guid);
    if (go && go->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_INTERACT_COND | GO_FLAG_NOT_SELECTABLE))
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

    if (skillId == SKILL_NONE)
    {
        return true;
    }

    if (skillId == SKILL_FISHING)
    {
        return false;
    }

    if (!botAI->HasSkill((SkillType)skillId))
    {
        return false;
    }

    if (!reqSkillValue)
    {
        return true;
    }

    uint32 skillValue = uint32(bot->GetSkillValue(skillId));
    if (reqSkillValue > skillValue)
    {
        return false;
    }

    if (skillId == SKILL_MINING && !bot->HasItemCount(756, 1) && !bot->HasItemCount(778, 1) &&
        !bot->HasItemCount(1819, 1) && !bot->HasItemCount(1893, 1) && !bot->HasItemCount(1959, 1) &&
        !bot->HasItemCount(2901, 1) && !bot->HasItemCount(9465, 1) && !bot->HasItemCount(20723, 1) &&
        !bot->HasItemCount(40772, 1) && !bot->HasItemCount(40892, 1) && !bot->HasItemCount(40893, 1))
    {
        return false;  // Bot is missing a mining pick
    }

    if (skillId == SKILL_SKINNING && !bot->HasItemCount(7005, 1) && !bot->HasItemCount(40772, 1) &&
        !bot->HasItemCount(40893, 1) && !bot->HasItemCount(12709, 1) && !bot->HasItemCount(19901, 1))
    {
        return false;  // Bot is missing a skinning knife
    }

    return true;
}

bool LootObjectStack::Add(ObjectGuid guid)
{
    if (availableLoot.size() >= MAX_LOOT_OBJECT_COUNT)
    {
        availableLoot.shrink(time(nullptr) - 30);
    }

    if (availableLoot.size() >= MAX_LOOT_OBJECT_COUNT)
    {
        availableLoot.clear();
    }

    if (!availableLoot.insert(guid).second)
        return false;

    return true;
}

void LootObjectStack::Remove(ObjectGuid guid)
{
    LootTargetList::iterator i = availableLoot.find(guid);
    if (i != availableLoot.end())
        availableLoot.erase(i);
}

void LootObjectStack::Clear() { availableLoot.clear(); }

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

    LootObject nearest;
    float nearestDistance = std::numeric_limits<float>::max();

    LootTargetList safeCopy(availableLoot);
    for (LootTargetList::iterator i = safeCopy.begin(); i != safeCopy.end(); i++)
    {
        ObjectGuid guid = i->guid;

        WorldObject* worldObj = ObjectAccessor::GetWorldObject(*bot, guid);
        if (!worldObj)
            continue;

        float distance = bot->GetDistance(worldObj);

        if (distance >= nearestDistance || (maxDistance && distance > maxDistance))
            continue;

        LootObject lootObject(bot, guid);

        if (!lootObject.IsLootPossible(bot))
            continue;

        nearestDistance = distance;
        nearest = lootObject;
    }

    return nearest;
}
