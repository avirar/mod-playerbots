
#include "OpenItemAction.h"
//#include "PlayerbotAI.h"
// #include "ItemTemplate.h"
#include "WorldPacket.h"
// #include "Player.h"
// #include "ObjectMgr.h"

#include "ChatHelper.h"
#include "ItemUsageValue.h"
#include "Playerbots.h"

#include "ItemVisitors.h"
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
void OpenItemAction::OpenItem(Item* item, uint8 bag, uint8 slot)
{
    WorldPacket packet(CMSG_OPEN_ITEM);
    packet << bag << slot;
    bot->GetSession()->HandleOpenItemOpcode(packet);

    std::ostringstream out;
    out << "Opened item: " << item->GetTemplate()->Name1;
    botAI->TellMaster(out.str());
}
bool OpenItemAction::Execute(Event event)
{
    FindItemUsageVisitor visitor(bot, ITEM_USAGE_OPEN);

    bot->VisitItem(ITERATE_ITEMS_IN_BAGS, visitor);

    std::vector<Item*> items = visitor.GetResult();

    for (auto& item : items)
    {
        uint8 bag = item->GetBagSlot();
        uint8 slot = item->GetSlot();
        OpenItem(item, bag, slot);
    }

    return false;
}


bool OpenItemAction::isUseful()
{
    if (bot->IsInCombat())
        return false;

    FindItemUsageVisitor visitor(bot, ITEM_USAGE_OPEN);
    bot->VisitItem(ITERATE_ITEMS_IN_BAGS, visitor);

    return !visitor.GetResult().empty(); // Returns true if there are openable items
}
