/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "SellAction.h"

#include "Event.h"
#include "ItemUsageValue.h"
#include "ItemVisitors.h"
#include "Playerbots.h"
#include "ItemPackets.h"
#include "ItemCountValue.h"

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
    SellVendorItemsVisitor(SellAction* action, AiObjectContext* con) : SellItemsVisitor(action) { context = con; }

    AiObjectContext* context;

    bool Visit(Item* item) override
    {
        ItemUsage usage = context->GetValue<ItemUsage>("item usage", item->GetEntry())->Get();
        if (usage != ITEM_USAGE_VENDOR && usage != ITEM_USAGE_AH)
            return true;

        return SellItemsVisitor::Visit(item);
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
        SellVendorItemsVisitor visitor(this, context);
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

uint32 SellAction::GetQuestItemRequirement(uint32 itemId)
{
    // Loop through all active quests to find if this item is required
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (questId == 0)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        // Check if the item is required for this quest
        for (uint8 i = 0; i < 4; i++)
        {
            if (quest->RequiredItemId[i] == itemId)
            {
                return quest->RequiredItemCount[i];
            }
        }
    }

    return 0; // Item is not required for any active quest
}

void SellAction::Sell(Item* item)
{
    if (!item)
        return;

    std::ostringstream out;
    uint32 itemId = item->GetEntry();
    ItemTemplate const* proto = item->GetTemplate();

    // Check if this is a quest item
    uint32 questRequirement = GetQuestItemRequirement(itemId);

    Item* itemToSell = item;
    uint32 countToSell = item->GetCount();

    if (questRequirement > 0)
    {
        // This is a quest item - need to handle carefully
        // Get total count of this item across all stacks
        QueryItemCountVisitor countVisitor(itemId);
        IterateItems(&countVisitor, ITERATE_ITEMS_IN_BAGS);
        uint32 totalCount = countVisitor.GetCount();

        // Calculate how many we can sell (excess)
        if (totalCount <= questRequirement)
        {
            // We don't have excess, don't sell
            return;
        }

        uint32 excessCount = totalCount - questRequirement;

        // If the current item stack is larger than the excess we want to sell, we need to split
        if (item->GetCount() > excessCount)
        {
            // SAFETY CHECK 3 & 4: Find an empty bag slot to split the stack
            ItemPosCountVec dest;
            InventoryResult result = bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, excessCount);

            // SAFETY CHECK 4: Handle case where no empty slots exist
            if (result != EQUIP_ERR_OK)
            {
                // Can't find empty slot, skip selling
                // This prevents errors when bags are full
                return;
            }

            // SAFETY CHECK 3: Ensure we have a valid destination slot
            if (dest.empty())
            {
                // No valid destination slot found
                return;
            }

            // Get source position
            uint16 srcPos = item->GetPos();
            uint16 dstPos = dest[0].pos;

            // SAFETY CHECK 3: Validate destination position
            uint8 dstBag = (dstPos >> 8) & 255;
            uint8 dstSlot = dstPos & 255;

            // Verify destination is actually empty
            if (bot->GetItemByPos(dstBag, dstSlot) != nullptr)
            {
                // Destination slot is not empty, abort split
                return;
            }

            // Split the item - move excessCount to a new stack
            // We'll split OFF the amount we want to KEEP, leaving the excess in the original stack
            uint32 keepCount = item->GetCount() - excessCount;

            // Store original item count for verification
            uint32 originalCount = item->GetCount();

            // Split: move keepCount from source, leaving excessCount behind
            bot->SplitItem(srcPos, dstPos, keepCount);

            // SAFETY CHECK 1: Verify split succeeded before attempting to sell
            itemToSell = bot->GetItemByPos(srcPos);
            if (!itemToSell)
            {
                // Original item no longer exists - split failed
                return;
            }

            // SAFETY CHECK 1: Verify the split resulted in correct counts
            if (itemToSell->GetCount() != excessCount)
            {
                // Split didn't work as expected, don't sell
                return;
            }

            // SAFETY CHECK 1: Verify the new stack was created with correct count
            Item* keptStack = bot->GetItemByPos(dstPos);
            if (!keptStack || keptStack->GetCount() != keepCount)
            {
                // The kept portion wasn't created correctly
                // Don't sell to avoid losing quest items
                return;
            }

            countToSell = excessCount;
        }
        else
        {
            // Current stack is small enough, we can sell it entirely
            countToSell = item->GetCount();
        }
    }

    // Proceed with selling
    GuidVector vendors = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();

    for (ObjectGuid const vendorguid : vendors)
    {
        Creature* pCreature = bot->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_VENDOR);
        if (!pCreature)
            continue;

        ObjectGuid itemguid = itemToSell->GetGUID();

        uint32 botMoney = bot->GetMoney();

        WorldPacket p(CMSG_SELL_ITEM);
        p << vendorguid << itemguid << countToSell;

        WorldPackets::Item::SellItem nicePacket(std::move(p));
        nicePacket.Read();
        bot->GetSession()->HandleSellItemOpcode(nicePacket);

        if (botAI->HasCheat(BotCheatMask::gold))
        {
            bot->SetMoney(botMoney);
        }

        out << "Selling " << chat->FormatItem(proto);
        if (questRequirement > 0)
        {
            out << " (keeping " << questRequirement << " for quest)";
        }
        botAI->TellMaster(out);

        bot->PlayDistanceSound(120);
        break;
    }
}
