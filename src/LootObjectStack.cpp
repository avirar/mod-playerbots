/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "LootObjectStack.h"

#include "LootMgr.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
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

    // Debug: Log what object we're checking
    Creature* creature = botAI->GetCreature(lootGUID);
    if (creature && creature->getDeathState() == DeathState::Corpse)
    {
        if (creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
        {
            guid = lootGUID;
            std::ostringstream stream;
            stream << "LootObject::Refresh - Creature is lootable: " << creature->GetName();
            botAI->TellMaster(stream);
        }
        else
        {
            std::ostringstream stream;
            stream << "LootObject::Refresh - Creature not lootable: " << creature->GetName();
            botAI->TellMaster(stream);
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
                std::ostringstream stream;
                stream << "LootObject::Refresh - Creature skinnable and bot has skill: " << creature->GetName();
                botAI->TellMaster(stream);
            }
            else
            {
                std::ostringstream stream;
                stream << "LootObject::Refresh - Creature skinnable but bot lacks skill or value: " << creature->GetName();
                botAI->TellMaster(stream);
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
                std::ostringstream stream;
                stream << "LootObject::Refresh - GameObject has quest item needed by bot: " << go->GetName();
                botAI->TellMaster(stream);
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
            std::ostringstream stream;
            stream << "LootObject::Refresh - GameObject has only quest items bot doesn't need: " << go->GetName();
            botAI->TellMaster(stream);
            return;
        }

        // Retrieve the correct loot table entry
        uint32 lootEntry = go->GetGOInfo()->GetLootId();
        if (lootEntry == 0)
        {
            std::ostringstream stream;
            stream << "LootObject::Refresh - GameObject has no loot template: " << go->GetName();
            botAI->TellMaster(stream);
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
            std::ostringstream stream;
            stream << "LootObject::Refresh - GameObject has only quest items bot doesn't need: " << go->GetName();
            botAI->TellMaster(stream);
            return;
        }

        // Otherwise, loot it.
        guid = lootGUID;
        std::ostringstream stream;
        stream << "LootObject::Refresh - GameObject is lootable: " << go->GetName();
        botAI->TellMaster(stream);

        uint32 goId = go->GetEntry();
        uint32 lockId = go->GetGOInfo()->GetLockId();
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
        {
            std::ostringstream stream;
            stream << "LootObject::Refresh - GameObject has no lock info: " << go->GetName();
            botAI->TellMaster(stream);
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
                        std::ostringstream stream;
                        stream << "LootObject::Refresh - GameObject requires item key: " << go->GetName();
                        botAI->TellMaster(stream);
                    }
                    break;

                case LOCK_KEY_SKILL:
                    if (goId == 13891 || goId == 19535 || lockId == 259)  // Serpentbloom uses lock 259, there are others too
                    {
                        this->guid = lootGUID;
                        std::ostringstream stream;
                        stream << "LootObject::Refresh - GameObject is Serpentbloom or lockId 259, no lock needed: " << go->GetName();
                        botAI->TellMaster(stream);
                    }
                    else if (SkillByLockType(LockType(lockInfo->Index[i])) > 0)
                    {
                        skillId = SkillByLockType(LockType(lockInfo->Index[i]));
                        reqSkillValue = std::max((uint32)1, lockInfo->Skill[i]);
                        guid = lootGUID;
                        std::ostringstream stream;
                        stream << "LootObject::Refresh - GameObject requires skill lock: " << go->GetName();
                        botAI->TellMaster(stream);
                    }
                    break;

                case LOCK_KEY_NONE:
                    guid = lootGUID;
                    std::ostringstream stream;
                    stream << "LootObject::Refresh - GameObject has no lock: " << go->GetName();
                    botAI->TellMaster(stream);
                    break;
            }
        }
    }
    else
    {
        std::ostringstream stream;
        stream << "LootObject::Refresh - GameObject not valid or not ready: " << lootGUID.ToString();
        botAI->TellMaster(stream);
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
        stream << "LootObject::IsLootPossible - Missing required item: " << reqItem;
        botAI->TellMaster(stream);
        return false;
    }

    
    if (!bot->IsInWater() && (abs(worldObj->GetPositionZ() - bot->GetPositionZ()) > INTERACTION_DISTANCE - 2.0f))
    {
        stream << "LootObject::IsLootPossible - Too far vertically from object";
        botAI->TellMaster(stream);
        return false;
    }

    Creature* creature = botAI->GetCreature(guid);
    if (creature && creature->getDeathState() == DeathState::Corpse)
    {
        if (!bot->isAllowedToLoot(creature) && skillId != SKILL_SKINNING)
        {
            stream << "LootObject::IsLootPossible - Bot not allowed to loot creature: " << creature->GetName();
            botAI->TellMaster(stream);
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
    
        // Log debug information
        std::ostringstream debugStream;
        debugStream << "LootObject::IsLootPossible - GameObject flags check for: " << go->GetName()
                    << " | INTERACT_COND: " << (go->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_INTERACT_COND) ? "true" : "false")
                    << " | NOT_SELECTABLE: " << (go->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_NOT_SELECTABLE) ? "true" : "false")
                    << " | CanLootForQuest: " << (canLootForQuest ? "true" : "false");
        botAI->TellMaster(debugStream.str());
    
        if (!canLootForQuest)
        {
            stream << "LootObject::IsLootPossible - GameObject is unlootable for bot (interact condition): " << go->GetName();
            botAI->TellMaster(stream);
            return false;
        }
    }

    if (skillId == SKILL_NONE)
    {
        if (go)
        {
            stream << "LootObject::IsLootPossible - No skill required, lootable: " << go->GetName();
        }
        else if (creature)
        {
            stream << "LootObject::IsLootPossible - No skill required, lootable: " << creature->GetName();
        }
        else
        {
            stream << "LootObject::IsLootPossible - No skill required, lootable";
        }
        botAI->TellMaster(stream);
        return true;
    }

    if (skillId == SKILL_FISHING)
    {
        stream << "LootObject::IsLootPossible - Fishing skill not allowed for loot";
        botAI->TellMaster(stream);
        return false;
    }

    if (!botAI->HasSkill((SkillType)skillId))
    {
        stream << "LootObject::IsLootPossible - Bot lacks required skill: " << skillId << " for ";
        if (go)
        {
            stream << go->GetName();
        }
        else if (creature)
        {
            stream << creature->GetName();
        }
        botAI->TellMaster(stream);
        return false;
    }

    if (!reqSkillValue)
    {
        stream << "LootObject::IsLootPossible - No skill value required, lootable " << go->GetName();
        botAI->TellMaster(stream);
        return true;
    }

    uint32 skillValue = uint32(bot->GetSkillValue(skillId));
    if (reqSkillValue > skillValue)
    {
        stream << "LootObject::IsLootPossible - Bot's skill too low for required value: " << reqSkillValue << " vs " << skillValue;
        botAI->TellMaster(stream);
        return false;
    }

    if (skillId == SKILL_MINING && !bot->HasItemCount(756, 1) && !bot->HasItemCount(778, 1) &&
        !bot->HasItemCount(1819, 1) && !bot->HasItemCount(1893, 1) && !bot->HasItemCount(1959, 1) &&
        !bot->HasItemCount(2901, 1) && !bot->HasItemCount(9465, 1) && !bot->HasItemCount(20723, 1) &&
        !bot->HasItemCount(40772, 1) && !bot->HasItemCount(40892, 1) && !bot->HasItemCount(40893, 1))
    {
        stream << "LootObject::IsLootPossible - Missing mining pick for skill: " << skillId;
        botAI->TellMaster(stream);
        return false;  // Bot is missing a mining pick
    }

    if (skillId == SKILL_SKINNING && !bot->HasItemCount(7005, 1) && !bot->HasItemCount(40772, 1) &&
        !bot->HasItemCount(40893, 1) && !bot->HasItemCount(12709, 1) && !bot->HasItemCount(19901, 1))
    {
        stream << "LootObject::IsLootPossible - Missing skinning knife for skill: " << skillId;
        botAI->TellMaster(stream);
        return false;  // Bot is missing a skinning knife
    }

    stream << "LootObject::IsLootPossible - All checks passed, lootable: ";
    if (go)
    {
        stream << go->GetName();
    }
    else if (creature)
    {
        stream << creature->GetName();
    }
    botAI->TellMaster(stream);
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
