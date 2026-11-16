/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ITEMKEEPPOLICY_H
#define PLAYERBOTS_ITEMKEEPPOLICY_H

#include <string>

#include "ItemUsageValue.h"

class PlayerbotAI;

struct ItemTemplate;

enum class ItemCleanupCategory : uint8
{
    None = 0,
    Useless,
    Trade,
    Disenchant,
    Consumable,
    Quest,
    Gear
};

struct ItemKeepRule
{
    uint32 keepCount = 0;  // total count kept across bags, or UINT32_MAX when always kept
    bool alwaysKeep = false;
    bool completedQuestSource = false;
    ItemCleanupCategory category = ItemCleanupCategory::None;
};

// Keep policy shared by selling, random-bot bag cleanup and future auction listing.
// The item usage is passed in because callers already cache it per item id.
ItemKeepRule GetItemKeepRule(PlayerbotAI* botAI, ItemTemplate const* proto, ItemUsage usage);

bool TryParseItemCleanupCategory(std::string const& token, ItemCleanupCategory& category);

#endif
