#include "OpenItemAction.h"
#include "PlayerbotAI.h"
#include "ItemTemplate.h"
#include "WorldPacket.h"
#include "Player.h"
#include "ObjectMgr.h"

bool OpenItemAction::Execute(Event event)
{
    bool foundOpenable = false;

    // Check main inventory slots
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

        if (item && CanOpenItem(item))
        {
            OpenItem(item, INVENTORY_SLOT_BAG_0, slot);
            foundOpenable = true;
        }
    }

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
                OpenItem(item, bag, slot);
                foundOpenable = true;
            }
        }
    }

    // If no openable items found
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
    if (!(itemTemplate->Flags & ITEM_FLAG_HAS_LOOT))
        return false;

    // If the item has no lock, it can be opened freely
    if (itemTemplate->LockID == 0)
        return true;

    // Get the lock entry for the item
    LockEntry const* lockInfo = sLockStore.LookupEntry(itemTemplate->LockID);
    if (!lockInfo)
        return false;

    // Declare variables outside of the switch block
    SkillType requiredSkill = SKILL_NONE;
    uint32 requiredSkillValue = 0;

    // Check the lock requirements
    for (uint8 i = 0; i < 8; ++i)
    {
        switch (lockInfo->Type[i])
        {
            case LOCK_KEY_ITEM:
                // Check if the bot has the required key item
                if (lockInfo->Index[i] > 0 && bot->HasItemCount(lockInfo->Index[i], 1))
                    return true;
                break;

            case LOCK_KEY_SKILL:
                // Assign values outside the switch block
                requiredSkill = SkillByLockType(LockType(lockInfo->Index[i]));
                requiredSkillValue = std::max((uint32)1, lockInfo->Skill[i]);

                if (requiredSkill > 0 && bot->HasSkill(requiredSkill) && bot->GetSkillValue(requiredSkill) >= requiredSkillValue)
                    return true;
                break;

            case LOCK_KEY_NONE:
                // The item is not locked, it can be opened
                return true;
        }
    }

    // If none of the conditions were met, the bot cannot open the item
    return false;
}

void OpenItemAction::OpenItem(Item* item, uint8 bag, uint8 slot)
{
    WorldPacket packet(CMSG_OPEN_ITEM);
    packet << bag << slot;
    bot->GetSession()->HandleOpenItemOpcode(packet);

    std::ostringstream out;
    out << "Opened item: " << item->GetTemplate()->Name1;
    botAI->TellMaster(out.str());
}
