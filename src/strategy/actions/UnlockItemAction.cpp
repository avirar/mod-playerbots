#include "UnlockItemAction.h"
#include "OpenItemAction.h"
#include "ChatHelper.h"
// #include "ItemTemplate.h"
#include "ItemUsageValue.h"
//#include "PlayerbotAI.h"
#include "Playerbots.h"

bool UnlockItemAction::Execute(Event event)
{
    Player* bot = ai->GetBot();

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_0; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = bot->GetBagByPos(bagSlot);
        if (!bag)
            continue;

        for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
        {
            Item* item = bag->GetItemByPos(slot);
            if (!item)
                continue;

            ItemTemplate const* proto = item->GetTemplate();
            if (!proto)
                continue;

            // Check if the item is unlockable
            if (proto->LockID > 0 && item->IsLocked())
            {
                LockEntry const* lockInfo = sLockStore.LookupEntry(proto->LockID);
                if (!lockInfo)
                    continue;

                for (uint8 i = 0; i < 8; ++i)
                {
                    if (lockInfo->Type[i] == LOCK_KEY_SKILL || lockInfo->Type[i] == LOCK_KEY_ITEM)
                    {
                        // Execute unlock spell
                        if (CastCustomSpellAction::Execute(Event("unlock items", "1804 " + chat->FormatQItem(item->GetEntry()))))
                        {
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}


bool UnlockItemAction::isUseful()
{
    return botAI->HasSkill(SKILL_LOCKPICKING) && !bot->IsInCombat() &&
           AI_VALUE2(uint32, "item count", "usage " + std::to_string(ITEM_USAGE_UNLOCK)) > 0;
}
