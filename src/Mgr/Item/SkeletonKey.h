/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SKELETONKEY_H
#define PLAYERBOTS_SKELETONKEY_H

#include "Common.h"

class Item;
class ItemTemplate;
class Player;

namespace SkeletonKey
{
// True for keyring items, including the class-0 skeleton keys.
bool IsKeyItem(ItemTemplate const* proto);

// Skill cap of a skeleton key's open-lock spell; 0 for other items.
uint32 GetSkill(uint32 itemId);

// Lowest-cap skeleton key the bot can use for the lock, or nullptr.
Item* FindForLock(Player* bot, uint32 requiredSkill, uint32 lockType);
}  // namespace SkeletonKey

#endif
