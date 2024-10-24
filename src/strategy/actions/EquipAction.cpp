/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "EquipAction.h"

#include "Event.h"
#include "ItemCountValue.h"
#include "ItemUsageValue.h"
#include "Playerbots.h"
#include "StatsWeightCalculator.h"

bool EquipAction::Execute(Event event)
{
    std::string const text = event.getParam();
    ItemIds ids = chat->parseItems(text);
    EquipItems(ids);
    return true;
}

void EquipAction::EquipItems(ItemIds ids)
{
    for (ItemIds::iterator i = ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        EquipItem(&visitor);
    }
}

// Return bagslot with smalest bag.
uint8 EquipAction::GetSmallestBagSlot()
{
    int8 curBag = 0;
    uint32 curSlots = 0;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        const Bag* const pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (pBag)
        {
            if (curBag > 0 && curSlots < pBag->GetBagSize())
                continue;

            curBag = bag;
            curSlots = pBag->GetBagSize();
        }
        else
            return bag;
    }

    return curBag;
}

void EquipAction::EquipItem(FindItemVisitor* visitor)
{
    IterateItems(visitor);
    std::vector<Item*> items = visitor->GetResult();
    if (!items.empty())
        EquipItem(*items.begin());
}

void EquipAction::EquipItem(Item* item)
{
    uint8 bagIndex = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    uint32 itemId = item->GetTemplate()->ItemId;

    // Define variables for the slot types based on the item type (ring or trinket)
    uint8 slot1, slot2;
    std::string itemType;
    
    if (item->GetTemplate()->InventoryType == INVTYPE_TRINKET)
    {
        slot1 = EQUIPMENT_SLOT_TRINKET1;
        slot2 = EQUIPMENT_SLOT_TRINKET2;
        itemType = "trinket";
    }
    else if (item->GetTemplate()->InventoryType == INVTYPE_FINGER)
    {
        slot1 = EQUIPMENT_SLOT_FINGER1;
        slot2 = EQUIPMENT_SLOT_FINGER2;
        itemType = "ring";
    }
    else
    {
        // Handle other item types like ammo, bags, etc.
        if (item->GetTemplate()->InventoryType == INVTYPE_AMMO)
        {
            bot->SetAmmo(itemId);
        }
        else
        {
            bool equippedBag = false;
            if (item->GetTemplate()->Class == ITEM_CLASS_CONTAINER)
            {
                Bag* pBag = (Bag*)item;
                uint8 newBagSlot = GetSmallestBagSlot();
                if (newBagSlot > 0)
                {
                    uint16 src = ((bagIndex << 8) | slot);
                    uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | newBagSlot);
                    bot->SwapItem(src, dst);
                    equippedBag = true;
                }
            }

            if (!equippedBag)
            {
                WorldPacket packet(CMSG_AUTOEQUIP_ITEM, 2);
                packet << bagIndex << slot;
                bot->GetSession()->HandleAutoEquipItemOpcode(packet);
            }
        }

        // Whisper master when equipping an item
        std::ostringstream out;
        out << "Equipping " << chat->FormatItem(item->GetTemplate());
        botAI->TellMaster(out.str());
        return;
    }

    // Retrieve the current items in the two slots
    Item* item1 = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot1);
    Item* item2 = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot2);

    // Check if the first slot is empty and auto-equip the new item if so
    if (!item1)
    {
        WorldPacket packet(CMSG_AUTOEQUIP_ITEM, 2);
        packet << bagIndex << slot;
        bot->GetSession()->HandleAutoEquipItemOpcode(packet);
        botAI->TellMaster("Equipping new " + itemType + " in slot 1: " + chat->FormatItem(item->GetTemplate()));
        return;
    }
    else if (!item2)
    {
        WorldPacket packet(CMSG_AUTOEQUIP_ITEM, 2);
        packet << bagIndex << slot;
        bot->GetSession()->HandleAutoEquipItemOpcode(packet);
        botAI->TellMaster("Equipping new " + itemType + " in slot 2: " + chat->FormatItem(item->GetTemplate()));
        return;
    }

    // Compare and replace the weaker item in slot 1 or 2 using SwapItem
    if (IsBetterItem(item, item1))  // Replace the first slot if the new one is better
    {
        uint16 src = ((bagIndex << 8) | slot);  // Source is from the current bag/slot
        uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | slot1);  // Destination is slot 1
        bot->SwapItem(src, dst);
        botAI->TellMaster("Replacing " + itemType + " 1: " + chat->FormatItem(item1->GetTemplate()) +
                          " with new " + itemType + ": " + chat->FormatItem(item->GetTemplate()));
        return;
    }
    else if (IsBetterItem(item, item2))  // Replace the second slot if the new one is better
    {
        uint16 src = ((bagIndex << 8) | slot);  // Source is from the current bag/slot
        uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | slot2);  // Destination is slot 2
        bot->SwapItem(src, dst);
        botAI->TellMaster("Replacing " + itemType + " 2: " + chat->FormatItem(item2->GetTemplate()) +
                          " with new " + itemType + ": " + chat->FormatItem(item->GetTemplate()));
        return;
    }

    // No upgrade found for either slot
    botAI->TellMaster("New " + itemType + " is not better than the currently equipped " + itemType + "s.");
}

// Helper function to compare items (trinkets, rings, or any equippable item)
bool EquipAction::IsBetterItem(Item* newItem, Item* currentItem)
{
    if (!currentItem)
        return true;  // No item equipped, so the new one is better

    // Use the StatsWeightCalculator to compare item scores
    StatsWeightCalculator calculator(bot);
    float newItemScore = calculator.CalculateItem(newItem->GetTemplate()->ItemId);
    float currentItemScore = calculator.CalculateItem(currentItem->GetTemplate()->ItemId);

    return newItemScore > currentItemScore;
}

bool EquipUpgradesAction::Execute(Event event)
{
    if (!sPlayerbotAIConfig->autoEquipUpgradeLoot && !sRandomPlayerbotMgr->IsRandomBot(bot))
        return false;

    if (event.GetSource() == "trade status")
    {
        WorldPacket p(event.getPacket());
        p.rpos(0);
        uint32 status;
        p >> status;

        if (status != TRADE_STATUS_TRADE_ACCEPT)
            return false;
    }

    ListItemsVisitor visitor;
    IterateItems(&visitor, ITERATE_ITEMS_IN_BAGS);

    ItemIds items;
    for (std::map<uint32, uint32>::iterator i = visitor.items.begin(); i != visitor.items.end(); ++i)
    {
        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", i->first);
        if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_BAD_EQUIP)
        {
            // LOG_INFO("playerbots", "Bot {} <{}> auto equips item {} ({})", bot->GetGUID().ToString().c_str(),
            // bot->GetName().c_str(), i->first, usage == 1 ? "no item in slot" : usage == 2 ? "replace" : usage == 3 ?
            // "wrong item but empty slot" : "");
            items.insert(i->first);
        }
    }

    EquipItems(items);
    return true;
}

bool EquipUpgradeAction::Execute(Event event)
{
    ListItemsVisitor visitor;
    IterateItems(&visitor, ITERATE_ITEMS_IN_BAGS);

    ItemIds items;
    for (std::map<uint32, uint32>::iterator i = visitor.items.begin(); i != visitor.items.end(); ++i)
    {
        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", i->first);
        if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_BAD_EQUIP)
        {
            items.insert(i->first);
        }
    }
    EquipItems(items);
    return true;
}
