/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ItemKeepPolicy.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <unordered_map>
#include <vector>

#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "QuestDef.h"

namespace
{
constexpr uint32 KEEP_TRADE_STACKS = 5;
constexpr uint32 KEEP_DISENCHANT_STACKS = 2;
constexpr uint32 KEEP_CONSUMABLE_STACKS = 3;
constexpr uint32 KEEP_GEAR_COPIES = 1;

std::array<uint32, 16> const PROFESSION_TOOL_IDS = {
    5956,   // Blacksmith Hammer
    6218,   // Runed Copper Rod
    6219,   // Arclight Spanner
    6339,   // Runed Silver Rod
    7005,   // Skinning Knife
    11130,  // Runed Golden Rod
    11145,  // Runed Truesilver Rod
    16207,  // Runed Arcanite Rod
    20815,  // Jeweler's Kit
    22461,  // Runed Fel Iron Rod
    22462,  // Runed Adamantite Rod
    22463,  // Runed Eternium Rod
    2901,   // Mining Pick
    39505,  // Virtuoso Inking Set
    44451,  // Runed Cobalt Rod
    44452   // Runed Titanium Rod
};

std::unordered_map<uint32, std::vector<uint32>> const& GetQuestSourceItemIndex()
{
    static std::unordered_map<uint32, std::vector<uint32>> const index = []
    {
        std::unordered_map<uint32, std::vector<uint32>> result;
        for (auto const& questPair : sObjectMgr->GetQuestTemplates())
        {
            Quest const* quest = questPair.second;
            if (!quest)
                continue;

            uint32 const sourceItemId = quest->GetSrcItemId();
            if (sourceItemId)
                result[sourceItemId].push_back(quest->GetQuestId());
        }
        return result;
    }();
    return index;
}

bool IsProfessionTool(ItemTemplate const* proto)
{
    if (std::find(PROFESSION_TOOL_IDS.begin(), PROFESSION_TOOL_IDS.end(), proto->ItemId) != PROFESSION_TOOL_IDS.end())
        return true;

    if (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_TRADE_GOODS)
        return false;

    return proto->TotemCategory != 0;
}

bool IsAlwaysKeptContainer(ItemTemplate const* proto)
{
    return proto->Class == ITEM_CLASS_KEY || proto->Class == ITEM_CLASS_CONTAINER || proto->Class == ITEM_CLASS_QUIVER;
}

bool HasCompletedEveryAssociatedQuest(PlayerbotAI* botAI, ItemTemplate const* proto)
{
    Player* bot = botAI->GetBot();
    bool associated = false;

    if (proto->StartQuest)
    {
        associated = true;
        if (bot->GetQuestStatus(proto->StartQuest) != QUEST_STATUS_REWARDED)
            return false;
    }

    auto const& index = GetQuestSourceItemIndex();
    auto const itr = index.find(proto->ItemId);
    if (itr != index.end())
    {
        associated = true;
        for (uint32 questId : itr->second)
        {
            if (bot->GetQuestStatus(questId) != QUEST_STATUS_REWARDED)
                return false;
        }
    }

    return associated;
}

uint32 GetQuestItemRequirement(PlayerbotAI* botAI, uint32 itemId)
{
    uint32 requirement = 0;
    for (auto const& questPair : botAI->GetCurrentQuestsRequiringItemId(itemId))
    {
        Quest const* quest = questPair.first;
        if (!quest)
            continue;

        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            if (quest->RequiredItemId[i] == itemId)
                requirement = std::max(requirement, quest->RequiredItemCount[i]);
        }
    }

    return requirement;
}

uint32 GetStackCount(ItemTemplate const* proto, uint32 stacks)
{
    return stacks * std::max<uint32>(1, proto->GetMaxStackSize());
}

std::string NormalizeCategoryToken(std::string const& token)
{
    std::string normalized;
    for (char character : token)
    {
        if (std::isspace(static_cast<unsigned char>(character)))
            continue;

        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return normalized;
}
}  // namespace

