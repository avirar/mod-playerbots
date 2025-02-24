#include "UnlockItemAction.h"
#include "PlayerbotAI.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"

bool UnlockItemAction::Unlock(Item* item, uint8 bag, uint8 slot)
{
    if (!item)
        return false;

    ItemTemplate const* itemTemplate = item->GetTemplate();
    if (!itemTemplate)
        return false;

    botAI->TellMaster("Attempting to unlock: " + itemTemplate->Name1);

    // 🔹 Log the LockID for better debugging
    botAI->TellMaster("LockID: " + std::to_string(itemTemplate->LockID));

    if (itemTemplate->LockID == 0)
    {
        botAI->TellMaster("Item has no lock. Can be opened normally.");
        return true;
    }

    LockEntry const* lockInfo = sLockStore.LookupEntry(itemTemplate->LockID);
    if (!lockInfo)
    {
        botAI->TellMaster("LockEntry not found for LockID: " + std::to_string(itemTemplate->LockID));
        return false;
    }

    // 🔹 Log detailed lockInfo
    std::ostringstream lockInfoDebug;
    lockInfoDebug << "Lock Types:";
    for (uint8 i = 0; i < 8; ++i)
    {
        lockInfoDebug << " [Type " << std::to_string(i) << ": " << std::to_string(lockInfo->Type[i])
                      << " | Index: " << std::to_string(lockInfo->Index[i]) << "]";
    }
    botAI->TellMaster(lockInfoDebug.str());

    SkillType requiredSkill = SKILL_NONE;
    uint32 requiredSkillValue = 0;
    uint32 requiredKeyItem = 0;
    bool isCompletelyUnlocked = true; // Assume it's unlocked unless we find a real lock

    // Scan for lock requirements
    for (uint8 i = 0; i < 8; ++i)
    {
        switch (lockInfo->Type[i])
        {
            case LOCK_KEY_SKILL:
            {
                isCompletelyUnlocked = false; // Item requires a skill, so it's locked
                requiredSkill = SkillByLockType(LockType(lockInfo->Index[i]));
                requiredSkillValue = std::max((uint32)1, lockInfo->Skill[i]);
                uint32 botSkillLevel = bot->GetSkillValue(requiredSkill);

                // 🔹 Always print Checking Skill message
                std::ostringstream debugMsg;
                debugMsg << "Checking skill: " << requiredSkill 
                         << " (Should be 633 for Lockpicking) | Bot skill level: " << botSkillLevel
                         << " | Required: " << requiredSkillValue
                         << " | Spell ID: " << lockInfo->Index[i];
                botAI->TellMaster(debugMsg.str());

                // **🔹 Fix: Verify spell ID is actually Pick Lock (1804)**
                if (lockInfo->Index[i] != 1804)
                {
                    botAI->TellMaster("❌ ERROR: Retrieved incorrect spell ID for Pick Lock! Spell ID: " +
                                      std::to_string(lockInfo->Index[i]) + ". Expected: 1804");
                    return false;
                }

                if (requiredSkill == SKILL_LOCKPICKING && botSkillLevel >= requiredSkillValue)
                {
                    botAI->TellMaster("Using Lockpicking skill on: " + itemTemplate->Name1);

                    // Cast Pick Lock on the item
                    bot->CastSpell(bot, 1804, TRIGGERED_NONE, item);  // 🔹 Force the correct spell ID (1804)

                    // Wait for the unlock to happen
                    botAI->SetNextCheckDelay(sPlayerbotAIConfig->lootDelay);

                    // 🔹 FIX: Check if item has been unlocked
                    if (item->HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_UNLOCKED))
                    {
                        botAI->TellMaster("Successfully unlocked: " + itemTemplate->Name1);
                        return true;
                    }
                    else
                    {
                        botAI->TellMaster("Unlock attempt failed, item is still locked: " + itemTemplate->Name1);
                    }
                }
                else
                {
                    botAI->TellMaster("Bot lacks the required Lockpicking skill level.");
                }
                break;
            }

            case LOCK_KEY_ITEM:
                isCompletelyUnlocked = false; // Item requires a key, so it's locked
                if (lockInfo->Index[i] > 0)
                {
                    requiredKeyItem = lockInfo->Index[i];
                    botAI->TellMaster("Key required: " + std::to_string(requiredKeyItem));
                }
                break;

            case LOCK_KEY_NONE:
                // Do nothing immediately, we will check this after scanning all locks
                break;
        }
    }

    // 🔹 Only say "Item is not locked" if NO lock conditions were found
    if (isCompletelyUnlocked)
    {
        botAI->TellMaster("Item is not locked.");
        return true;
    }

    // If skill unlocking failed, attempt to use a key item
    if (requiredKeyItem > 0 && bot->HasItemCount(requiredKeyItem, 1))
    {
        Item* keyItem = bot->GetItemByEntry(requiredKeyItem);
        if (keyItem)
        {
            botAI->TellMaster("Using key to unlock: " + itemTemplate->Name1);
            if (UseItem(keyItem, ObjectGuid::Empty, item))
            {
                botAI->SetNextCheckDelay(sPlayerbotAIConfig->lootDelay);
                botAI->TellMaster("Successfully unlocked with key: " + itemTemplate->Name1);
                return true;
            }
        }
    }

    botAI->TellMaster("Failed to unlock item: " + itemTemplate->Name1);
    return false;
}
