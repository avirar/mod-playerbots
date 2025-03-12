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
    botAI->TellMaster("Attempting to find a locked item...");

    bool foundLockedItem = false;
    Item* item = botAI->FindLockedItem();

    if (item)
    {
        uint8 bag = item->GetBagSlot();  // Retrieves the bag slot (0 for main inventory)
        uint8 slot = item->GetSlot();    // Retrieves the actual slot inside the bag

        std::ostringstream out;
        out << "Found locked item: " << item->GetTemplate()->Name1
            << " (Bag: " << static_cast<uint32>(bag) << ", Slot: " << static_cast<uint32>(slot) << ")";
        botAI->TellMaster(out.str());

        UnlockItem(item, bag, slot);
        foundLockedItem = true;
    }
    else
    {
        botAI->TellMaster("No locked item found.");
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
