/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "ItemWeightAction.h"
#include "Playerbots.h"
#include "StatsWeightCalculator.h"
#include "ChatHelper.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"

bool ItemWeightAction::Execute(Event event)
{
    std::string itemLink = event.getParam();  // Get the item link from the player's chat input
    uint32 itemId = botAI->GetChatHelper()->ExtractItemIdFromLink(itemLink);  // Extract itemId from item link

    if (!itemId)
    {
        botAI->TellMaster("Invalid item link.");
        return false;
    }

    ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(itemId);
    if (!itemProto)
    {
        botAI->TellMaster("Could not find the item.");
        return false;
    }

    // Calculate the item's weight using StatsWeightCalculator
    StatsWeightCalculator calculator(bot);
    float itemWeight = calculator.CalculateItem(itemId);

    // Form the message with the item name and weight
    char message[256];
    snprintf(message, sizeof(message), "[%s] weighs %f", itemProto->Name1.c_str(), itemWeight);

    // Output the result to the master player
    botAI->TellMaster(message);
    
    return true;
}
