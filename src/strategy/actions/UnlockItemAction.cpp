#include "UnlockItemAction.h"
#include "PlayerbotAI.h"
#include "ItemTemplate.h"
#include "WorldPacket.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "SpellInfo.h"

#define PICK_LOCK_SPELL_ID 1804

bool UnlockItemAction::Execute(Event event)
{
    bool foundLockedItem = false;

    Item* item = botAI->FindLockedItem();
    if (!item)
    {
        botAI->TellMaster("No locked items found in inventory.");
        return false;
    }

    uint8 bag = item->GetBagSlot();  // Retrieves the bag slot (0 for main inventory)
    uint8 slot = item->GetSlot();    // Retrieves the actual slot inside the bag

    std::ostringstream out;
    out << "Attempting to unlock: " << item->GetTemplate()->Name1 << " (Bag: " << (int)bag << ", Slot: " << (int)slot << ")";
    botAI->TellMaster(out.str());

    if (UnlockItem(item, bag, slot))
    {
        botAI->TellMaster("Successfully unlocked the item.");
        foundLockedItem = true;
    }
    else
    {
        botAI->TellMaster("Failed to unlock the item.");
    }

    return foundLockedItem;
}

void UnlockItemAction::UnlockItem(Item* item, uint8 bag, uint8 slot)
{
    // Use CastSpell to unlock the item
    if (botAI->CastSpell(PICK_LOCK_SPELL_ID, bot, item))
    {
        std::ostringstream out;
        out << "Used Pick Lock on: " << item->GetTemplate()->Name1;
        botAI->TellMaster(out.str());
    }
    else
    {
        botAI->TellError("Failed to cast Pick Lock.");
    }
}
