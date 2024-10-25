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

    // Define variables for the slot types based on the item type (ring, trinket, or weapon)
    uint8 slot1 = 0, slot2 = 0;
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
    else if (item->GetTemplate()->InventoryType == INVTYPE_WEAPON ||
             item->GetTemplate()->InventoryType == INVTYPE_WEAPONMAINHAND ||
             item->GetTemplate()->InventoryType == INVTYPE_WEAPONOFFHAND ||
             item->GetTemplate()->InventoryType == INVTYPE_2HWEAPON)
    {
        itemType = "weapon";
        bool isTwoHanded = item->GetTemplate()->InventoryType == INVTYPE_2HWEAPON;
        bool isMainhand = item->GetTemplate()->InventoryType == INVTYPE_WEAPONMAINHAND;
        bool isOffhand = item->GetTemplate()->InventoryType == INVTYPE_WEAPONOFFHAND;

        // Assign the correct slot based on the type of weapon
        if (isMainhand)
        {
            slot1 = EQUIPMENT_SLOT_MAINHAND;
            slot2 = 0;  // Main-hand only weapon, no off-hand use
            botAI->TellMaster("Equipping main-hand only weapon in main-hand slot.");
        }
        else if (isOffhand)
        {
            slot1 = EQUIPMENT_SLOT_OFFHAND;
            slot2 = 0;  // Off-hand only weapon, no main-hand use
            botAI->TellMaster("Equipping off-hand only weapon in off-hand slot.");
        }
        else
        {
            slot1 = EQUIPMENT_SLOT_MAINHAND;
            slot2 = EQUIPMENT_SLOT_OFFHAND;  // Consider off-hand slot for dual wielders

            // If the bot cannot dual wield, we don't assign an off-hand slot
            if (!bot->CanDualWield())
            {
                slot2 = 0;  // Prevent off-hand use if the bot cannot dual wield
                botAI->TellMaster("Bot cannot dual wield. Only main-hand will be used.");
            }
            else if (isTwoHanded && !bot->CanTitanGrip())
            {
                // If it's a two-handed weapon and the bot doesn't have Titan's Grip, only use main-hand
                slot2 = 0;
                botAI->TellMaster("Bot cannot use two-handed weapons in off-hand. Only main-hand will be used.");
            }
            else if (bot->CanDualWield())
            {
                botAI->TellMaster("Bot can dual wield. Main-hand and off-hand slots will be used if applicable.");
            }
        }
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

        std::ostringstream out;
        out << "Equipping " << chat->FormatItem(item->GetTemplate());
        botAI->TellMaster(out.str());
        return;
    }

    // Retrieve the current items in the two slots (ring, trinket, or weapon)
    Item* item1 = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot1);
    Item* item2 = slot2 ? bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot2) : nullptr;

    // Check if the first slot is empty and auto-equip the new item if so
    if (!item1)
    {
        WorldPacket packet(CMSG_AUTOEQUIP_ITEM, 2);
        packet << bagIndex << slot;
        bot->GetSession()->HandleAutoEquipItemOpcode(packet);
        botAI->TellMaster("Equipping new " + itemType + " in slot 1: " + chat->FormatItem(item->GetTemplate()));
        return;
    }
    else if (!item2 && slot2)  // Handle off-hand for dual wield/trinket
    {
        WorldPacket packet(CMSG_AUTOEQUIP_ITEM, 2);
        packet << bagIndex << slot;
        bot->GetSession()->HandleAutoEquipItemOpcode(packet);
        botAI->TellMaster("Equipping new " + itemType + " in slot 2: " + chat->FormatItem(item->GetTemplate()));
        return;
    }

    // Compare and replace the weaker item in slot 1 or slot 2
    StatsWeightCalculator calculator(bot);
    float item1Score = item1 ? calculator.CalculateItem(item1->GetTemplate()->ItemId) : 0.0f;
    float item2Score = item2 ? calculator.CalculateItem(item2->GetTemplate()->ItemId) : 0.0f;
    float newItemScore = calculator.CalculateItem(item->GetTemplate()->ItemId);

    botAI->TellMaster("New item score: " + std::to_string(newItemScore) +
                      ", Slot 1 score: " + std::to_string(item1Score) +
                      ", Slot 2 score: " + std::to_string(item2Score));

    // Determine the weaker slot (either slot1 or slot2) based on score
    uint8 weakestSlot = (item1Score < item2Score || !slot2) ? slot1 : slot2;
    Item* weakestItem = (weakestSlot == slot1) ? item1 : item2;
    float weakestScore = (weakestSlot == slot1) ? item1Score : item2Score;

    if (newItemScore > weakestScore)  // If the new item is better than the weakest equipped item
    {
        uint16 src = ((bagIndex << 8) | slot);  // Source is the new item's bag and slot
        uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | weakestSlot);  // Destination is the weakest slot

        bot->SwapItem(src, dst);
        botAI->TellMaster("Replacing " + itemType + " in " + (weakestSlot == slot1 ? "slot 1" : "slot 2") +
                      ": " + chat->FormatItem(weakestItem->GetTemplate()) +
                      " with new " + itemType + ": " + chat->FormatItem(item->GetTemplate()));
        return;
    }
    else
    {
        botAI->TellMaster("New " + itemType + " (" + chat->FormatItem(item->GetTemplate()) +
                      ") is not better than the currently equipped " + itemType +
                      "s: Slot 1 (" + chat->FormatItem(item1->GetTemplate()) +
                      "), Slot 2 (" + (item2 ? chat->FormatItem(item2->GetTemplate()) : "none") + ").");
    }

    // No upgrade found for either slot
    botAI->TellMaster("New " + itemType + " (" + chat->FormatItem(item->GetTemplate()) + 
            ") is not better than the currently equipped " + itemType + 
            "s: Slot 1 (" + chat->FormatItem(item1->GetTemplate()) + 
            "), Slot 2 (" + (item2 ? chat->FormatItem(item2->GetTemplate()) : "none") + ").");

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
