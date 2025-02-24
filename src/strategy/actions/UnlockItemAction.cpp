#include "UnlockItemAction.h"
#include "PlayerbotAI.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"

bool UnlockItemAction::Execute(Item* item, uint8 bag, uint8 slot)
{
    if (!item)
        return false;

    ItemTemplate const* itemTemplate = item->GetTemplate();
    if (!itemTemplate || itemTemplate->LockID == 0)
        return true; // No lock means it is already openable.

    LockEntry const* lockInfo = sLockStore.LookupEntry(itemTemplate->LockID);
    if (!lockInfo)
        return false;

    SkillType requiredSkill = SKILL_NONE;
    uint32 requiredSkillValue = 0;
    uint32 requiredKeyItem = 0;

    // Scan for lock requirements
    for (uint8 i = 0; i < 8; ++i)
    {
        switch (lockInfo->Type[i])
        {
            case LOCK_KEY_SKILL:
                // Prioritize skill-based unlocking
                requiredSkill = SkillByLockType(LockType(lockInfo->Index[i]));
                requiredSkillValue = std::max((uint32)1, lockInfo->Skill[i]);

                if (requiredSkill > 0 && bot->HasSkill(requiredSkill) && bot->GetSkillValue(requiredSkill) >= requiredSkillValue)
                {
                    bot->CastSpell(bot, lockInfo->Index[i], TRIGGERED_NONE);
                    botAI->TellMaster("Using skill to unlock item: " + item->GetTemplate()->Name1);
                    return true;
                }
                break;

            case LOCK_KEY_ITEM:
                // Store the required key item for later use
                if (lockInfo->Index[i] > 0)
                    requiredKeyItem = lockInfo->Index[i];
                break;

            case LOCK_KEY_NONE:
                return true; // No unlocking required.
        }
    }

    // If skill unlocking was not possible, attempt to use a key item
    if (requiredKeyItem > 0 && bot->HasItemCount(requiredKeyItem, 1))
    {
        Item* keyItem = bot->GetItemByEntry(requiredKeyItem);
        if (keyItem)
        {
            // Use the key item correctly
            if (UseItem(keyItem, ObjectGuid::Empty, item))
            {
                botAI->TellMaster("Used key to unlock item: " + item->GetTemplate()->Name1);
                return true;
            }
        }
    }

    botAI->TellMaster("Failed to unlock item: " + item->GetTemplate()->Name1);
    return false;
}
