/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "OpenItemAction.h"
#include "AiObjectContext.h"
#include "Bag.h"
#include "DBCStores.h"
#include "ItemTemplate.h"
#include "LootObjectStack.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "SkeletonKey.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "UseItemAction.h"
#include "WorldPacket.h"

namespace
{
// A locked, lootable item the bot can open with a skeleton key; skips locks the
// bot can already pick so a key is not wasted.
Item* FindKeyForLockedItem(Player* bot, Item* item)
{
    ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
    if (!proto || !proto->HasFlag(ITEM_FLAG_HAS_LOOT) || !proto->LockID || !item->IsLocked())
        return nullptr;

    LockEntry const* lockInfo = sLockStore.LookupEntry(proto->LockID);
    if (!lockInfo)
        return nullptr;

    for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
    {
        if (lockInfo->Type[i] != LOCK_KEY_SKILL || lockInfo->Index[i] != LOCKTYPE_PICKLOCK)
            continue;

        SkillType const skillId = SkillByLockType(LockType(lockInfo->Index[i]));
        uint32 const requiredSkill = std::max(1u, lockInfo->Skill[i]);
        if (skillId == SKILL_LOCKPICKING && bot->HasSkill(skillId) &&
            bot->GetSkillValue(skillId) >= requiredSkill)
            return nullptr;

        if (Item* key = SkeletonKey::FindForLock(bot, requiredSkill, lockInfo->Index[i]))
            return key;
    }

    return nullptr;
}

Item* FindLockedItemWithKey(Player* bot, Item*& keyOut)
{
    auto check = [bot, &keyOut](Item* item) -> Item*
    {
        if (Item* key = FindKeyForLockedItem(bot, item))
        {
            keyOut = key;
            return item;
        }
        return nullptr;
    };

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (Item* found = check(item))
                return found;

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bot->GetItemByPos(bagSlot, slot))
                    if (Item* found = check(item))
                        return found;

    return nullptr;
}
}  // namespace

bool OpenItemAction::Execute(Event /*event*/)
{
    Item* item = botAI->FindOpenableItem();
    if (item)
    {
        OpenItem(item, item->GetBagSlot(), item->GetSlot());
        return true;
    }

    Item* key = nullptr;
    Item* locked = FindLockedItemWithKey(bot, key);
    if (!locked)
        return false;

    UseItemAction useItem(botAI);
    if (!useItem.UseItemOnItem(key, locked))
        return false;

    SpellInfo const* keySpell = nullptr;
    if (ItemTemplate const* keyProto = key->GetTemplate())
        if (keyProto->Spells[0].SpellId)
            keySpell = sSpellMgr->GetSpellInfo(keyProto->Spells[0].SpellId);

    uint32 delay = keySpell ? keySpell->CalcCastTime(bot) : 0;
    delay = std::max<uint32>(delay + 1000, 1000);

    ObjectGuid const botGuid = bot->GetGUID();
    ObjectGuid const itemGuid = locked->GetGUID();
    botAI->AddTimedEvent(
        [botGuid, itemGuid]
        {
            Player* player = ObjectAccessor::FindPlayer(botGuid);
            if (!player)
                return;

            Item* opened = player->GetItemByGuid(itemGuid);
            if (!opened || opened->IsLocked())
                return;

            if (PlayerbotAI* ai = GET_PLAYERBOT_AI(player))
            {
                LootObject lootObject;
                lootObject.guid = itemGuid;
                ai->GetAiObjectContext()->GetValue<LootObject>("loot target")->Set(lootObject);
            }

            WorldPacket packet(CMSG_OPEN_ITEM);
            packet << opened->GetBagSlot() << opened->GetSlot();
            player->GetSession()->HandleOpenItemOpcode(packet);
        },
        delay);

    LOG_DEBUG("playerbots", "[New RPG] {} using {} on {}", bot->GetName(), key->GetEntry(),
              locked->GetEntry());

    std::ostringstream out;
    out << "Using " << key->GetTemplate()->Name1 << " on " << locked->GetTemplate()->Name1;
    botAI->TellMaster(out.str());
    return true;
}

void OpenItemAction::OpenItem(Item* item, uint8 bag, uint8 slot)
{
    ObjectGuid const itemGuid = item->GetGUID();
    std::ostringstream out;
    out << "Opened item: " << item->GetTemplate()->Name1;

    WorldPacket packet(CMSG_OPEN_ITEM);
    packet << bag << slot;
    bot->GetSession()->HandleOpenItemOpcode(packet);

    // Store the item GUID as the loot target
    LootObject lootObject;
    lootObject.guid = itemGuid;
    botAI->GetAiObjectContext()->GetValue<LootObject>("loot target")->Set(lootObject);

    botAI->TellMaster(out.str());
}
