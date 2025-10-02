/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "LootAction.h"

#include "ChatHelper.h"
#include "Event.h"
#include "GuildMgr.h"
#include "GuildTaskMgr.h"
#include "ItemUsageValue.h"
#include "LootObjectStack.h"
#include "LootStrategyValue.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "GuildMgr.h"
#include "BroadcastHelper.h"

bool LootAction::Execute(Event /*event*/)
{
    if (!AI_VALUE(bool, "has available loot"))
        return false;

    LootObject prevLoot = AI_VALUE(LootObject, "loot target");
    LootObject const& lootObject =
        AI_VALUE(LootObjectStack*, "available loot")->GetLoot(sPlayerbotAIConfig->lootDistance);

    if (!prevLoot.IsEmpty() && prevLoot.guid != lootObject.guid)
    {
        WorldPacket* packet = new WorldPacket(CMSG_LOOT_RELEASE, 8);
        *packet << prevLoot.guid;
        bot->GetSession()->QueuePacket(packet);
        // bot->GetSession()->HandleLootReleaseOpcode(packet);
    }

    // Provide a system to check if the game object id is disallowed in the user configurable list or not.
    // Check if the game object id is disallowed in the user configurable list or not.
    if (sPlayerbotAIConfig->disallowedGameObjects.find(lootObject.guid.GetEntry()) != sPlayerbotAIConfig->disallowedGameObjects.end())
    {
        return false;  // Game object ID is disallowed, so do not proceed
    }
    else
    {
        context->GetValue<LootObject>("loot target")->Set(lootObject);
        return true;
    }
}

bool LootAction::isUseful()
{
    return sPlayerbotAIConfig->freeMethodLoot || !bot->GetGroup() || bot->GetGroup()->GetLootMethod() != FREE_FOR_ALL;
}

enum ProfessionSpells
{
    ALCHEMY = 2259,
    BLACKSMITHING = 2018,
    COOKING = 2550,
    ENCHANTING = 7411,
    ENGINEERING = 49383,
    FIRST_AID = 3273,
    FISHING = 7620,
    HERB_GATHERING = 2366,
    INSCRIPTION = 45357,
    JEWELCRAFTING = 25229,
    MINING = 2575,
    SKINNING = 8613,
    TAILORING = 3908
};

bool OpenLootAction::Execute(Event /*event*/)
{
    LootObject lootObject = AI_VALUE(LootObject, "loot target");
    bool result = DoLoot(lootObject);
    if (result)
    {
        // Mark as pending instead of removing immediately
        AI_VALUE(LootObjectStack*, "available loot")->MarkAsPending(lootObject.guid);
        context->GetValue<LootObject>("loot target")->Set(LootObject());
    }
    return result;
}

