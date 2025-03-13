/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "SellAction.h"

#include "Event.h"
#include "ItemUsageValue.h"
#include "ItemVisitors.h"
#include "Playerbots.h"

class SellItemsVisitor : public IterateItemsVisitor
{
public:
    SellItemsVisitor(SellAction* action) : IterateItemsVisitor(), action(action) {}

    bool Visit(Item* item) override
    {
        action->Sell(item);
        return true;
    }

private:
    SellAction* action;
};

class SellGrayItemsVisitor : public SellItemsVisitor
{
public:
    SellGrayItemsVisitor(SellAction* action) : SellItemsVisitor(action) {}

    bool Visit(Item* item) override
    {
        if (item->GetTemplate()->Quality != ITEM_QUALITY_POOR)
            return true;

        return SellItemsVisitor::Visit(item);
    }
};

class SellVendorItemsVisitor : public SellItemsVisitor
{
public:
    SellVendorItemsVisitor(SellAction* action, AiObjectContext* con, PlayerbotAI* ai) : SellItemsVisitor(action), botAI(ai) { context = con; }

    AiObjectContext* context;
    PlayerbotAI* botAI;

bool Visit(Item* item) override
{
    if (!context)
    {
        botAI->TellMaster("SellVendorItemsVisitor: Context is null.");
        return true;
    }

    Player* bot = botAI->GetBot();
    if (!bot)
    {
        botAI->TellMaster("SellVendorItemsVisitor: Bot is null. Possible AI initialization issue.");
        return true;
    }

    botAI->TellMaster("SellVendorItemsVisitor: Bot retrieved successfully!");

    ItemUsage usage = context->GetValue<ItemUsage>("item usage", item->GetEntry())->Get();

    // Check if we have excess quest items
    uint32 excessCount = GetExcessQuestItemCount(bot, item);
    if (excessCount > 0)
    {
        botAI->TellMaster("SellVendorItemsVisitor: Selling excess quest items.");
        SellExcessItem(bot, item, excessCount);
        return false; // **PREVENT further processing, avoid duplicate selling**
    }

    if (usage != ITEM_USAGE_VENDOR && usage != ITEM_USAGE_AH)
        return true;

    return SellItemsVisitor::Visit(item);
}

private:
    uint32 GetExcessQuestItemCount(Player* bot, Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        uint32 itemCount = item->GetCount();

        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = bot->GetQuestSlotQuestId(slot);
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (!quest)
                continue;

            for (uint8 i = 0; i < 4; i++)
            {
                if (quest->RequiredItemId[i] != proto->ItemId)
                    continue;

                uint32 requiredCount = quest->RequiredItemCount[i];
                uint32 currentCount = AI_VALUE2(uint32, "item count", proto->Name1);

                if (currentCount > requiredCount)
                    return currentCount - requiredCount;
            }
        }

        return 0; // No excess
    }
    bool SellExcessItem(Player* bot, Item* item, uint32 excessCount)
    {
        if (excessCount == 0 || !item)
            return true;
    
        std::ostringstream out;
        GuidVector vendors = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
    
        // Get item position
        uint16 srcSlot = item->GetPos();
        uint16 freeSlot = INVENTORY_SLOT_BAG_0; // Default to main backpack
    
        // Find a free inventory slot
        bool foundSlot = false;
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        {
            if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot)) // Empty slot found in main bag
            {
                freeSlot = (INVENTORY_SLOT_BAG_0 << 8) | slot;
                foundSlot = true;
                break;
            }
        }
    
        // If no free slots in main inventory, check bags
        if (!foundSlot)
        {
            for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            {
                if (Bag* pBag = dynamic_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
                {
                    if (pBag->GetFreeSlots() > 0) // If the bag has a free slot
                    {
                        for (uint8 slot = 0; slot < pBag->GetBagSize(); ++slot)
                        {
                            if (!bot->GetItemByPos(bag, slot)) // Found empty slot in the bag
                            {
                                freeSlot = (bag << 8) | slot;
                                foundSlot = true;
                                break;
                            }
                        }
                    }
                }
                if (foundSlot)
                    break;
            }
        }
    
        if (!foundSlot)
        {
            botAI->TellMaster("No free slot found to split excess item stack.");
            return false; // Prevent selling the entire stack
        }
    
        // If the item count is greater than the excess amount, split it
        if (item->GetCount() > excessCount)
        {
            botAI->TellMaster("Splitting " + std::to_string(excessCount) + " from stack of " + std::to_string(item->GetCount()));
            bot->SplitItem(srcSlot, freeSlot, excessCount);
    
            // Get the newly split item
            item = bot->GetItemByPos(freeSlot >> 8, freeSlot & 255);
            if (!item)
            {
                botAI->TellMaster("Failed to split item, cannot proceed with selling.");
                return false;
            }
        }
    
        for (ObjectGuid const vendorguid : vendors)
        {
            Creature* pCreature = bot->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_VENDOR);
            if (!pCreature)
                continue;
    
            ObjectGuid itemguid = item->GetGUID();
            uint32 botMoney = bot->GetMoney();
    
            // Sell the **split stack**
            WorldPacket p;
            p << vendorguid << itemguid << excessCount;
            bot->GetSession()->HandleSellItemOpcode(p);
    
            if (botAI->HasCheat(BotCheatMask::gold))
            {
                bot->SetMoney(botMoney);
            }
    
            out << "Sold " << excessCount << " of " << botAI->GetChatHelper()->FormatItem(item->GetTemplate());
            botAI->TellMaster(out.str());
    
            bot->PlayDistanceSound(120);
            break;
        }
    
        return true;
    }

};

bool SellAction::Execute(Event event)
{
    std::string const text = event.getParam();
    if (text == "gray" || text == "*")
    {
        SellGrayItemsVisitor visitor(this);
        IterateItems(&visitor);
        return true;
    }

    if (text == "vendor")
    {
        SellVendorItemsVisitor visitor(this, context, botAI);
        IterateItems(&visitor);
        return true;
    }

    if (text != "")
    {
        std::vector<Item*> items = parseItems(text, ITERATE_ITEMS_IN_BAGS);
        for (Item* item : items)
        {
            Sell(item);
        }
        return true;
    }

    botAI->TellError("Usage: s gray/*/vendor/[item link]");
    return false;
}

void SellAction::Sell(FindItemVisitor* visitor)
{
    IterateItems(visitor);
    std::vector<Item*> items = visitor->GetResult();
    for (Item* item : items)
    {
        Sell(item);
    }
}

void SellAction::Sell(Item* item)
{
    std::ostringstream out;

    GuidVector vendors = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();

    for (ObjectGuid const vendorguid : vendors)
    {
        Creature* pCreature = bot->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_VENDOR);
        if (!pCreature)
            continue;

        ObjectGuid itemguid = item->GetGUID();
        uint32 count = item->GetCount();

        uint32 botMoney = bot->GetMoney();

        WorldPacket p;
        p << vendorguid << itemguid << count;
        bot->GetSession()->HandleSellItemOpcode(p);

        if (botAI->HasCheat(BotCheatMask::gold))
        {
            bot->SetMoney(botMoney);
        }

        out << "Selling " << chat->FormatItem(item->GetTemplate());
        botAI->TellMaster(out);

        bot->PlayDistanceSound(120);
        break;
    }
}