ItemKeepRule GetItemKeepRule(PlayerbotAI* botAI, ItemTemplate const* proto, ItemUsage usage)
{
    ItemKeepRule rule;
    if (!botAI || !proto)
        return rule;

    // A source item whose quests are all rewarded has no remaining purpose. The usage and
    // sell-price guards keep it from matching reagents or items that can still be sold.
    if (usage == ITEM_USAGE_NONE && proto->SellPrice == 0 && HasCompletedEveryAssociatedQuest(botAI, proto))
    {
        rule.completedQuestSource = true;
        rule.category = ItemCleanupCategory::Useless;
        return rule;
    }

    if (sPlayerbotAIConfig.IsInRandomQuestItemList(proto->ItemId) || IsProfessionTool(proto) ||
        IsAlwaysKeptContainer(proto))
    {
        rule.alwaysKeep = true;
        rule.keepCount = std::numeric_limits<uint32>::max();
        return rule;
    }

    if (usage == ITEM_USAGE_GUILD_TASK)
    {
        rule.category = ItemCleanupCategory::Quest;
        rule.keepCount = std::max<uint32>(1, proto->GetMaxStackSize());
        return rule;
    }

    if (usage == ITEM_USAGE_QUEST || proto->Class == ITEM_CLASS_QUEST)
    {
        rule.category = ItemCleanupCategory::Quest;
        rule.keepCount = std::max<uint32>(1, GetQuestItemRequirement(botAI, proto->ItemId));
        return rule;
    }

    bool const isTradeClass =
        proto->Class == ITEM_CLASS_TRADE_GOODS || proto->Class == ITEM_CLASS_MISC || proto->Class == ITEM_CLASS_REAGENT;
    bool const isRecipe = proto->Class == ITEM_CLASS_RECIPE;

    if (isTradeClass || isRecipe)
    {
        // ItemUsageValue falls through to VENDOR/AH above two stacks, so check the crafting
        // usefulness directly instead of trusting the usage bucket. Always use the permissive
        // check: the bag-space fallback decides how many stacks to keep, not whether the
        // reagent matters at all.
        bool needed = usage == ITEM_USAGE_SKILL || usage == ITEM_USAGE_KEEP;
        if (!needed && isTradeClass)
        {
            ScopedPermissiveReagentCheck const permissiveReagentCheck;
            needed = ItemUsageValue(botAI).IsItemNeededForUsefullSpell(proto);
        }

        if (needed)
        {
            rule.category = ItemCleanupCategory::Trade;
            rule.keepCount = isRecipe ? 1 : GetStackCount(proto, KEEP_TRADE_STACKS);
            return rule;
        }
    }

    switch (usage)
    {
        case ITEM_USAGE_EQUIP:
        case ITEM_USAGE_REPLACE:
        case ITEM_USAGE_BAD_EQUIP:
        case ITEM_USAGE_BROKEN_EQUIP:
            rule.category = ItemCleanupCategory::Gear;
            rule.keepCount = KEEP_GEAR_COPIES;
            return rule;
        case ITEM_USAGE_DISENCHANT:
            rule.category = ItemCleanupCategory::Disenchant;
            rule.keepCount = GetStackCount(proto, KEEP_DISENCHANT_STACKS);
            return rule;
        case ITEM_USAGE_USE:
        case ITEM_USAGE_AMMO:
            rule.category = ItemCleanupCategory::Consumable;
            rule.keepCount = GetStackCount(proto, KEEP_CONSUMABLE_STACKS);
            return rule;
        case ITEM_USAGE_SKILL:
        case ITEM_USAGE_KEEP:
            if (proto->Class == ITEM_CLASS_CONSUMABLE || proto->Class == ITEM_CLASS_PROJECTILE)
            {
                rule.category = ItemCleanupCategory::Consumable;
                rule.keepCount = GetStackCount(proto, KEEP_CONSUMABLE_STACKS);
            }
            else
            {
                rule.category = ItemCleanupCategory::Trade;
                rule.keepCount = isRecipe ? 1 : std::max<uint32>(1, proto->GetMaxStackSize());
            }
            return rule;
        case ITEM_USAGE_NONE:
        case ITEM_USAGE_VENDOR:
        case ITEM_USAGE_AH:
        default:
            rule.category = ItemCleanupCategory::Useless;
            rule.keepCount = 0;
            return rule;
    }
}

bool TryParseItemCleanupCategory(std::string const& token, ItemCleanupCategory& category)
{
    std::string const normalized = NormalizeCategoryToken(token);
    if (normalized == "useless")
        category = ItemCleanupCategory::Useless;
    else if (normalized == "trade")
        category = ItemCleanupCategory::Trade;
    else if (normalized == "disenchant")
        category = ItemCleanupCategory::Disenchant;
    else if (normalized == "consumable")
        category = ItemCleanupCategory::Consumable;
    else if (normalized == "quest")
        category = ItemCleanupCategory::Quest;
    else if (normalized == "gear")
        category = ItemCleanupCategory::Gear;
    else
        return false;

    return true;
}