bool OpenLootAction::DoLoot(LootObject& lootObject)
{
    if (lootObject.IsEmpty())
        return false;

    bool debugLoot = botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT);
    
    if (debugLoot)
    {
        std::ostringstream out;
        out << "DoLoot: Attempting to loot " << lootObject.guid.ToString();
        botAI->TellMaster(out.str());
    }

    Creature* creature = botAI->GetCreature(lootObject.guid);
    if (creature && bot->GetDistance(creature) > INTERACTION_DISTANCE - 2.0f)
    {
        if (debugLoot)
            botAI->TellMaster("DoLoot: Creature too far away");
        return false;
    }

    // Dismount if the bot is mounted
    if (bot->IsMounted())
    {
        bot->Dismount();
        botAI->SetNextCheckDelay(sPlayerbotAIConfig->lootDelay); // Small delay to avoid animation issues
    }

    if (creature && creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
    {
        WorldPacket* packet = new WorldPacket(CMSG_LOOT, 8);
        *packet << lootObject.guid;
        bot->GetSession()->QueuePacket(packet);
        // bot->GetSession()->HandleLootOpcode(packet);
        botAI->SetNextCheckDelay(sPlayerbotAIConfig->lootDelay);
        return true;
    }

    if (bot->isMoving())
    {
        bot->StopMoving();
    }

    if (creature)
    {
        SkillType skill = creature->GetCreatureTemplate()->GetRequiredLootSkill();
        if (!CanOpenLock(skill, lootObject.reqSkillValue))
            return false;

        switch (skill)
        {
            case SKILL_ENGINEERING:
                return botAI->HasSkill(SKILL_ENGINEERING) ? botAI->CastSpell(ENGINEERING, creature) : false;
            case SKILL_HERBALISM:
                return botAI->HasSkill(SKILL_HERBALISM) ? botAI->CastSpell(32605, creature) : false;
            case SKILL_MINING:
                return botAI->HasSkill(SKILL_MINING) ? botAI->CastSpell(32606, creature) : false;
            default:
                return botAI->HasSkill(SKILL_SKINNING) ? botAI->CastSpell(SKINNING, creature) : false;
        }
    }

    GameObject* go = botAI->GetGameObject(lootObject.guid);
    if (go && bot->GetDistance(go) > INTERACTION_DISTANCE - 2.0f)
    {
        if (debugLoot)
        {
            std::ostringstream out;
            out << "DoLoot: GameObject too far away (distance: " << bot->GetDistance(go) << ")";
            botAI->TellMaster(out.str());
        }
        return false;
    }
    
    if (go && debugLoot)
    {
        std::ostringstream out;
        out << "DoLoot: Found GameObject " << go->GetName() << " (Entry: " << go->GetEntry() 
            << ", Type: " << go->GetGoType() << ", State: " << go->GetGoState() << ")";
        botAI->TellMaster(out.str());
    }

    if (go && (go->GetGoState() != GO_STATE_READY))
        return false;

    // This prevents dungeon chests like Tribunal Chest (Halls of Stone) from being ninja'd by the bots
    // This prevents raid chests like Gunship Armory (ICC) from being ninja'd by the bots
    // Allows quest objects
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
            if (debugLoot)
                botAI->TellMaster("DoLoot: Cannot loot chest - no quest requirement met and has interaction flags");
            return false;
        }
        else if (debugLoot)
        {
            botAI->TellMaster("DoLoot: Chest accessible for quest");
        }
    }

    if (lootObject.skillId == SKILL_MINING)
    {
        if (debugLoot)
            botAI->TellMaster("DoLoot: Attempting mining");
        return botAI->HasSkill(SKILL_MINING) ? botAI->CastSpell(MINING, bot) : false;
    }

    if (lootObject.skillId == SKILL_HERBALISM)
    {
        if (debugLoot)
            botAI->TellMaster("DoLoot: Attempting herbalism");
        return botAI->HasSkill(SKILL_HERBALISM) ? botAI->CastSpell(HERB_GATHERING, bot) : false;
    }

    // For key-locked chests, find and cast the key's spell using the key item
    if (go && lootObject.reqItem > 0 && bot->HasItemCount(lootObject.reqItem, 1))
    {
        uint32 keySpell = GetKeySpell(lootObject.reqItem);
        if (keySpell)
        {
            // Find the key item in bot's inventory (check keyring first, then fallback to bags)
            Item* keyItem = nullptr;
            
            // Check keyring slots first (keys are automatically stored here)
            for (uint8 slot = KEYRING_SLOT_START; slot < KEYRING_SLOT_END; ++slot)
            {
                if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                {
                    if (item->GetEntry() == lootObject.reqItem)
                    {
                        keyItem = item;
                        if (debugLoot)
                        {
                            std::ostringstream out;
                            out << "DoLoot: Found key item " << lootObject.reqItem << " in keyring slot " << slot;
                            botAI->TellMaster(out.str());
                        }
                        break;
                    }
                }
            }
            
            // Fallback: Check main inventory slots if not in keyring
            if (!keyItem)
            {
                for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                {
                    if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    {
                        if (item->GetEntry() == lootObject.reqItem)
                        {
                            keyItem = item;
                            if (debugLoot)
                            {
                                std::ostringstream out;
                                out << "DoLoot: Found key item " << lootObject.reqItem << " in main inventory slot " << slot;
                                botAI->TellMaster(out.str());
                            }
                            break;
                        }
                    }
                }
            }
            
            // Final fallback: Check bags if still not found
            if (!keyItem)
            {
                for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
                {
                    if (Bag* pBag = bot->GetBagByPos(bag))
                    {
                        for (uint32 slot = 0; slot < pBag->GetBagSize(); ++slot)
                        {
                            if (Item* item = pBag->GetItemByPos(slot))
                            {
                                if (item->GetEntry() == lootObject.reqItem)
                                {
                                    keyItem = item;
                                    if (debugLoot)
                                    {
                                        std::ostringstream out;
                                        out << "DoLoot: Found key item " << lootObject.reqItem << " in bag " << bag << " slot " << slot;
                                        botAI->TellMaster(out.str());
                                    }
                                    break;
                                }
                            }
                        }
                        if (keyItem) break;
                    }
                }
            }
            
            if (keyItem)
            {
                if (debugLoot)
                {
                    std::ostringstream out;
                    out << "DoLoot: Casting key spell " << keySpell << " for key item " << lootObject.reqItem << " on GameObject " << go->GetName();
                    botAI->TellMaster(out.str());
                }
                
                // Use the new GameObject-specific CastSpell method with the key item
                bool result = botAI->CastSpell(keySpell, go, keyItem);
                if (debugLoot)
                {
                    std::ostringstream out;
                    out << "DoLoot: Key spell cast result: " << (result ? "SUCCESS" : "FAILED");
                    botAI->TellMaster(out.str());
                }
                return result;
            }
            else if (debugLoot)
            {
                std::ostringstream out;
                out << "DoLoot: Could not find key item " << lootObject.reqItem << " in inventory";
                botAI->TellMaster(out.str());
            }
        }
        else if (debugLoot)
        {
            std::ostringstream out;
            out << "DoLoot: No spell found for key item " << lootObject.reqItem;
            botAI->TellMaster(out.str());
        }
    }
    
    uint32 spellId = GetOpeningSpell(lootObject);
    if (!spellId)
    {
        if (debugLoot)
            botAI->TellMaster("DoLoot: No opening spell found for object");
        return false;
    }

    if (debugLoot)
    {
        std::ostringstream out;
        out << "DoLoot: Casting opening spell " << spellId << " on target " << lootObject.guid.ToString();
        botAI->TellMaster(out.str());
    }

    bool result = botAI->CastSpell(spellId, bot);

    if (debugLoot)
    {
        std::ostringstream out;
        out << "DoLoot: Spell cast result: " << (result ? "SUCCESS" : "FAILED");
        botAI->TellMaster(out.str());
    }

    return result;
}

