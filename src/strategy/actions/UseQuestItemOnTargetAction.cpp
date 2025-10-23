/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "UseQuestItemOnTargetAction.h"

#include "ChatHelper.h"
#include "Event.h"
#include "GameObject.h"
#include "Item.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "QuestItemHelper.h"
#include "SpellInfo.h"
#include "Unit.h"
#include "Object.h"

// Use the same range as grinding distance for quest target search

bool UseQuestItemOnTargetAction::Execute(Event event)
{
    uint32 spellId = 0;

    // Find the best quest item to use
    Item* questItem = QuestItemHelper::FindBestQuestItem(bot, &spellId);
    if (!questItem)
    {
        return false;
    }

    // Find the best target for this quest item
    WorldObject* target = QuestItemHelper::FindBestTargetForQuestItem(botAI, spellId, questItem);
    if (!target)
    {
        return false;
    }

    // Debug: Show which target we selected for actual usage
    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "QuestItem: Selected target for spell cast: " << target->GetName() 
            << " (GUID:" << target->GetGUID().ToString() << ")";
        botAI->TellMaster(out.str());
    }

    // Check if we're in range of the target (use the spell's actual range minus buffer)
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    float range = spellInfo ? (spellInfo->GetMaxRange() - 2.0f) : (INTERACTION_DISTANCE - 2.0f); // -2.0f buffer for reliable casting
    
    // Ensure minimum distance is INTERACTION_DISTANCE minus buffer for quest item interactions
    if (range <= 0.0f)
        range = 0.5f; // -2.0f buffer for reliable interaction
        
    float distance = bot->GetDistance(target);
    
    
    if (distance > range)
    {
        return false;
    }
    
    // Use the quest item on the target
    return UseQuestItemOnTarget(questItem, target);
}

bool UseQuestItemOnTargetAction::isUseful()
{
    // Process pending quest item casts periodically (clean up timeouts)
    QuestItemHelper::ProcessPendingQuestItemCasts(botAI);

    uint32 spellId = 0;
    
    // Check if we have any usable quest items
    Item* questItem = QuestItemHelper::FindBestQuestItem(bot, &spellId);
    if (!questItem)
    {
        return false;
    }

    // Check if there are valid targets available
    WorldObject* target = QuestItemHelper::FindBestTargetForQuestItem(botAI, spellId, questItem);
    bool useful = (target != nullptr);
    
    
    return useful;
}

bool UseQuestItemOnTargetAction::isPossible()
{
    return true;
}

bool UseQuestItemOnTargetAction::UseQuestItemOnTarget(Item* item, WorldObject* target)
{
    if (!item || !target)
        return false;

    // First verify the item pointer is not pointing to deallocated memory
    // by checking if it's still in the bot's inventory before accessing any properties
    ObjectGuid itemGuid;
    uint8 bagIndex = 0;
    uint8 slot = 0;
    
    try 
    {
        itemGuid = item->GetGUID();
        bagIndex = item->GetBagSlot();
        slot = item->GetSlot();
    }
    catch (...)
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "Quest item pointer is invalid - cannot use";
            botAI->TellMaster(out.str());
        }
        return false;
    }

    // Verify item is still in player inventory using the GUID
    Item* inventoryItem = bot->GetItemByGuid(itemGuid);
    if (!inventoryItem || inventoryItem != item)
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "Quest item no longer in inventory or changed - cannot use";
            botAI->TellMaster(out.str());
        }
        return false;
    }

    // Now safely get the item template from the verified inventory item
    ItemTemplate const* itemTemplate = inventoryItem->GetTemplate();
    if (!itemTemplate)
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "Quest item has no template - cannot use";
            botAI->TellMaster(out.str());
        }
        return false;
    }

    // Get the spell ID from the item
    uint32 spellId = 0;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (itemTemplate->Spells[i].SpellId > 0)
        {
            spellId = itemTemplate->Spells[i].SpellId;
            break;
        }
    }

    // Clear movement states like other item uses do
    bot->ClearUnitState(UNIT_STATE_CHASE);
    bot->ClearUnitState(UNIT_STATE_FOLLOW);

    if (bot->isMoving())
    {
        bot->StopMoving();
        botAI->SetNextCheckDelay(sPlayerbotAIConfig->globalCoolDown);
        return false;
    }

    if (bot->IsMounted())
    {
        bot->Dismount();
        botAI->SetNextCheckDelay(sPlayerbotAIConfig->globalCoolDown);
    }


    // Check if target is a GameObject (for OPEN_LOCK spells)
    GameObject* gameObject = target->ToGameObject();
    if (gameObject)
    {
        // Record pending cast before using spell
        QuestItemHelper::RecordPendingQuestItemCast(botAI, target, spellId);

        // Use PlayerbotAI's GameObject casting method
        bool result = botAI->CastSpell(spellId, gameObject, item);
        
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "Using " << chat->FormatItem(itemTemplate) << " on GameObject " << gameObject->GetName() 
                << " (result: " << (result ? "success" : "failed") << ")";
            botAI->TellMaster(out.str());
        }
        
        return result;
    }

    // Cast to Unit for regular target handling
    Unit* unitTarget = target->ToUnit();
    if (!unitTarget)
        return false;

    // Check if spell requires facing the target
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (spellInfo && spellInfo->FacingCasterFlags & 0x1) // SPELL_FACING_FLAG_INFRONT
    {
        if (!bot->HasInArc(static_cast<float>(M_PI), unitTarget))
        {
            // Face the target before casting
            bot->SetFacingToObject(unitTarget);
            botAI->SetNextCheckDelay(sPlayerbotAIConfig->globalCoolDown);

            if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "Facing target " << unitTarget->GetName() << " before using quest item";
                botAI->TellMaster(out.str());
            }

            return false;
        }
    }

    // For regular unit targets, use the original packet-based approach
    uint8 spell_index = 0;
    uint8 cast_count = 1;
    uint32 glyphIndex = 0;
    uint8 castFlags = 0;

    // Create the item use packet
    WorldPacket packet(CMSG_USE_ITEM);
    packet << bagIndex << slot << cast_count << spellId << itemGuid << glyphIndex << castFlags;

    // Add target information
    uint32 targetFlag = TARGET_FLAG_UNIT;
    packet << targetFlag << unitTarget->GetGUID().WriteAsPacked();

    // Record pending cast before sending packet
    QuestItemHelper::RecordPendingQuestItemCast(botAI, unitTarget, spellId);

    // Send the packet
    bot->GetSession()->HandleUseItemOpcode(packet);

    // Safety check: Verify item is still valid after packet handling
    Item* postUseItem = bot->GetItemByGuid(itemGuid);
    if (!postUseItem)
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "Used quest item on " << unitTarget->GetName() << " (item was consumed/invalidated)";
            botAI->TellMaster(out.str());
        }
        return true;
    }

    // Get template safely from the post-use item
    ItemTemplate const* postUseTemplate = postUseItem->GetTemplate();
    if (!postUseTemplate)
    {
        if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "Used quest item (unknown) on " << unitTarget->GetName();
            botAI->TellMaster(out.str());
        }
        return true;
    }

    if (botAI && botAI->HasStrategy("debug questitems", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "Using " << chat->FormatItem(postUseTemplate) << " on " << unitTarget->GetName();
        botAI->TellMaster(out.str());
    }

    // NOTE: Do not record usage immediately - let the pending system handle it
    // based on server response (success/failure)

    return true;
}
