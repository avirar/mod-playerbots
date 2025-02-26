
#include "OpenItemAction.h"
//#include "PlayerbotAI.h"
// #include "ItemTemplate.h"
#include "WorldPacket.h"
// #include "Player.h"
// #include "ObjectMgr.h"

#include "ChatHelper.h"
#include "ItemUsageValue.h"
#include "Playerbots.h"
/*
bool OpenItemAction::Execute(Event event)
{
    bool foundOpenable = false;

    // Check items in the bags
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* bagItem = bot->GetBagByPos(bag);
        if (!bagItem)
            continue;

        for (uint32 slot = 0; slot < bagItem->GetBagSize(); ++slot)
        {
            Item* item = bot->GetItemByPos(bag, slot);

            if (item && CanOpenItem(item))
            {
                // Create an instance of UnlockItemAction
                UnlockItemAction unlockAction(botAI);

                // Attempt to unlock the item first
                if (unlockAction.Execute(event))
                {
                    OpenItem(item, bag, slot);
                    foundOpenable = true;
                }
            }
        }
    }

    // If no openable items were found
    if (!foundOpenable)
    {
        botAI->TellError("No openable items in inventory.");
    }

    return foundOpenable;
}

bool OpenItemAction::CanOpenItem(Item* item)
{
    if (!item)
        return false;

    ItemTemplate const* itemTemplate = item->GetTemplate();
    if (!itemTemplate)
        return false;

    // Check if the item is openable
    return (itemTemplate->Flags & ITEM_FLAG_HAS_LOOT);
}
*/
void OpenItemAction::OpenItem(Item* item)
{
    uint8 bag = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    WorldPacket packet(CMSG_OPEN_ITEM);
    packet << bag << slot;
    bot->GetSession()->HandleOpenItemOpcode(packet);

    std::ostringstream out;
    out << "Opened item: " << item->GetTemplate()->Name1;
    botAI->TellMaster(out.str());
}

bool OpenItemAction::Execute(Event event)
{
    Player* bot = ai->GetBot();

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_0; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = bot->GetBagByPos(bagSlot);
        if (!bag)
            continue;

        for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
        {
            Item* item = bag->GetItemByPos(slot);
            if (!item)
                continue;

            ItemTemplate const* proto = item->GetTemplate();
            if (!proto)
                continue;

            // Check if the item is openable
            if ((proto->Flags & ITEM_FLAG_HAS_LOOT) && (proto->LockID == 0 || !item->IsLocked()))
            {
                OpenItem(item);
            }
        }
    }

    return false;
}

bool OpenItemAction::isUseful()
{
    return !bot->IsInCombat() &&
           AI_VALUE2(uint32, "item count", "usage " + std::to_string(ITEM_USAGE_OPEN)) > 0;
}