uint32 OpenLootAction::GetOpeningSpell(LootObject& lootObject)
{
    bool debugLoot = botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT);
    
    if (GameObject* go = botAI->GetGameObject(lootObject.guid))
    {
        if (go->isSpawned())
        {
            uint32 spell = GetOpeningSpell(lootObject, go);
            if (debugLoot)
            {
                std::ostringstream out;
                out << "GetOpeningSpell: Found spell " << spell << " for GameObject " << go->GetName();
                botAI->TellMaster(out.str());
            }
            return spell;
        }
        else if (debugLoot)
        {
            botAI->TellMaster("GetOpeningSpell: GameObject not spawned");
        }
    }
    else if (debugLoot)
    {
        botAI->TellMaster("GetOpeningSpell: GameObject not found");
    }

    return 0;
}

uint32 OpenLootAction::GetOpeningSpell(LootObject& lootObject, GameObject* go)
{
    for (PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        uint32 spellId = itr->first;

        if (itr->second->State == PLAYERSPELL_REMOVED || !itr->second->Active)
            continue;

        if (spellId == MINING || spellId == HERB_GATHERING)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            continue;

        if (CanOpenLock(lootObject, spellInfo, go))
            return spellId;
    }

    for (uint32 spellId = 0; spellId < sSpellMgr->GetSpellInfoStoreSize(); spellId++)
    {
        if (spellId == MINING || spellId == HERB_GATHERING)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            continue;

        if (CanOpenLock(lootObject, spellInfo, go))
            return spellId;
    }

    return sPlayerbotAIConfig->openGoSpell;
}

