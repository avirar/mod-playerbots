/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BAGPOLICY_H
#define PLAYERBOTS_BAGPOLICY_H

#include "Define.h"

struct ItemTemplate;
class Player;

namespace BagPolicy
{
    // Bag slot under the equip rules, or 0 when the container would not
    // replace/upgrade anything. Empty slots first, generic bags only upgrade
    // smaller generic bags, family bags only upgrade the same family and are
    // never displaced.
    uint8 FindBagSlotForBag(Player* bot, ItemTemplate const* proto);
    bool IsBagUpgradeForBot(Player* bot, ItemTemplate const* proto);
}  // namespace BagPolicy

#endif
