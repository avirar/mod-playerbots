/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SkeletonKey.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"

namespace
{
struct KeyInfo
{
    uint32 itemId;
    uint32 skill;
};

// Blacksmithing skeleton keys in ascending lock skill: Silver, Golden, Truesilver,
// Arcanite, Cobalt, Titanium.
constexpr KeyInfo KEYS[] = {
    {15869, 25},
    {15870, 125},
    {15871, 200},
    {15872, 300},
    {43854, 375},
    {43853, 400},
};
}  // namespace

bool SkeletonKey::IsKeyItem(ItemTemplate const* proto)
{
    return proto && (proto->Class == ITEM_CLASS_KEY || (proto->BagFamily & BAG_FAMILY_MASK_KEYS));
}

uint32 SkeletonKey::GetSkill(uint32 itemId)
{
    for (KeyInfo const& key : KEYS)
        if (key.itemId == itemId)
            return key.skill;

    return 0;
}

Item* SkeletonKey::FindForLock(Player* bot, uint32 requiredSkill, uint32 lockType)
{
    if (!bot || lockType != LOCKTYPE_PICKLOCK)
        return nullptr;

    for (KeyInfo const& key : KEYS)
    {
        if (key.skill < requiredSkill)
            continue;

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(key.itemId);
        if (!proto || bot->CanUseItem(proto) != EQUIP_ERR_OK)
            continue;

        if (Item* item = bot->GetItemByEntry(key.itemId))
            return item;
    }

    return nullptr;
}