uint32 OpenLootAction::GetKeySpell(uint32 keyItemId)
{
    bool debugLoot = botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT);
    
    ItemTemplate const* keyItem = sObjectMgr->GetItemTemplate(keyItemId);
    if (!keyItem)
    {
        if (debugLoot)
        {
            std::ostringstream out;
            out << "GetKeySpell: Key item " << keyItemId << " not found in database";
            botAI->TellMaster(out.str());
        }
        return 0;
    }
    
    // Check all spell entries in the key item
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        uint32 spellId = keyItem->Spells[i].SpellId;
        if (!spellId)
            continue;
            
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            continue;
            
        // Look for opening spells (SPELL_EFFECT_OPEN_LOCK)
        for (uint8 effIndex = 0; effIndex < MAX_SPELL_EFFECTS; ++effIndex)
        {
            if (spellInfo->Effects[effIndex].Effect == SPELL_EFFECT_OPEN_LOCK)
            {
                if (debugLoot)
                {
                    std::ostringstream out;
                    out << "GetKeySpell: Found opening spell " << spellId << " in key item " << keyItemId;
                    botAI->TellMaster(out.str());
                }
                return spellId;
            }
        }
    }
    
    if (debugLoot)
    {
        std::ostringstream out;
        out << "GetKeySpell: No opening spell found in key item " << keyItemId;
        botAI->TellMaster(out.str());
    }
    
    return 0;
}

bool OpenLootAction::CanOpenLock(LootObject& /*lootObject*/, SpellInfo const* spellInfo, GameObject* go)
{
    for (uint8 effIndex = 0; effIndex <= EFFECT_2; effIndex++)
    {
        if (spellInfo->Effects[effIndex].Effect != SPELL_EFFECT_OPEN_LOCK &&
            spellInfo->Effects[effIndex].Effect != SPELL_EFFECT_SKINNING)
            return false;

        uint32 lockId = go->GetGOInfo()->GetLockId();
        if (!lockId)
            return false;

        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
            return false;

        for (uint8 j = 0; j < 8; ++j)
        {
            switch (lockInfo->Type[j])
            {
                /*
                case LOCK_KEY_ITEM:
                    return true;
                */
                case LOCK_KEY_SKILL:
                {
                    if (uint32(spellInfo->Effects[effIndex].MiscValue) != lockInfo->Index[j])
                        continue;

                    uint32 skillId = SkillByLockType(LockType(lockInfo->Index[j]));
                    if (skillId == SKILL_NONE)
                        return true;

                    if (CanOpenLock(skillId, lockInfo->Skill[j]))
                        return true;
                }
            }
        }
    }

    return false;
}

bool OpenLootAction::CanOpenLock(uint32 skillId, uint32 reqSkillValue)
{
    uint32 skillValue = bot->GetSkillValue(skillId);
    return skillValue >= reqSkillValue || !reqSkillValue;
}

