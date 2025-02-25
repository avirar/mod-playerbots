#include "UnlockItemAction.h"
#include "PlayerbotAI.h"
#include "ItemTemplate.h"
#include "ItemUsageValue.h"
#include "ObjectMgr.h"

#include "ChatHelper.h"
//#include "Event.h"
#include "Playerbots.h"
//#include "ServerFacade.h"
/*
bool UnlockItemAction::Unlock(Item* item, uint8 bag, uint8 slot)
{
    if (!item)
        return false;

    ItemTemplate const* itemTemplate = item->GetTemplate();
    if (!itemTemplate)
        return false;

    botAI->TellMaster("🔓 Attempting to unlock: " + itemTemplate->Name1);

    // 🔹 Log the LockID for debugging
    botAI->TellMaster("🔎 LockID: " + std::to_string(itemTemplate->LockID));

    if (itemTemplate->LockID == 0)
    {
        botAI->TellMaster("✅ Item has no lock. Can be opened normally.");
        return true;
    }

    LockEntry const* lockInfo = sLockStore.LookupEntry(itemTemplate->LockID);
    if (!lockInfo)
    {
        botAI->TellMaster("❌ LockEntry not found for LockID: " + std::to_string(itemTemplate->LockID));
        return false;
    }

    // 🔹 Log detailed lockInfo
    std::ostringstream lockInfoDebug;
    lockInfoDebug << "🔐 Lock Types:";
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
                isCompletelyUnlocked = false;
                requiredSkill = SkillByLockType(LockType(lockInfo->Index[i]));
                requiredSkillValue = std::max((uint32)1, lockInfo->Skill[i]);
                uint32 botSkillLevel = bot->GetSkillValue(requiredSkill);

                // 🔹 Log skill check
                std::ostringstream debugMsg;
                debugMsg << "🔍 Checking skill: " << requiredSkill 
                         << " (Should be 633 for Lockpicking) | Bot skill level: " << botSkillLevel
                         << " | Required: " << requiredSkillValue
                         << " | LockType: " << lockInfo->Index[i];
                botAI->TellMaster(debugMsg.str());

                // **🔹 Ensure we use the correct spell ID (Pick Lock = 1804)**
                uint32 spellId = (LockType(lockInfo->Index[i]) == LOCKTYPE_PICKLOCK) ? 1804 : lockInfo->Index[i];

                if (spellId == 1) // ❌ If it's "Word of Recall", something is wrong
                {
                    botAI->TellMaster("❌ ERROR: Retrieved incorrect spell ID for Pick Lock! Spell ID: 1. Expected: 1804");
                    return false;
                }

                if (requiredSkill == SKILL_LOCKPICKING && botSkillLevel >= requiredSkillValue)
                {
                    botAI->TellMaster("🛠️ Using Lockpicking skill on: " + itemTemplate->Name1 + " with Spell ID: " + std::to_string(spellId));

                    // 🔹 Format the spell command (following disenchant logic)
                    std::ostringstream spellCommand;
                    spellCommand << spellId << " " << chat->FormatQItem(item->GetEntry());

                    botAI->TellMaster("🪄 Casting Pick Lock using: " + spellCommand.str());

                    // 🔹 Use CastCustomSpellAction like Disenchanting does
                    if (CastCustomSpellAction(botAI, "pick lock").Execute(Event("unlock item", spellCommand.str())))
                    {
                        botAI->SetNextCheckDelay(sPlayerbotAIConfig->lootDelay);

                        for (int j = 0; j < 3; ++j) // Retry checking unlock status
                        {
                            botAI->SetNextCheckDelay(500);
                            if (item->HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_UNLOCKED))
                            {
                                botAI->TellMaster("✅ Successfully unlocked: " + itemTemplate->Name1);
                                return true;
                            }
                        }
                        botAI->TellMaster("❌ Unlock attempt failed, item is still locked: " + itemTemplate->Name1);
                    }
                    else
                    {
                        botAI->TellMaster("❌ CastCustomSpellAction failed for Pick Lock!");
                    }
                }
                else
                {
                    botAI->TellMaster("❌ Bot lacks the required Lockpicking skill level.");
                }

                return false;
            }

            case LOCK_KEY_ITEM:
                isCompletelyUnlocked = false;
                if (lockInfo->Index[i] > 0)
                {
                    requiredKeyItem = lockInfo->Index[i];
                    botAI->TellMaster("🔑 Key required: " + std::to_string(requiredKeyItem));
                }
                break;

            case LOCK_KEY_NONE:
                break;
        }
    }

    if (isCompletelyUnlocked)
    {
        botAI->TellMaster("✅ Item is not locked.");
        return true;
    }

    /*
    if (requiredKeyItem > 0 && bot->HasItemCount(requiredKeyItem, 1))
    {
        Item* keyItem = bot->GetItemByEntry(requiredKeyItem);
        if (keyItem)
        {
            botAI->TellMaster("🔑 Using key to unlock: " + itemTemplate->Name1);
            if (UseItem(keyItem, ObjectGuid::Empty, item))
            {
                botAI->SetNextCheckDelay(sPlayerbotAIConfig->lootDelay);
                botAI->TellMaster("✅ Successfully unlocked with key: " + itemTemplate->Name1);
                return true;
            }
        }
    }
    //

    botAI->TellMaster("❌ Failed to unlock item: " + itemTemplate->Name1);
    return false;
}
*/

bool UnlockItemAction::Execute(Event event)
{
    std::vector<Item*> items =
        AI_VALUE2(std::vector<Item*>, "inventory items", "usage " + std::to_string(ITEM_USAGE_UNLOCK));

    if (items.empty())
    {
        botAI->TellMaster("I have no locked items to unlock.");
        return false;
    }

    botAI->TellMaster("I found " + std::to_string(items.size()) + " locked items.");

    for (auto& item : items)
    {
        if (!item)
            continue;

        botAI->TellMaster("Attempting to unlock " + chat->FormatItem(item->GetTemplate()));

        if (CastCustomSpellAction::Execute(
                Event("unlock item", "1804 " + std::to_string(item->GetGUID().GetRawValue()))))
        // if (botAI->CastSpell(1804, nullptr, item))
        {
            botAI->TellMaster("Successfully unlocked " + chat->FormatItem(item->GetTemplate()));
            return true;
        }
        else
        {
            botAI->TellMaster("I failed to unlock " + chat->FormatItem(item->GetTemplate()));
        }
    }

    botAI->TellMaster("I couldn't unlock any items.");
    return false;
}

bool UnlockItemAction::isUseful()
{
    if (!botAI->HasSkill(SKILL_LOCKPICKING))
    {
        botAI->TellMaster("I don't know how to pick locks.");
        return false;
    }

    if (bot->IsInCombat())
    {
        botAI->TellMaster("I'm too busy fighting to unlock anything!");
        return false;
    }

    uint32 lockedItemCount = AI_VALUE2(uint32, "item count", "usage " + std::to_string(ITEM_USAGE_UNLOCK));

    if (lockedItemCount == 0)
    {
        botAI->TellMaster("I have no locked items to unlock.");
        return false;
    }

    botAI->TellMaster("I have " + std::to_string(lockedItemCount) + " locked item(s) I can unlock.");
    return true;
}
