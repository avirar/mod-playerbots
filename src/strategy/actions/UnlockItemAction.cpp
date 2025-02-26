#include "UnlockItemAction.h"
#include "PlayerbotAI.h"
#include "ItemTemplate.h"
#include "ItemUsageValue.h"
#include "ObjectMgr.h"
#include "WorldPacket.h"

#include "ChatHelper.h"
//#include "Event.h"
#include "Playerbots.h"
//#include "ServerFacade.h"

bool UnlockItemAction::UnlockItem(Item* item, uint8 bag, uint8 slot)
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
                    // targets.SetItemTarget(item);
                    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(1804);
                    if (!spellInfo)
                    {
                        botAI->TellMaster("❌ ERROR: Spell 1804 (Pick Lock) not found!");
                        return false;
                    }
                    
                    // Create a new Spell instance
                    Spell* spell = new Spell(bot, spellInfo, TRIGGERED_NONE);
                    SpellCastTargets targets;
                    targets.SetItemTarget(item);
                    botAI->TellMaster("🔎 Item GUID = " + std::to_string(item->GetGUID().GetRawValue()));
                    botAI->TellMaster("🔐 LockID = " + std::to_string(item->GetTemplate()->LockID));

                    
                    SpellCastResult result = spell->CheckCast(true);
                    if (result != SPELL_CAST_OK)
                    {
                        botAI->TellMaster("❌ Pick Lock cast failed! Error Code: " + std::to_string(result));
                        delete spell;
                        return false;
                    }
                    
                    botAI->TellMaster("🪄 Casting Pick Lock on item: " + item->GetTemplate()->Name1);
                    spell->prepare(&targets);
                    
                    delete spell;
                    return true;

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
    */

    botAI->TellMaster("❌ Failed to unlock item: " + itemTemplate->Name1);
    return false;
}


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

        botAI->TellMaster("Debug: Item GUID = " + std::to_string(item->GetGUID().GetRawValue()));

        // Get bag and slot information
        uint8 bag = item->GetBagSlot();
        uint8 slot = item->GetSlot();

        botAI->TellMaster("Debug: Bag = " + std::to_string(bag) + ", Slot = " + std::to_string(slot));

        // Call UnlockItem with correct parameters
        if (UnlockItem(item, bag, slot))
        {
            botAI->TellMaster("Successfully sent unlock attempt for " + chat->FormatItem(item->GetTemplate()));
            return true;
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
/*
void UnlockItemAction::UnlockItem(Item* item, uint8 bag, uint8 slot)
{
    if (!item)
    {
        botAI->TellMaster("Tried to unlock an invalid item.");
        return;
    }

    botAI->TellMaster("Casting Pick Lock on " + chat->FormatItem(item->GetTemplate()));

    // Step 1: Create the spell cast packet
    WorldPacket packet(CMSG_CAST_SPELL);

    uint32 spellId = 1804; // Pick Lock
    uint8 castCount = 0;
    uint8 castFlags = 0;
    uint32 targetFlags = TARGET_FLAG_ITEM; // Correctly define item target flag

    packet << castCount;       // Cast count (always 0)
    packet << spellId;         // Spell ID (Pick Lock)
    packet << castFlags;       // Cast flags
    packet << targetFlags;     // Target flag (must be TARGET_FLAG_ITEM)
    packet << item->GetGUID(); // Correctly pass the item GUID

    // Step 2: Attach valid `SpellCastTargets`
    SpellCastTargets targets;
    targets.SetItemTarget(item); // Corrected: pass `Item*`, not `ObjectGuid`
    targets.Write(packet);       // Attach the target data

    // Step 3: Send the packet
    bot->GetSession()->HandleCastSpellOpcode(packet);

    botAI->TellMaster("Pick Lock spell cast with correct target.");
}
*/
/*
void UnlockItemAction::UnlockItem(Item* item, uint8 bag, uint8 slot)
{
    if (!item)
    {
        botAI->TellMaster("Tried to unlock an invalid item.");
        return;
    }

    botAI->TellMaster("Casting Pick Lock on " + chat->FormatItem(item->GetTemplate()));

    uint32 spellId = 1804; // Pick Lock

    // Step 1: Ensure bot has the spell and can cast it
    if (!botAI->CanCastSpell(spellId, bot, true, item))
    {
        botAI->TellMaster("Bot cannot cast Pick Lock (invalid conditions).");
        return;
    }

    // Step 2: Cast the spell directly on the item
    if (botAI->CastSpell(spellId, bot, item))
    {
        botAI->TellMaster("Pick Lock successfully cast on item.");
    }
    else
    {
        botAI->TellMaster("Pick Lock cast failed.");
    }
}
*/
/*
void UnlockItemAction::UnlockItem(Item* item, uint8 bag, uint8 slot)
{
    if (!item)
    {
        botAI->TellMaster("Tried to unlock an invalid item.");
        return;
    }

    std::string spellName = "pick lock"; // Use spell name instead of ID
    uint32 spellId = 1804;

    if (!spellId)
    {
        botAI->TellMaster("Failed to find spell ID for " + spellName);
        return;
    }

    botAI->TellMaster("Casting " + spellName + " on " + chat->FormatItem(item->GetTemplate()));

    // Step 1: Ensure bot can cast the spell
    if (!botAI->CanCastSpell(spellId, bot, true, item))
    {
        botAI->TellMaster("Bot cannot cast " + spellName + " (invalid conditions).");
        return;
    }

    // Step 2: Cast the spell using its name on the item
    if (botAI->CastSpell(spellName, bot, item))
    {
        botAI->TellMaster(spellName + " successfully cast on item.");
    }
    else
    {
        botAI->TellMaster(spellName + " cast failed.");
    }
}
*/
/*
void UnlockItemAction::UnlockItem(Item* item, uint8 bag, uint8 slot)
{
    if (!item)
    {
        botAI->TellMaster("Tried to unlock an invalid item.");
        return;
    }

    uint32 spellId = 1804; // Pick Lock spell ID

    botAI->TellMaster("Casting Pick Lock on " + chat->FormatItem(item->GetTemplate()));

    // Step 1: Create the spell cast packet
    WorldPacket packet(CMSG_CAST_SPELL);

    uint8 castCount = 0;
    uint8 castFlags = 0;
    uint32 targetFlags = TARGET_FLAG_GAMEOBJECT_ITEM; // Ensure we're correctly using GAMEOBJECT_ITEM

    packet << castCount;       // Cast count (always 0)
    packet << spellId;         // Spell ID (Pick Lock)
    packet << castFlags;       // Cast flags
    packet << targetFlags;     // Correct target flag for Pick Lock
    packet << item->GetGUID(); // Pass the locked item GUID

    // Step 2: Ensure PlayerBots Handles This Target Type
    SpellCastTargets targets;
    targets.SetItemTarget(item); // Correct method for setting an item target
    targets.Write(packet);       // Attach target data to packet

    // Step 3: Send the packet
    bot->GetSession()->HandleCastSpellOpcode(packet);

    botAI->TellMaster("Pick Lock spell cast with correct target.");
}
*/

