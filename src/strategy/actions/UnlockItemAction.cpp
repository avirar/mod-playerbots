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

    botAI->TellMaster("Attempting to unlock: " + item->GetTemplate()->Name1);

    SkillType requiredSkill = SKILL_NONE;
    uint32 requiredSkillValue = 0;
    uint32 requiredKeyItem = 0;

    // Scan for lock requirements
    for (uint8 i = 0; i < 8; ++i)
    {
        switch (lockInfo->Type[i])
        {
            case LOCK_KEY_SKILL:
            {
                // Determine skill requirement
                requiredSkill = SkillByLockType(LockType(lockInfo->Index[i]));
                requiredSkillValue = std::max((uint32)1, lockInfo->Skill[i]);
                uint32 botSkillLevel = bot->GetSkillValue(requiredSkill);

                std::ostringstream debugMsg;
                debugMsg << "Checking skill: " << requiredSkill 
                         << " (Should be 633 for Lockpicking) | Bot skill level: " << botSkillLevel
                         << " | Required: " << requiredSkillValue
                         << " | Spell ID: " << lockInfo->Index[i];
                botAI->TellMaster(debugMsg.str());

                // Ensure the detected skill is valid
                if (requiredSkill == SKILL_LOCKPICKING && botSkillLevel >= requiredSkillValue)
                {
                    botAI->TellMaster("Using Lockpicking skill on: " + item->GetTemplate()->Name1);

                    // 🔹 FIX: Use correct CastSpell function for items
                    bot->CastSpell(bot, lockInfo->Index[i], TRIGGERED_NONE, item);

                    // Wait for the unlock to happen
                    botAI->SetNextCheckDelay(sPlayerbotAIConfig->lootDelay);

                    // Check if the item is now openable
                    if (botAI->CanOpenItem(item)) 
                    {
                        botAI->TellMaster("Successfully unlocked: " + item->GetTemplate()->Name1);
                        return true;
                    }
                    else
                    {
                        botAI->TellMaster("Unlock attempt failed, item is still locked: " + item->GetTemplate()->Name1);
                    }
                }
                break;
            }

            case LOCK_KEY_ITEM:
                if (lockInfo->Index[i] > 0)
                    requiredKeyItem = lockInfo->Index[i];
                break;

            case LOCK_KEY_NONE:
                return true; // No unlocking required.
        }
    }

    // If skill unlocking failed, attempt to use a key item
    if (requiredKeyItem > 0 && bot->HasItemCount(requiredKeyItem, 1))
    {
        Item* keyItem = bot->GetItemByEntry(requiredKeyItem);
        if (keyItem)
        {
            botAI->TellMaster("Using key to unlock: " + item->GetTemplate()->Name1);
            if (UseItem(keyItem, ObjectGuid::Empty, item))
            {
                botAI->SetNextCheckDelay(sPlayerbotAIConfig->lootDelay);
                botAI->TellMaster("Successfully unlocked with key: " + item->GetTemplate()->Name1);
                return true;
            }
        }
    }

    botAI->TellMaster("Failed to unlock item: " + item->GetTemplate()->Name1);
    return false;
}
