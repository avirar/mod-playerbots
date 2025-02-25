#include "UnlockItemAction.h"
#include "PlayerbotAI.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "CastCustomSpellAction.h"

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
    bool isCompletelyUnlocked = true;

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
                         << " | LockType: " << lockInfo->Index[i];
                botAI->TellMaster(debugMsg.str());

                // **🔹 Fix: Ensure we use the correct spell for Pick Lock**
                uint32 spellId = 0;
                if (LockType(lockInfo->Index[i]) == LOCKTYPE_PICKLOCK)
                {
                    spellId = 1804; // Force correct Pick Lock spell ID
                }
                else
                {
                    spellId = lockInfo->Index[i]; // Use the stored spell ID if it's valid
                }

                if (spellId == 1) // ❌ If it's still "Word of Recall", something is wrong
                {
                    botAI->TellMaster("❌ ERROR: Retrieved incorrect spell ID for Pick Lock! Spell ID: 1. Expected: 1804");
                    return false;
                }

                if (requiredSkill == SKILL_LOCKPICKING && botSkillLevel >= requiredSkillValue)
                {
                    botAI->TellMaster("Using Lockpicking skill on: " + itemTemplate->Name1 + " with Spell ID: " + std::to_string(spellId));

                    // 🔹 Properly format the spell command to target the item
                    std::ostringstream spellCommand;
                    spellCommand << spellId << " " << chat->FormatQItem(item->GetEntry());

                    botAI->TellMaster("Casting Pick Lock using: " + spellCommand.str());

                    // **🔹 Create an instance of `CastCustomSpellAction`**
                    CastCustomSpellAction castAction(botAI, "pick lock");
                    if (castAction.Execute(Event("unlock item", spellCommand.str())))
                    {
                        botAI->SetNextCheckDelay(sPlayerbotAIConfig->lootDelay);

                        for (int j = 0; j < 3; ++j) // Retry 3 times to allow for server delay
                        {
                            botAI->SetNextCheckDelay(500);
                            if (item->HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_UNLOCKED))
                            {
                                botAI->TellMaster("Successfully unlocked: " + itemTemplate->Name1);
                                return true;
                            }
                        }
                        botAI->TellMaster("Unlock attempt failed, item is still locked: " + itemTemplate->Name1);
                    }
                }
                else
                {
                    botAI->TellMaster("Bot lacks the required Lockpicking skill level.");
                }

                return false;
            }

            case LOCK_KEY_ITEM:
                isCompletelyUnlocked = false;
                if (lockInfo->Index[i] > 0)
                {
                    requiredKeyItem = lockInfo->Index[i];
                    botAI->TellMaster("Key required: " + std::to_string(requiredKeyItem));
                }
                break;

            case LOCK_KEY_NONE:
                break;
        }
    }

    if (isCompletelyUnlocked)
    {
        botAI->TellMaster("Item is not locked.");
        return true;
    }

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
