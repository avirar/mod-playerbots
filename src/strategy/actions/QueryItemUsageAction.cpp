/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "QueryItemUsageAction.h"

#include "ChatHelper.h"
#include "Event.h"
#include "ItemUsageValue.h"
#include "Playerbots.h"

bool QueryItemUsageAction::Execute(Event event)
{
    // Extract the parameter passed with the event (the text, which includes the item or quest link)
    std::string text = event.getParam();

    // Parse item IDs from the text
    ItemIds itemIds = chat->parseItems(text);

    // Check if any item IDs were found
    if (!itemIds.empty())
    {
        // Use the first item ID found (if multiple are found)
        uint32 itemId = *itemIds.begin();

        // Get the item template from the item ID
        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
        if (!itemTemplate)  // Handle cases where the item is not found
        {
            botAI->TellMaster("Item not found.");
            return false;
        }

        // Get the count of items the bot has
        uint32 count = GetCount(itemTemplate);
        uint32 total = bot->GetItemCount(itemId);

        // Call QueryItem to get the item usage and related information
        std::string output = QueryItem(itemTemplate, count, total);
        
        // Send the formatted output back to the master
        botAI->TellMaster(output);

        return true;  // Return true to indicate success
    }

    // If no valid item link is found in the input, notify the master
    botAI->TellMaster("No valid item link found.");
    return false;
}



uint32 QueryItemUsageAction::GetCount(ItemTemplate const* item)
{
    if (!item)  // Check if item template is valid
        return 0;

    uint32 total = 0;
    std::vector<Item*> items = InventoryAction::parseItems(item->Name1);

    if (items.empty())  // Check if parseItems returned a valid result
        return 0;

    for (std::vector<Item*>::iterator i = items.begin(); i != items.end(); ++i)
    {
        total += (*i)->GetCount();
    }

    return total;
}

std::string const QueryItemUsageAction::QueryItem(ItemTemplate const* item, uint32 count, uint32 total)
{
    if (!item)  // Ensure item template is valid
    {
        return "Invalid item";
    }
    std::ostringstream out;
    std::string usage = QueryItemUsage(item);
    std::string const quest = QueryQuestItem(item->ItemId);
    std::string const price = QueryItemPrice(item);
    if (usage.empty())
        usage = (quest.empty() ? "Useless" : "Quest");

    out << chat->FormatItem(item, count, total) << ": " << usage;
    if (!quest.empty())
        out << ", " << quest;

    if (!price.empty())
        out << ", " << price;

    return out.str();
}

std::string const QueryItemUsageAction::QueryItemUsage(ItemTemplate const* item)
{
    if (!item)  // Ensure item template is valid
    {
        return "Invalid item";
    }
    std::ostringstream out;
    out << item->ItemId;
    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", out.str());
    switch (usage)
    {
        case ITEM_USAGE_EQUIP:
            return "Equip";
        case ITEM_USAGE_REPLACE:
            return "Equip (replace)";
        case ITEM_USAGE_BAD_EQUIP:
            return "Equip (temporary)";
        case ITEM_USAGE_BROKEN_EQUIP:
            return "Broken Equip";
        case ITEM_USAGE_QUEST:
            return "Quest (other)";
        case ITEM_USAGE_SKILL:
            return "Tradeskill";
        case ITEM_USAGE_USE:
            return "Use";
        case ITEM_USAGE_GUILD_TASK:
            return "Guild task";
        case ITEM_USAGE_DISENCHANT:
            return "Disenchant";
        case ITEM_USAGE_VENDOR:
            return "Vendor";
        case ITEM_USAGE_AH:
            return "Auctionhouse";
        case ITEM_USAGE_AMMO:
            return "Ammunition";
        default:
            break;
    }

    return "";
}

std::string const QueryItemUsageAction::QueryItemPrice(ItemTemplate const* item)
{
    if (!sRandomPlayerbotMgr->IsRandomBot(bot))
        return "";

    if (item->Bonding == BIND_WHEN_PICKED_UP)
        return "";

    std::ostringstream msg;
    std::vector<Item*> items = InventoryAction::parseItems(item->Name1);
    int32 sellPrice = 0;
    if (!items.empty())
    {
        for (std::vector<Item*>::iterator i = items.begin(); i != items.end(); ++i)
        {
            Item* sell = *i;
            int32 price =
                sell->GetCount() * sell->GetTemplate()->SellPrice * sRandomPlayerbotMgr->GetSellMultiplier(bot);
            if (!sellPrice || sellPrice > price)
                sellPrice = price;
        }
    }
    if (sellPrice)
        msg << "Sell: " << chat->formatMoney(sellPrice);

    std::ostringstream out;
    out << item->ItemId;
    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", out.str());
    if (usage == ITEM_USAGE_NONE)
        return msg.str();

    int32 buyPrice = item->BuyPrice * sRandomPlayerbotMgr->GetBuyMultiplier(bot);
    if (buyPrice)
    {
        if (sellPrice)
            msg << " ";

        msg << "Buy: " << chat->formatMoney(buyPrice);
    }

    return msg.str();
}

std::string const QueryItemUsageAction::QueryQuestItem(uint32 itemId)
{
    Player* bot = botAI->GetBot();
    QuestStatusMap& questMap = bot->getQuestStatusMap();
    for (QuestStatusMap::const_iterator i = questMap.begin(); i != questMap.end(); i++)
    {
        Quest const* questTemplate = sObjectMgr->GetQuestTemplate(i->first);
        if (!questTemplate)
            continue;

        uint32 questId = questTemplate->GetQuestId();
        QuestStatus status = bot->GetQuestStatus(questId);
        if (status == QUEST_STATUS_INCOMPLETE ||
            (status == QUEST_STATUS_COMPLETE && !bot->GetQuestRewardStatus(questId)))
        {
            QuestStatusData const& questStatus = i->second;
            std::string const usage = QueryQuestItem(itemId, questTemplate, &questStatus);
            if (!usage.empty())
                return usage;
        }
    }

    return "";
}

std::string const QueryItemUsageAction::QueryQuestItem(uint32 itemId, Quest const* questTemplate,
                                                       QuestStatusData const* questStatus)
{
    for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
    {
        if (questTemplate->RequiredItemId[i] != itemId)
            continue;

        uint32 required = questTemplate->RequiredItemCount[i];
        uint32 available = questStatus->ItemCount[i];
        if (!required)
            continue;

        return chat->FormatQuestObjective(chat->FormatQuest(questTemplate), available, required);
    }

    return "";
}
