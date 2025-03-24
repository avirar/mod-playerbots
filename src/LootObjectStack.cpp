/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "LootObjectStack.h"

#include "ItemUsageValue.h"
#include "LootMgr.h"
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

    GameObject* go = botAI->GetGameObject(lootGUID);
    if (go && go->isSpawned() && go->GetGoState() == GO_STATE_READY)
    {
        std::ostringstream goInfo;
        goInfo << "Evaluating GO: " << go->GetNameForLocaleIdx(sWorld->GetDefaultDbcLocale())
               << " [Entry: " << go->GetEntry() << "], State: " << go->GetGoState();
        botAI->TellMaster(goInfo.str());
    
        bool onlyHasQuestItems = true;
        bool hasAnyQuestItems = false;
    
        GameObjectQuestItemList const* items = sObjectMgr->GetGameObjectQuestItemList(go->GetEntry());
        for (size_t i = 0; i < MAX_GAMEOBJECT_QUEST_ITEMS; i++)
        {
            if (!items || i >= items->size())
                break;
    
            uint32 itemId = uint32((*items)[i]);
            if (!itemId)
                continue;
    
            hasAnyQuestItems = true;
    
            const ItemTemplate* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto)
            {
                botAI->TellMaster("Quest item ID " + std::to_string(itemId) + " has no item template.");
                continue;
            }
    
            std::ostringstream msg;
            msg << "GO registered quest item: " << proto->Name1 << " [" << itemId << "]";
            botAI->TellMaster(msg.str());
    
            if (IsNeededForQuest(bot, itemId))
            {
                botAI->TellMaster("Bot needs quest item: " + proto->Name1 + " [" + std::to_string(itemId) + "]");
                this->guid = lootGUID;
                return;
            }
            else
            {
                botAI->TellMaster("Quest item not needed: " + proto->Name1 + " [" + std::to_string(itemId) + "]");
            }
    
            if (proto->Class != ITEM_CLASS_QUEST)
            {
                botAI->TellMaster("GO quest item is not a quest class item.");
                onlyHasQuestItems = false;
            }
        }
    
        // Retrieve the correct loot table entry
        uint32 lootEntry = go->GetGOInfo()->GetLootId();
        if (lootEntry == 0)
        {
            botAI->TellMaster("GO has no loot entry. Skipping.");
            return;
        }
    
        const LootTemplate* lootTemplate = LootTemplates_Gameobject.GetLootFor(lootEntry);
        if (!lootTemplate)
        {
            botAI->TellMaster("Loot template not found for lootEntry: " + std::to_string(lootEntry));
            return;
        }
    
        Loot loot;
        lootTemplate->Process(loot, LootTemplates_Gameobject, 1, bot);
        botAI->TellMaster("Processing GO loot template with " + std::to_string(loot.items.size()) + " items.");
    
        for (const LootItem& item : loot.items)
        {
            uint32 itemId = item.itemid;
            if (!itemId)
                continue;
    
            const ItemTemplate* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto)
            {
                botAI->TellMaster("Loot item ID " + std::to_string(itemId) + " has no item template.");
                continue;
            }
    
            AiObjectContext* context = botAI->GetAiObjectContext();
            std::ostringstream out;
            out << itemId;
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", out.str());
    
            std::ostringstream msg;
            msg << "Loot item: " << proto->Name1 << " [" << itemId << "], usage: " << usage
                << ", class: " << proto->Class;
            botAI->TellMaster(msg.str());
    
            if (usage == ITEM_USAGE_NONE)
            {
                botAI->TellMaster("Item is not useful to bot.");
                onlyHasQuestItems = false;
                continue;
            }
    
            if (proto->Class != ITEM_CLASS_QUEST)
            {
                botAI->TellMaster("Item is not a quest item.");
                onlyHasQuestItems = false;
                break;
            }
    
            // Check reference loot template
            if (const LootTemplate* refLootTemplate = LootTemplates_Reference.GetLootFor(itemId))
            {
                Loot refLoot;
                refLootTemplate->Process(refLoot, LootTemplates_Reference, 1, bot);
                botAI->TellMaster("Processing referenced loot template for item: " + std::to_string(itemId));
    
                for (const LootItem& refItem : refLoot.items)
                {
                    uint32 refItemId = refItem.itemid;
                    if (!refItemId)
                        continue;
    
                    const ItemTemplate* refProto = sObjectMgr->GetItemTemplate(refItemId);
                    if (!refProto)
                    {
                        botAI->TellMaster("Referenced item ID " + std::to_string(refItemId) + " has no template.");
                        continue;
                    }
    
                    std::ostringstream refOut;
                    refOut << refItemId;
                    ItemUsage refUsage = AI_VALUE2(ItemUsage, "item usage", refOut.str());
    
                    std::ostringstream refMsg;
                    refMsg << "Referenced loot item: " << refProto->Name1 << " [" << refItemId << "], usage: "
                           << refUsage << ", class: " << refProto->Class;
                    botAI->TellMaster(refMsg.str());
    
                    if (refUsage == ITEM_USAGE_NONE)
                    {
                        botAI->TellMaster("Referenced item is not useful to bot.");
                        onlyHasQuestItems = false;
                        continue;
                    }
    
                    if (refProto->Class != ITEM_CLASS_QUEST)
                    {
                        botAI->TellMaster("Referenced item is not a quest item.");
                        onlyHasQuestItems = false;
                        break;
                    }
                }
            }
        }
    
        if (hasAnyQuestItems && onlyHasQuestItems)
        {
            botAI->TellMaster("GO has only quest items, none needed. Skipping.");
            return;
        }
    
        botAI->TellMaster("GO is lootable. Assigning guid.");
        guid = lootGUID;

        // If gameobject has only quest items that bot doesn’t need, skip it.
        if (hasAnyQuestItems && onlyHasQuestItems)
            return;

        // Otherwise, loot it.
        guid = lootGUID;

        uint32 goId = go->GetEntry();
        uint32 lockId = go->GetGOInfo()->GetLockId();
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
            return;

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
                    if (goId == 13891 || goId == 19535)  // Serpentbloom
                    {
                        this->guid = lootGUID;
                    }
                    else if (SkillByLockType(LockType(lockInfo->Index[i])) > 0)
                    {
                        skillId = SkillByLockType(LockType(lockInfo->Index[i]));
                        reqSkillValue = std::max((uint32)1, lockInfo->Skill[i]);
                        guid = lootGUID;
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
    if (reqItem && !bot->HasItemCount(reqItem, 1))
        return false;

    if (abs(worldObj->GetPositionZ() - bot->GetPositionZ()) > INTERACTION_DISTANCE -2.0f)
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
        return false;  // Bot is missing a mining pick
    }
    
    if (skillId == SKILL_SKINNING &&
        !bot->HasItemCount(7005, 1) &&
        !bot->HasItemCount(40772, 1) &&
        !bot->HasItemCount(40893, 1) &&
        !bot->HasItemCount(12709, 1) &&
        !bot->HasItemCount(19901, 1))
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
        if (!lootObject.IsLootPossible(bot)) // Ensure loot object is valid
            continue;

        WorldObject* worldObj = lootObject.GetWorldObject(bot);
        if (!worldObj) // Prevent null pointer dereference
        {
            continue;
        }

        float distance = bot->GetDistance(worldObj);
        if (!maxDistance || distance <= maxDistance)
            sortedMap[distance] = lootObject;
    }

    std::vector<LootObject> result;
    for (auto& [_, lootObject] : sortedMap)
        result.push_back(lootObject);

    return result;
}
