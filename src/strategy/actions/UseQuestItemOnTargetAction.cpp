/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "UseQuestItemOnTargetAction.h"

#include "ChatHelper.h"
#include "Event.h"
#include "Item.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "QuestItemHelper.h"
#include "SpellInfo.h"
#include "Unit.h"

// Use the same range as grinding distance for quest target search

bool UseQuestItemOnTargetAction::Execute(Event event)
{
    // Use shared computation from isUseful to avoid duplicate work
    if (!isUseful())
        return false;
        
    // We know from isUseful() that we have both item and target - get them again
    uint32 spellId = 0;
    Item* questItem = QuestItemHelper::FindBestQuestItem(bot, &spellId);
    Unit* target = QuestItemHelper::FindBestTargetForQuestItem(botAI, spellId);
    
    // Final range check with precise distance calculation
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    float range = spellInfo ? (spellInfo->GetMaxRange() - 2.0f) : (INTERACTION_DISTANCE - 2.0f);
    
    if (range <= 0.0f || range < (INTERACTION_DISTANCE - 2.0f))
        range = INTERACTION_DISTANCE - 2.0f;
        
    if (bot->GetDistance(target) > range)
        return false;
    
    return UseQuestItemOnTarget(questItem, target);
}

bool UseQuestItemOnTargetAction::isUseful()
{
    uint32 spellId = 0;
    
    // Check if we have any usable quest items
    Item* questItem = QuestItemHelper::FindBestQuestItem(bot, &spellId);
    if (!questItem)
        return false;

    // Check if there are valid targets available and in range
    Unit* target = QuestItemHelper::FindBestTargetForQuestItem(botAI, spellId);
    if (!target)
        return false;
    
    // Do range check here so Execute() doesn't need to repeat the work
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    float range = spellInfo ? (spellInfo->GetMaxRange() - 2.0f) : (INTERACTION_DISTANCE - 2.0f);
    
    if (range <= 0.0f || range < (INTERACTION_DISTANCE - 2.0f))
        range = INTERACTION_DISTANCE - 2.0f;
        
    return bot->GetDistance(target) <= range;
}

bool UseQuestItemOnTargetAction::isPossible()
{
    return true;
}

bool UseQuestItemOnTargetAction::UseQuestItemOnTarget(Item* item, Unit* target)
{
    if (!item || !target)
        return false;

    // For quest items, we need to bypass normal spell checks
    // and send the item use packet directly with the target
    uint8 bagIndex = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    uint8 spell_index = 0;
    uint8 cast_count = 1;
    ObjectGuid item_guid = item->GetGUID();
    uint32 glyphIndex = 0;
    uint8 castFlags = 0;

    // Get the spell ID from the item
    uint32 spellId = 0;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (item->GetTemplate()->Spells[i].SpellId > 0)
        {
            spellId = item->GetTemplate()->Spells[i].SpellId;
            break;
        }
    }

    // Create the item use packet
    WorldPacket packet(CMSG_USE_ITEM);
    packet << bagIndex << slot << cast_count << spellId << item_guid << glyphIndex << castFlags;

    // Add target information
    uint32 targetFlag = TARGET_FLAG_UNIT;
    packet << targetFlag << target->GetGUID().WriteAsPacked();

    // Clear movement states like other item uses do
    bot->ClearUnitState(UNIT_STATE_CHASE);
    bot->ClearUnitState(UNIT_STATE_FOLLOW);

    if (bot->isMoving())
    {
        bot->StopMoving();
        botAI->SetNextCheckDelay(sPlayerbotAIConfig->globalCoolDown);
        return false;
    }

    // Send the packet
    bot->GetSession()->HandleUseItemOpcode(packet);

    std::ostringstream out;
    out << "Using " << chat->FormatItem(item->GetTemplate()) << " on " << target->GetName();
    botAI->TellMasterNoFacing(out.str());

    return true;
}