/*
uint32 StoreLootAction::RoundPrice(double price)
{
    if (price < 100)
    {
        return (uint32)price;
    }

    if (price < 10000)
    {
        return (uint32)(price / 100.0) * 100;
    }

    if (price < 100000)
    {
        return (uint32)(price / 1000.0) * 1000;
    }

    return (uint32)(price / 10000.0) * 10000;
}

bool StoreLootAction::AuctionItem(uint32 itemId)
{
    ItemTemplate const* proto = sItemStorage.LookupEntry<ItemTemplate>(itemId);
    if (!proto)
        return false;

    if (!proto || proto->Bonding == BIND_WHEN_PICKED_UP || proto->Bonding == BIND_QUEST_ITEM)
        return false;

    Item* oldItem = bot->GetItemByEntry(itemId);
    if (!oldItem)
        return false;

    AuctionHouseEntry const* ahEntry = AuctionHouseMgr::GetAuctionHouseEntry(unit->getFaction());
    if (!ahEntry)
        return false;

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(ahEntry);

    uint32 price = oldItem->GetCount() * proto->BuyPrice * sRandomPlayerbotMgr->GetBuyMultiplier(bot);

uint32 stackCount = urand(1, proto->GetMaxStackSize());
    if (!price || !stackCount)
        return false;

    if (!stackCount)
        stackCount = 1;

    if (urand(0, 100) <= sAhBotConfig.underPriceProbability * 100)
        price = price * 100 / urand(100, 200);

    uint32 bidPrice = RoundPrice(stackCount * price);
    uint32 buyoutPrice = RoundPrice(stackCount * urand(price, 4 * price / 3));

    Item* item = Item::CreateItem(proto->ItemId, stackCount);
    if (!item)
        return false;

    uint32 auction_time = uint32(urand(8, 24) * HOUR * sWorld->getConfig(CONFIG_FLOAT_RATE_AUCTION_TIME));

    AuctionEntry* auctionEntry = new AuctionEntry;
    auctionEntry->Id = sObjectMgr->GenerateAuctionID();
    auctionEntry->itemGuidLow = item->GetGUID().GetCounter();
    auctionEntry->itemTemplate = item->GetEntry();
    auctionEntry->itemCount = item->GetCount();
    auctionEntry->itemRandomPropertyId = item->GetItemRandomPropertyId();
    auctionEntry->owner = bot->GetGUID().GetCounter();
    auctionEntry->startbid = bidPrice;
    auctionEntry->bidder = 0;
    auctionEntry->bid = 0;
    auctionEntry->buyout = buyoutPrice;
    auctionEntry->expireTime = time(nullptr) + auction_time;
    //auctionEntry->moneyDeliveryTime = 0;
    auctionEntry->deposit = 0;
    auctionEntry->auctionHouseEntry = ahEntry;

    auctionHouse->AddAuction(auctionEntry);

    sAuctionMgr.AddAItem(item);

    item->SaveToDB();
    auctionEntry->SaveToDB();

    LOG_ERROR("playerbots", "AhBot {} added {} of {} to auction {} for {}..{}", bot->GetName().c_str(), stackCount,
proto->Name1.c_str(), 1, bidPrice, buyoutPrice);

    if (oldItem->GetCount() > stackCount)
        oldItem->SetCount(oldItem->GetCount() - stackCount);
    else
        bot->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);

    return true;
}
*/

