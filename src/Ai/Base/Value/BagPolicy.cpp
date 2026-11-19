/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BagPolicy.h"

#include "Bag.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Player.h"
#include "SharedDefines.h"

namespace BagPolicy
{
uint8 FindBagSlotForBag(Player* bot, ItemTemplate const* proto)
{
    if (!bot || !proto || proto->Class != ITEM_CLASS_CONTAINER || !proto->ContainerSlots)
        return 0;

    if (bot->CanUseItem(proto) != EQUIP_ERR_OK)
        return 0;

    uint32 const newSize = proto->ContainerSlots;
    uint32 const newFamily = proto->BagFamily;

    uint8 emptySlot = 0;
    uint8 sameFamilySlot = 0;
    uint32 sameFamilySize = 0;
    uint8 smallestGenericSlot = 0;
    uint32 smallestGenericSize = 0;
    bool hasFamily = false;

    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
        {
            if (!emptySlot)
                emptySlot = slot;
            continue;
        }

        ItemTemplate const* bagProto = item->GetTemplate();
        if (!bagProto)
            continue;

        uint32 const bagSize = bagProto->ContainerSlots;
        uint32 const bagFamily = bagProto->BagFamily;

        if (bagFamily)
        {
            hasFamily = true;
            if (bagFamily == newFamily && (!sameFamilySlot || bagSize < sameFamilySize))
            {
                sameFamilySlot = slot;
                sameFamilySize = bagSize;
            }
        }
        else if (!smallestGenericSlot || bagSize < smallestGenericSize)
        {
            smallestGenericSlot = slot;
            smallestGenericSize = bagSize;
        }
    }

    if (newFamily)
    {
        // Family bags are hard to re-acquire: only upgrade the same family.
        // A second family bag is never equipped, even into an empty slot.
        if (hasFamily)
            return (sameFamilySlot && newSize > sameFamilySize) ? sameFamilySlot : 0;

        return emptySlot;
    }

    // Generic bags fill empty slots first, then upgrade the smallest generic
    // bag. They never displace a family bag.
    if (emptySlot)
        return emptySlot;

    return (smallestGenericSlot && newSize > smallestGenericSize) ? smallestGenericSlot : 0;
}

bool IsBagUpgradeForBot(Player* bot, ItemTemplate const* proto) { return FindBagSlotForBag(bot, proto) != 0; }
}  // namespace BagPolicy
