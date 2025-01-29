/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "LootObjectStack.h"

#include "LootMgr.h"
#include "Playerbots.h"
#include "Unit.h"

#define MAX_LOOT_OBJECT_COUNT 10

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
            guid = lootGUID;

        if (creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE))
        {
            skillId = creature->GetCreatureTemplate()->GetRequiredLootSkill();
            uint32 targetLevel = creature->GetLevel();
            reqSkillValue = targetLevel < 10 ? 1 : targetLevel < 20 ? (targetLevel - 10) * 10 : targetLevel * 5;
            if (botAI->HasSkill((SkillType)skillId) && bot->GetSkillValue(skillId) >= reqSkillValue)
                guid = lootGUID;
        }

        return;
    }

    bool botDebugEnabled = (botAI->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT));

    GameObject* go = botAI->GetGameObject(lootGUID);
    if (go && go->isSpawned() && go->GetGoState() == GO_STATE_READY)
    {
        if (botDebugEnabled)
        {
            LOG_INFO("playerbots", "Found gameobject {} with lootGUID {} in ready state", go->GetEntry(), lootGUID.ToString());
        }

        bool onlyHasQuestItems = true;
        bool hasAnyQuestItems = false;

        GameObjectQuestItemList const* items = sObjectMgr->GetGameObjectQuestItemList(go->GetEntry());
        for (int i = 0; i < MAX_GAMEOBJECT_QUEST_ITEMS; i++)
        {
            if (!items || i >= items->size())
                break;

            uint32 itemId = uint32((*items)[i]);
            if (!itemId)
                continue;

            hasAnyQuestItems = true;

            if (IsNeededForQuest(bot, itemId))
            {
                if (botDebugEnabled)
                {
                    LOG_INFO("playerbots", "Bot needs quest item with ID {}", itemId);
                }
                this->guid = lootGUID;
                return;
            }

            const ItemTemplate* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto)
                continue;

            if (proto->Class != ITEM_CLASS_QUEST)
            {
                onlyHasQuestItems = false;
            }
        }

        // Then ALSO check the normal loot template:
        if (auto lootTemplate = LootTemplates_Gameobject.GetLootFor(go->GetEntry()))
        {
            if (botDebugEnabled)
            {
                LOG_INFO("playerbots", "Processing loot for GameObject {} (lootGUID: {}).", go->GetEntry(), lootGUID.ToString());
            }
        
            // Create a loot object to hold the processed items
            Loot loot;
        
            // Process the loot using the existing LootTemplates_Gameobject store
            lootTemplate->Process(loot, LootTemplates_Gameobject, 0, bot);
        
            if (botDebugEnabled)
            {
                LOG_INFO("playerbots", "GameObject {} processed {} loot items.", go->GetEntry(), loot.Items.size());
            }
        
            for (LootItem const& item : loot.Items)
            {
                uint32 itemId = item.itemid;
                if (!itemId)
                    continue;
        
                const ItemTemplate* proto = sObjectMgr->GetItemTemplate(itemId);
                if (!proto)
                    continue;
        
                if (botDebugEnabled)
                {
                    LOG_INFO("playerbots", "Bot {} found item ID {} (Class: {}).", bot->GetName(), itemId, proto->Class);
                }
        
                // Check if the item is not a quest item
                if (proto->Class != ITEM_CLASS_QUEST)
                {
                    onlyHasQuestItems = false;
        
                    if (botDebugEnabled)
                    {
                        LOG_INFO("playerbots", "Item ID {} is NOT a quest item. Allowing loot.", itemId);
                    }
        
                    break; // Non-quest loot found; stop further checks
                }
            }
        }


        
        if (hasAnyQuestItems && onlyHasQuestItems)
        {
            if (botDebugEnabled)
            {
                LOG_INFO("playerbots", "Gameobject contains only quest items that the bot doesn't need. Skipping.");
            }
            return;
        }

        guid = lootGUID;

        uint32 goId = go->GetEntry();
        uint32 lockId = go->GetGOInfo()->GetLockId();
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
        {
            if (botDebugEnabled)
            {
                LOG_INFO("playerbots", "No lock information found for gameobject {}", goId);
            }
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
                        if (botDebugEnabled)
                        {
                            LOG_INFO("playerbots", "Lock requires item with ID {}", reqItem);
                        }
                        guid = lootGUID;
                    }
                    break;

                case LOCK_KEY_SKILL:
                    if (goId == 13891 || goId == 19535)  // Serpentbloom
                    {
                        if (botDebugEnabled)
                        {
                            LOG_INFO("playerbots", "Serpentbloom gameobject, setting GUID directly.");
                        }
                        this->guid = lootGUID;
                    }
                    else if (SkillByLockType(LockType(lockInfo->Index[i])) > 0)
                    {
                        skillId = SkillByLockType(LockType(lockInfo->Index[i]));
                        reqSkillValue = std::max((uint32)1, lockInfo->Skill[i]);

                        if (botDebugEnabled)
                        {
                            LOG_INFO("playerbots", "Lock requires skill ID {} with value {}", skillId, reqSkillValue);
                        }
                        guid = lootGUID;
                    }
                    break;

                case LOCK_KEY_NONE:
                    if (botDebugEnabled)
                    {
                        LOG_INFO("playerbots", "No lock required for this gameobject");
                    }
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
    if (IsEmpty() || !GetWorldObject(bot))
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
    {
        return false;
    }
    if (reqItem && !bot->HasItemCount(reqItem, 1))
        return false;

    if (abs(GetWorldObject(bot)->GetPositionZ() - bot->GetPositionZ()) > INTERACTION_DISTANCE)
        return false;

    Creature* creature = botAI->GetCreature(guid);
    if (creature && creature->getDeathState() == DeathState::Corpse)
    {
        if (!bot->isAllowedToLoot(creature) && skillId != SKILL_SKINNING)
            return false;
    }

    if (skillId == SKILL_NONE)
        return true;

    if (skillId == SKILL_FISHING)
        return false;

    if (!botAI->HasSkill((SkillType)skillId))
        return false;

    if (!reqSkillValue)
        return true;

    uint32 skillValue = uint32(bot->GetSkillValue(skillId));
    if (reqSkillValue > skillValue)
        return false;
    
    if (skillId == SKILL_MINING &&
        !bot->HasItemCount(756, 1) &&
        !bot->HasItemCount(778, 1) &&
        !bot->HasItemCount(1819, 1) &&
        !bot->HasItemCount(1893, 1) &&
        !bot->HasItemCount(1959, 1) &&
        !bot->HasItemCount(2901, 1) &&
        !bot->HasItemCount(9465, 1) &&
        !bot->HasItemCount(20723, 1) &&
        !bot->HasItemCount(40772, 1) &&
        !bot->HasItemCount(40892, 1) &&
        !bot->HasItemCount(40893, 1))
    {
        // If the bot is missing a pick for mining
        return false;
    }
    
    if (skillId == SKILL_SKINNING &&
        !bot->HasItemCount(7005, 1) &&
        !bot->HasItemCount(40772, 1) &&
        !bot->HasItemCount(40893, 1) &&
        !bot->HasItemCount(12709, 1) &&
        !bot->HasItemCount(19901, 1))
    {
        // If the bot is missing a skinner's knife
        return false;
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
    std::vector<LootObject> ordered = OrderByDistance(maxDistance);
    return !ordered.empty();
}

LootObject LootObjectStack::GetLoot(float maxDistance)
{
    std::vector<LootObject> ordered = OrderByDistance(maxDistance);
    return ordered.empty() ? LootObject() : *ordered.begin();
}

std::vector<LootObject> LootObjectStack::OrderByDistance(float maxDistance)
{
    availableLoot.shrink(time(nullptr) - 30);

    std::map<float, LootObject> sortedMap;
    LootTargetList safeCopy(availableLoot);
    for (LootTargetList::iterator i = safeCopy.begin(); i != safeCopy.end(); i++)
    {
        ObjectGuid guid = i->guid;
        LootObject lootObject(bot, guid);
        if (!lootObject.IsLootPossible(bot))
            continue;

        float distance = bot->GetDistance(lootObject.GetWorldObject(bot));
        if (!maxDistance || distance <= maxDistance)
            sortedMap[distance] = lootObject;
    }

    std::vector<LootObject> result;
    for (std::map<float, LootObject>::iterator i = sortedMap.begin(); i != sortedMap.end(); i++)
        result.push_back(i->second);

    return result;
}