bool StoreLootAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());  // (8+1+4+1+1+4+4+4+4+4+1)
    ObjectGuid guid;
    uint8 loot_type;
    uint32 gold = 0;
    uint8 items = 0;

    p.rpos(0);
    p >> guid;       // 8 corpse guid
    p >> loot_type;  // 1 loot type

    if (p.size() > 10)
    {
        p >> gold;   // 4 money on corpse
        p >> items;  // 1 number of items on corpse
    }

    bot->SetLootGUID(guid);

    if (gold > 0)
    {
        WorldPacket* packet = new WorldPacket(CMSG_LOOT_MONEY, 0);
        bot->GetSession()->QueuePacket(packet);
        // bot->GetSession()->HandleLootMoneyOpcode(packet);
    }

    uint8 totalAvailableItems = items;
    uint8 itemsSkipped = 0;
    
    for (uint8 i = 0; i < items; ++i)
    {
        uint32 itemid;
        uint32 itemcount;
        uint8 lootslot_type;
        uint8 itemindex;

        p >> itemindex;
        p >> itemid;
        p >> itemcount;
        p.read_skip<uint32>();  // display id
        p.read_skip<uint32>();  // randomSuffix
        p.read_skip<uint32>();  // randomPropertyId
        p >> lootslot_type;     // 0 = can get, 1 = look only, 2 = master get

        if (lootslot_type != LOOT_SLOT_TYPE_ALLOW_LOOT && lootslot_type != LOOT_SLOT_TYPE_OWNER)
        {
            itemsSkipped++;
            continue;
        }

        if (loot_type != LOOT_SKINNING && !IsLootAllowed(itemid, botAI))
        {
            itemsSkipped++;
            continue;
        }

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemid);
        if (!proto)
        {
            itemsSkipped++;
            continue;
        }

        if (!botAI->HasActivePlayerMaster() && AI_VALUE(uint8, "bag space") > 80)
        {
            // Check item usage to determine if it should be prioritized
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", itemid);
            
            // Prioritize useful items (anything not vendor trash, AH items, or completely useless)
            bool isUsefulItem = (usage != ITEM_USAGE_NONE && usage != ITEM_USAGE_VENDOR && usage != ITEM_USAGE_AH);
            
            if (isUsefulItem)
            {
                // Check if bot has any free bag slots for important items
                // (quest items, useful equipment, skill items, consumables, etc.)
                uint32 totalfree = 0;
                
                // Check main bag slots
                for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
                {
                    if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                        ++totalfree;
                }
                
                // Check additional bag slots
                for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
                {
                    const Bag* const pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
                    if (pBag)
                    {
                        ItemTemplate const* pBagProto = pBag->GetTemplate();
                        if (pBagProto->Class == ITEM_CLASS_CONTAINER && pBagProto->SubClass == ITEM_SUBCLASS_CONTAINER)
                        {
                            totalfree += pBag->GetFreeSlots();
                        }
                    }
                }
                
                // Allow important item if there's any free space
                if (totalfree == 0)
                {
                    itemsSkipped++;
                    continue; // Skip only if absolutely no space
                }
            }
            else
            {
                // Apply normal restrictions for non-quest items
                uint32 maxStack = proto->GetMaxStackSize();
                if (maxStack == 1)
                {
                    itemsSkipped++;
                    continue;
                }

                std::vector<Item*> found = parseItems(chat->FormatItem(proto));

                bool hasFreeStack = false;

                for (auto stack : found)
                {
                    if (stack->GetCount() + itemcount < maxStack)
                    {
                        hasFreeStack = true;
                        break;
                    }
                }

                if (!hasFreeStack)
                {
                    itemsSkipped++;
                    continue;
                }
            }
        }

        Player* master = botAI->GetMaster();
        if (sRandomPlayerbotMgr->IsRandomBot(bot) && master)
        {
            uint32 price = itemcount * proto->BuyPrice * sRandomPlayerbotMgr->GetBuyMultiplier(bot) + gold;
            if (price)
                sRandomPlayerbotMgr->AddTradeDiscount(bot, master, price);

            if (Group* group = bot->GetGroup())
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    if (ref->GetSource() != bot)
                        sGuildTaskMgr->CheckItemTask(itemid, itemcount, ref->GetSource(), bot);
        }

        WorldPacket* packet = new WorldPacket(CMSG_AUTOSTORE_LOOT_ITEM, 1);
        *packet << itemindex;
        bot->GetSession()->QueuePacket(packet);
        // bot->GetSession()->HandleAutostoreLootItemOpcode(packet);
        botAI->SetNextCheckDelay(sPlayerbotAIConfig->lootDelay);

        if (proto->Quality > ITEM_QUALITY_NORMAL && !urand(0, 50) && botAI->HasStrategy("emote", BOT_STATE_NON_COMBAT) && sPlayerbotAIConfig->randomBotEmote)
            botAI->PlayEmote(TEXT_EMOTE_CHEER);

        if (proto->Quality >= ITEM_QUALITY_RARE && !urand(0, 1) && botAI->HasStrategy("emote", BOT_STATE_NON_COMBAT) && sPlayerbotAIConfig->randomBotEmote)
            botAI->PlayEmote(TEXT_EMOTE_CHEER);

        BroadcastHelper::BroadcastLootingItem(botAI, bot, proto);
    }

    // Mark loot based on whether items were skipped
    if (itemsSkipped > 0 && totalAvailableItems > 0)
    {
        // Some items were left behind, mark as partially looted
        AI_VALUE(LootObjectStack*, "available loot")->MarkAsPartiallyLooted(guid);
    }
    else
    {
        // All items were taken or no items were available, mark as completed
        AI_VALUE(LootObjectStack*, "available loot")->MarkAsCompleted(guid);
    }

    // release loot
    WorldPacket* packet = new WorldPacket(CMSG_LOOT_RELEASE, 8);
    *packet << guid;
    bot->GetSession()->QueuePacket(packet);
    // bot->GetSession()->HandleLootReleaseOpcode(packet);
    return true;
}

