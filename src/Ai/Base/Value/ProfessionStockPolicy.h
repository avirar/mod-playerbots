/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PROFESSIONSTOCKPOLICY_H
#define PLAYERBOTS_PROFESSIONSTOCKPOLICY_H

#include <cstdint>
#include <vector>

#include "Common.h"

class PlayerbotAI;

struct ItemTemplate;

// Vendor consumables that a profession uses in bulk (vials, thread, flux,
// parchment, spices). The policy keeps the current tier at two stacks.
uint32 GetProfessionVendorStockTarget(PlayerbotAI* botAI, ItemTemplate const* proto);
std::vector<uint32> GetProfessionVendorStockItems(PlayerbotAI* botAI);

// Crafted consumables that a profession uses in bulk but vendors do not sell
// (enchanting vellums). The policy keeps the current tier at two stacks so a
// scroll recipe always has a target item in the bags.
uint32 GetProfessionCraftedStockTarget(PlayerbotAI* botAI, ItemTemplate const* proto);
std::vector<uint32> GetProfessionCraftedStockItems(PlayerbotAI* botAI);

// True when the item is a reagent of any known profession spell, including an
// obsolete tier. Used to sell old vials/thread without touching unrelated
// vendor consumables.
bool IsKnownProfessionVendorReagent(PlayerbotAI* botAI, ItemTemplate const* proto);

// True for the item shapes that can be profession vendor stock, even when the
// item is an obsolete tier for this bot.
bool IsProfessionVendorStockCandidate(ItemTemplate const* proto);

#endif