bool StoreLootAction::IsLootAllowed(uint32 itemid, PlayerbotAI* botAI)
{
    AiObjectContext* context = botAI->GetAiObjectContext();
    LootStrategy* lootStrategy = AI_VALUE(LootStrategy*, "loot strategy");

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemid);
    if (!proto)
        return false;

    std::set<uint32>& lootItems = AI_VALUE(std::set<uint32>&, "always loot list");
    if (lootItems.find(itemid) != lootItems.end())
        return true;

    uint32 max = proto->MaxCount;
    if (max > 0 && botAI->GetBot()->HasItemCount(itemid, max, true))
        return false;

    if (proto->StartQuest)
    {
        return true;
    }

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 entry = botAI->GetBot()->GetQuestSlotQuestId(slot);
        Quest const* quest = sObjectMgr->GetQuestTemplate(entry);
        if (!quest)
            continue;

        for (uint8 i = 0; i < 4; i++)
        {
            if (quest->RequiredItemId[i] == itemid)
            {
                // if (AI_VALUE2(uint32, "item count", proto->Name1) < quest->RequiredItemCount[i])
                // {
                //     if (botAI->GetMaster() && sPlayerbotAIConfig->syncQuestWithPlayer)
                //         return false; //Quest is autocomplete for the bot so no item needed.
                // }

                return true;
            }
        }
    }

    // if (proto->Bonding == BIND_QUEST_ITEM ||  //Still testing if it works ok without these lines.
    //     proto->Bonding == BIND_QUEST_ITEM1 || //Eventually this has to be removed.
    //     proto->Class == ITEM_CLASS_QUEST)
    //{

    bool canLoot = lootStrategy->CanLoot(proto, context);
    // if (canLoot && proto->Bonding == BIND_WHEN_PICKED_UP && botAI->HasActivePlayerMaster())
    // canLoot = sPlayerbotAIConfig->IsInRandomAccountList(botAI->GetBot()->GetSession()->GetAccountId());

    return canLoot;
}

bool ReleaseLootAction::Execute(Event /*event*/)
{
    GuidVector gos = context->GetValue<GuidVector>("nearest game objects")->Get();
    for (ObjectGuid const guid : gos)
    {
        WorldPacket* packet = new WorldPacket(CMSG_LOOT_RELEASE, 8);
        *packet << guid;
        bot->GetSession()->QueuePacket(packet);
    }

    GuidVector corpses = context->GetValue<GuidVector>("nearest corpses")->Get();
    for (ObjectGuid const guid : corpses)
    {
        WorldPacket* packet = new WorldPacket(CMSG_LOOT_RELEASE, 8);
        *packet << guid;
        bot->GetSession()->QueuePacket(packet);
    }

    return true;
}
