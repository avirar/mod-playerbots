/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgDoProfessions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Bag.h"
#include "BudgetValues.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "GameObject.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ItemUsageValue.h"
#include "Log.h"
#include "LootValues.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "SpellFocusMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StatsWeightCalculator.h"
#include "Trainer.h"
#include "WorldSession.h"

namespace
{
struct ProfessionRecipe
{
    uint32 spellId = 0;
    uint32 outputItemId = 0;
    uint32 outputCount = 1;
    int32 chance = 0;
    uint32 tier = 0;
    uint32 spellFocus = 0;
    bool useful = false;
    bool noItemSpell = false;  // enchanting: target a bag item instead of creating one
    bool marketable = false;   // enchanting: the scroll output is auction-worthy
    bool hasCooldown = false;
    std::array<uint32, MAX_SPELL_REAGENTS> reagents{};
    std::array<uint8, MAX_SPELL_REAGENTS> reagentCounts{};
};

struct ConversionCandidate
{
    uint32 spellId = 0;
    uint32 inputItemId = 0;
    uint32 inputCount = 0;
    std::unordered_map<uint32, LootTemplateOutput> outputs;
};

struct VendorOffer
{
    ObjectGuid guid{};
    uint32 price{0};  // discounted unit price
    uint32 buyCount{1};
    uint32 stock{0};  // units left; UINT32_MAX when unlimited
};

struct BagSnapshot
{
    std::unordered_map<uint32, uint32> counts;
    std::unordered_map<uint32, std::vector<uint32>> stacks;
    std::unordered_map<uint32, std::vector<VendorOffer>> vendorItems;
    std::vector<ConversionCandidate> conversions;
};

struct CraftPlan
{
    std::unordered_map<uint32, uint32> available;
    std::unordered_map<uint32, std::vector<uint32>> stacks;
    std::unordered_set<uint32> seeded;
    std::unordered_set<uint32> visiting;
    std::unordered_set<uint32> plannedSpells;
    uint32 moneyBudget{0};
    std::vector<NewRpgInfo::DoProfessions::CraftStep> steps;
};

struct CraftPlanContext
{
    Player* bot = nullptr;
    NewRpgInfo const* info = nullptr;
    std::vector<ProfessionRecipe> const* recipes = nullptr;
    std::unordered_map<uint32, std::vector<size_t>> const* craftIndex = nullptr;
    BagSnapshot const* bags = nullptr;
    CraftPlan plan;
};

constexpr uint32 BASIC_CAMPFIRE_SPELL = 818;
constexpr uint32 COOKING_FIRE_FOCUS = 4;
constexpr uint32 CAMPFIRE_WAIT_MS = 10 * IN_MILLISECONDS;

constexpr uint32 MILLING_SPELL = 51005;
constexpr uint32 PROSPECTING_SPELL = 31252;
constexpr uint32 DISENCHANT_SPELL = 13262;
constexpr uint32 CONVERSION_INPUT_COUNT = 5;
constexpr uint32 DISENCHANT_INPUT_COUNT = 1;
constexpr uint32 CONVERSION_LOOT_TIMEOUT_MS = 15 * IN_MILLISECONDS;
constexpr uint32 CONVERSION_LOOT_HARD_TIMEOUT_MS = 30 * IN_MILLISECONDS;
constexpr uint32 CONVERSION_GRACE_MS = 6 * IN_MILLISECONDS;
constexpr uint32 CONVERSION_FAILURE_MEMORY_MS = 30 * IN_MILLISECONDS;
constexpr uint32 VENDOR_FAILURE_MEMORY_MS = 5 * MINUTE * IN_MILLISECONDS;
constexpr uint32 RECIPE_FAILURE_MEMORY_MS = 30 * IN_MILLISECONDS;
constexpr uint8 MAX_CRAFT_DEPTH = 5;
constexpr size_t MAX_PLAN_STEPS = 20;
// Spread loot groups (1/6 gems, 25% pigments) stay plannable; 1-3% chase
// outputs do not consume the whole plan budget.
constexpr float MIN_CONVERSION_EXPECTED = 0.1f;

uint32 GetEnchantId(SpellInfo const* spellInfo)
{
    for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
        if (spellInfo->Effects[e].Effect == SPELL_EFFECT_ENCHANT_ITEM)
            return spellInfo->Effects[e].MiscValue;

    return 0;
}

int8 FindUsefulEnchantSlot(Player* bot, SpellInfo const* spellInfo, StatsWeightCalculator& calculator)
{
    uint32 const enchantId = GetEnchantId(spellInfo);
    if (!enchantId)
        return -1;

    SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(enchantId);
    if (!enchant || enchant->slot != PERM_ENCHANTMENT_SLOT)
        return -1;

    if (enchant->requiredSkill && (!bot->HasSkill(enchant->requiredSkill) ||
                                   bot->GetSkillValue(enchant->requiredSkill) < enchant->requiredSkillValue))
        return -1;
    if (enchant->requiredLevel > bot->GetLevel())
        return -1;

    int8 bestSlot = -1;
    float bestGain = 0.0f;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (slot == EQUIPMENT_SLOT_TABARD || slot == EQUIPMENT_SLOT_BODY)
            continue;

        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item || !item->IsFitToSpellRequirements(spellInfo))
            continue;

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            continue;

        uint32 const currentId = item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
        float const newScore = calculator.CalculateEnchant(enchantId, proto->Delay, proto->IsRangedWeapon());
        float const oldScore =
            currentId ? calculator.CalculateEnchant(currentId, proto->Delay, proto->IsRangedWeapon()) : 0.0f;
        if (currentId && oldScore <= 0.0f)
            continue;  // the calculator cannot value this enchant; do not overwrite it
        float const gain = newScore - oldScore;
        if (newScore > 0.0f && gain > bestGain)
        {
            bestGain = gain;
            bestSlot = int8(slot);
        }
    }

    return bestSlot;
}

Item* FindEnchantTarget(Player* bot, SpellInfo const* spellInfo, bool allowGear, StatsWeightCalculator* calculator)
{
    std::vector<Item*> gear;
    std::vector<Item*> vellums;
    auto collect = [spellInfo, &gear, &vellums](Item* item)
    {
        if (!item->IsFitToSpellRequirements(spellInfo))
            return;
        if (item->IsArmorVellum() || item->IsWeaponVellum())
            vellums.push_back(item);
        else
            gear.push_back(item);
    };

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            collect(item);

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bot->GetItemByPos(bagSlot, slot))
                    collect(item);

    if (!vellums.empty())
        return vellums[urand(0, uint32(vellums.size()) - 1)];

    if (!allowGear)
        return nullptr;

    uint32 const enchantId = GetEnchantId(spellInfo);
    std::vector<Item*> candidates;
    for (Item* item : gear)
    {
        uint32 const currentId = item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
        if (!currentId)
        {
            candidates.push_back(item);
            continue;
        }

        if (!calculator || !enchantId)
            continue;

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            continue;

        float const newScore = calculator->CalculateEnchant(enchantId, proto->Delay, proto->IsRangedWeapon());
        float const oldScore = calculator->CalculateEnchant(currentId, proto->Delay, proto->IsRangedWeapon());
        if (oldScore > 0.0f && newScore > 0.0f && newScore > oldScore)
            candidates.push_back(item);
    }

    if (candidates.empty())
        return nullptr;

    return candidates[urand(0, uint32(candidates.size()) - 1)];
}

bool TryBasicCampfire(PlayerbotAI* botAI, Player* bot, NewRpgInfo::DoProfessions& data, uint32 focusId)
{
    if (focusId != COOKING_FIRE_FOCUS || !bot->HasSpell(BASIC_CAMPFIRE_SPELL) ||
        bot->HasSpellCooldown(BASIC_CAMPFIRE_SPELL))
        return false;

    if (bot->isMoving())
        bot->StopMoving();
    if (!bot->IsStandState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    data.lastCampfire = getMSTime();
    LOG_DEBUG("playerbots", "[New RPG] {} professions casting basic campfire", bot->GetName());
    if (botAI->CastSpell(BASIC_CAMPFIRE_SPELL, bot))
        return true;

    data.lastCampfire = 0;
    return false;
}

int32 FindVendorItemSlot(Creature* vendor, uint32 itemId)
{
    VendorItemData const* items = sObjectMgr->GetNpcVendorItemList(vendor->GetEntry());
    if (!items)
        return -1;

    for (uint32 i = 0; i < items->GetItemCount(); ++i)
    {
        VendorItem const* item = items->GetItem(i);
        if (item && item->item == itemId)
            return int32(i);
    }

    return -1;
}

Item* FindLargestBagItemByEntry(Player* bot, uint32 entry)
{
    Item* best = nullptr;
    auto consider = [&best, entry](Item* item)
    {
        if (item && item->GetEntry() == entry && (!best || item->GetCount() > best->GetCount()))
            best = item;
    };

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        consider(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                consider(bot->GetItemByPos(bagSlot, slot));

    return best;
}

bool IsDisenchantableItem(Player* bot, ItemTemplate const* proto, ItemUsage usage);

void InspectBagItem(PlayerbotAI* botAI, Item* item, BagSnapshot& snapshot, std::unordered_set<uint64_t>& seen)
{
    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
        return;

    snapshot.counts[proto->ItemId] += item->GetCount();
    snapshot.stacks[proto->ItemId].push_back(item->GetCount());

    auto addConversion =
        [&snapshot, &seen](uint32 spellId, uint32 inputItemId, uint32 inputCount, LootTemplate const* loot)
    {
        if (!loot)
            return;

        uint64_t const key = (uint64_t(spellId) << 32) | inputItemId;
        if (!seen.insert(key).second)
            return;

        ConversionCandidate candidate;
        candidate.spellId = spellId;
        candidate.inputItemId = inputItemId;
        candidate.inputCount = inputCount;
        CollectLootTemplateOutputs(loot, candidate.outputs);
        if (!candidate.outputs.empty())
            snapshot.conversions.push_back(std::move(candidate));
    };

    Player* bot = botAI->GetBot();
    bool const millable = bot->HasSpell(MILLING_SPELL) && proto->HasFlag(ITEM_FLAG_IS_MILLABLE) &&
                          item->GetCount() >= CONVERSION_INPUT_COUNT &&
                          proto->RequiredSkillRank <= bot->GetSkillValue(SKILL_INSCRIPTION);
    bool const prospectable = bot->HasSpell(PROSPECTING_SPELL) && proto->HasFlag(ITEM_FLAG_IS_PROSPECTABLE) &&
                              item->GetCount() >= CONVERSION_INPUT_COUNT &&
                              proto->RequiredSkillRank <= bot->GetSkillValue(SKILL_JEWELCRAFTING);
    bool const disenchantable = bot->HasSpell(DISENCHANT_SPELL) && proto->DisenchantID &&
                                (proto->Class == ITEM_CLASS_ARMOR || proto->Class == ITEM_CLASS_WEAPON) &&
                                proto->Quality >= ITEM_QUALITY_UNCOMMON && proto->Quality <= ITEM_QUALITY_EPIC;
    if (!millable && !prospectable && !disenchantable)
        return;

    ItemUsage const usage = botAI->GetAiObjectContext()->GetValue<ItemUsage>("item usage", proto->ItemId)->Get();
    if (usage == ITEM_USAGE_QUEST || usage == ITEM_USAGE_GUILD_TASK)
        return;

    if (millable)
        addConversion(MILLING_SPELL, proto->ItemId, CONVERSION_INPUT_COUNT,
                      LootTemplates_Milling.GetLootFor(proto->ItemId));

    if (prospectable)
        addConversion(PROSPECTING_SPELL, proto->ItemId, CONVERSION_INPUT_COUNT,
                      LootTemplates_Prospecting.GetLootFor(proto->ItemId));

    if (disenchantable && IsDisenchantableItem(bot, proto, usage))
        addConversion(DISENCHANT_SPELL, proto->ItemId, DISENCHANT_INPUT_COUNT,
                      LootTemplates_Disenchant.GetLootFor(proto->DisenchantID));
}

void BuildBagSnapshot(PlayerbotAI* botAI, BagSnapshot& snapshot)
{
    Player* bot = botAI->GetBot();
    std::unordered_set<uint64_t> seen;
    auto inspect = [botAI, &snapshot, &seen](Item* item)
    {
        if (item)
            InspectBagItem(botAI, item, snapshot, seen);
    };

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        inspect(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                inspect(bot->GetItemByPos(bagSlot, slot));

    GuidVector const vendors = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
    for (ObjectGuid const guid : vendors)
    {
        Creature* vendor = botAI->GetCreature(guid);
        if (!vendor || !vendor->IsInWorld() || !vendor->IsVendor())
            continue;
        if (bot->GetDistance(vendor) > 100.0f)
            continue;

        VendorItemData const* items = sObjectMgr->GetNpcVendorItemList(vendor->GetEntry());
        if (!items)
            continue;

        float const discount = bot->GetReputationPriceDiscount(vendor);
        for (uint32 i = 0; i < items->GetItemCount(); ++i)
        {
            VendorItem const* item = items->GetItem(i);
            if (!item || !item->item || item->ExtendedCost)
                continue;

            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item->item);
            if (!proto || !proto->BuyPrice)
                continue;

            VendorOffer offer;
            offer.guid = guid;
            offer.price = uint32(std::floor(float(proto->BuyPrice) * discount));
            offer.buyCount = std::max<uint32>(1, proto->BuyCount);
            offer.stock = item->maxcount ? vendor->GetVendorItemCurrentCount(item) : std::numeric_limits<uint32>::max();
            snapshot.vendorItems[item->item].push_back(offer);
        }
    }
}

bool IsConversionFailure(NewRpgInfo const& info, uint32 spellId, uint32 inputItemId, uint32 expectedItemId)
{
    return info.failedAt && info.failedSpell == spellId && info.failedInput == inputItemId &&
           info.failedItem == expectedItemId && GetMSTimeDiffToNow(info.failedAt) < CONVERSION_FAILURE_MEMORY_MS;
}

bool IsVendorFailure(NewRpgInfo const& info, uint32 itemId)
{
    return info.failedVendorAt && info.failedVendorItem == itemId &&
           GetMSTimeDiffToNow(info.failedVendorAt) < VENDOR_FAILURE_MEMORY_MS;
}

bool IsRecipeFailure(NewRpgInfo const& info, uint32 spellId)
{
    auto const it = info.failedRecipes.find(spellId);
    return it != info.failedRecipes.end() && GetMSTimeDiffToNow(it->second) < RECIPE_FAILURE_MEMORY_MS;
}

void SeedPlanItem(CraftPlanContext& ctx, uint32 itemId)
{
    if (!ctx.plan.seeded.insert(itemId).second)
        return;

    if (auto const it = ctx.bags->counts.find(itemId); it != ctx.bags->counts.end())
        ctx.plan.available[itemId] += it->second;
    if (auto const it = ctx.bags->stacks.find(itemId); it != ctx.bags->stacks.end())
        ctx.plan.stacks[itemId] = it->second;
}

bool IsDisenchantableItem(Player* bot, ItemTemplate const* proto, ItemUsage usage)
{
    if (!proto || !proto->DisenchantID)
        return false;
    if (proto->Class != ITEM_CLASS_ARMOR && proto->Class != ITEM_CLASS_WEAPON)
        return false;
    if (proto->Quality < ITEM_QUALITY_UNCOMMON || proto->Quality > ITEM_QUALITY_EPIC)
        return false;
    if (!bot->HasSkill(SKILL_ENCHANTING) || bot->GetSkillValue(SKILL_ENCHANTING) < proto->RequiredDisenchantSkill)
        return false;
    if (usage == ITEM_USAGE_QUEST || usage == ITEM_USAGE_GUILD_TASK || usage == ITEM_USAGE_EQUIP ||
        usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_USE || usage == ITEM_USAGE_SKILL || usage == ITEM_USAGE_KEEP)
        return false;

    // Off-class greens are NONE (BoE ones may be AH/VENDOR); those are safe to
    // destroy for materials. BAD_EQUIP is deliberately excluded: it marks a
    // stopgap upgrade the equip path may still wear.
    return usage == ITEM_USAGE_DISENCHANT || usage == ITEM_USAGE_NONE || usage == ITEM_USAGE_VENDOR ||
           usage == ITEM_USAGE_AH;
}

bool EnsureRecipeIngredients(CraftPlanContext& ctx, ProfessionRecipe const& recipe, uint8 depth);

bool PlanVendorPurchase(CraftPlanContext& ctx, uint32 itemId, uint32 missing)
{
    if (!missing)
        return false;

    auto const vendor = ctx.bags->vendorItems.find(itemId);
    if (vendor == ctx.bags->vendorItems.end() || IsVendorFailure(*ctx.info, itemId) ||
        ctx.plan.steps.size() >= MAX_PLAN_STEPS)
        return false;

    for (VendorOffer const& offer : vendor->second)
    {
        if (offer.stock < missing)
            continue;

        uint32 const buyCount = std::max<uint32>(1, offer.buyCount);
        uint32 const purchases = (missing + buyCount - 1) / buyCount;
        uint64 const cost = uint64(offer.price) * purchases;
        if (cost > ctx.plan.moneyBudget)
            continue;

        ctx.plan.moneyBudget -= uint32(cost);
        ctx.plan.steps.push_back({0, 0, 0, missing, itemId, offer.guid});
        LOG_DEBUG("playerbots", "[New RPG] {} plans vendor purchase {} x{} at {} copper each", ctx.bot->GetName(),
                  itemId, missing, offer.price);
        return true;
    }

    return false;
}

bool EnsureItem(CraftPlanContext& ctx, uint32 itemId, uint32 count, uint8 depth)
{
    if (!itemId || count == 0)
        return false;

    SeedPlanItem(ctx, itemId);
    uint32& available = ctx.plan.available[itemId];
    uint32 const fromBag = std::min(available, count);
    available -= fromBag;
    count -= fromBag;
    if (count == 0)
        return true;

    if (depth >= MAX_CRAFT_DEPTH || ctx.plan.visiting.count(itemId) || ctx.plan.steps.size() >= MAX_PLAN_STEPS)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} cannot plan item {} x{}: depth {} visiting {} steps {}",
                  ctx.bot->GetName(), itemId, count, depth, ctx.plan.visiting.count(itemId), ctx.plan.steps.size());
        return false;
    }

    ctx.plan.visiting.insert(itemId);

    uint32 remaining = count;
    if (auto const index = ctx.craftIndex->find(itemId); index != ctx.craftIndex->end())
    {
        while (remaining > 0 && ctx.plan.steps.size() < MAX_PLAN_STEPS)
        {
            bool crafted = false;
            for (size_t recipeIndex : index->second)
            {
                ProfessionRecipe const& recipe = (*ctx.recipes)[recipeIndex];
                if (!recipe.outputCount)
                    continue;
                if (recipe.hasCooldown && ctx.plan.plannedSpells.count(recipe.spellId))
                    continue;

                CraftPlanContext trial = ctx;
                if (!EnsureRecipeIngredients(trial, recipe, depth + 1))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} cannot plan item {} x{}: recipe {} ingredients failed",
                              ctx.bot->GetName(), itemId, remaining, recipe.spellId);
                    continue;
                }

                trial.plan.steps.push_back({recipe.spellId, recipe.spellFocus, 0, 0, 0});
                trial.plan.plannedSpells.insert(recipe.spellId);
                trial.plan.available[itemId] += recipe.outputCount;
                uint32 const consumed = std::min(recipe.outputCount, remaining);
                trial.plan.available[itemId] -= consumed;
                remaining -= consumed;
                ctx = std::move(trial);
                crafted = true;
                break;
            }
            if (!crafted)
                break;
        }

        if (remaining == 0)
        {
            ctx.plan.visiting.erase(itemId);
            return true;
        }
    }

    float produced = 0.0f;
    bool plannedConversion = false;
    bool probabilistic = false;
    bool lowYieldPlanned = false;
    for (ConversionCandidate const& conversion : ctx.bags->conversions)
    {
        if (produced >= float(remaining) || ctx.plan.steps.size() >= MAX_PLAN_STEPS)
            break;
        if (conversion.inputItemId == itemId)
            continue;

        auto const output = conversion.outputs.find(itemId);
        if (output == conversion.outputs.end() || output->second.expectedCount <= 0.0f)
            continue;
        if (IsConversionFailure(*ctx.info, conversion.spellId, conversion.inputItemId, itemId))
            continue;

        // Milling, Prospecting and Disenchanting target one stack and need the
        // whole input count in that stack. Consume from the largest stack and
        // promote the next one once it drops below the required count.
        SeedPlanItem(ctx, conversion.inputItemId);
        uint32& inputAvailable = ctx.plan.available[conversion.inputItemId];
        auto& stacks = ctx.plan.stacks[conversion.inputItemId];
        float const perCast = output->second.expectedCount;
        // Very low-yield outputs get one gamble per plan; high-yield ones may
        // use the remaining step budget.
        bool const lowYield = perCast < MIN_CONVERSION_EXPECTED;
        if (lowYield && lowYieldPlanned)
            continue;

        uint32 const maxCasts = lowYield ? 1 : uint32(MAX_PLAN_STEPS);
        uint32 const needed = std::min(maxCasts, std::max<uint32>(1, uint32(std::ceil(float(remaining) / perCast))));

        for (uint32 casts = 0; casts < needed && produced < float(remaining) && ctx.plan.steps.size() < MAX_PLAN_STEPS;
             ++casts)
        {
            auto stack = std::max_element(stacks.begin(), stacks.end());
            if (stack == stacks.end() || *stack < conversion.inputCount || inputAvailable < conversion.inputCount)
                break;

            *stack -= conversion.inputCount;
            inputAvailable -= conversion.inputCount;
            ctx.plan.steps.push_back({conversion.spellId, 0, conversion.inputItemId, conversion.inputCount, itemId});
            produced += perCast;
            plannedConversion = true;
            if (output->second.minCount == 0)
                probabilistic = true;
            if (lowYield)
            {
                lowYieldPlanned = true;
                break;
            }
        }
    }

    ctx.plan.visiting.erase(itemId);
    if (produced >= float(remaining))
        return true;

    uint32 const missing = remaining - uint32(std::min(produced, float(remaining)));
    if (PlanVendorPurchase(ctx, itemId, missing))
        return true;

    // A chance-based conversion may not yield the exact item. Only a direct
    // reagent of the final recipe may gamble; a nested craft that cannot be
    // covered would otherwise discard the whole plan after consuming inputs.
    if (probabilistic && plannedConversion && depth == 0)
        return true;

    LOG_DEBUG("playerbots", "[New RPG] {} cannot plan item {} x{}: no craft, conversion or vendor (produced {})",
              ctx.bot->GetName(), itemId, missing, produced);
    return false;
}

bool EnsureRecipeIngredients(CraftPlanContext& ctx, ProfessionRecipe const& recipe, uint8 depth)
{
    for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
        if (recipe.reagentCounts[r] && !EnsureItem(ctx, recipe.reagents[r], recipe.reagentCounts[r], depth))
            return false;

    return true;
}

bool PlanRecipe(Player* bot, NewRpgInfo const& info, ProfessionRecipe const& recipe,
                std::vector<ProfessionRecipe> const& recipes,
                std::unordered_map<uint32, std::vector<size_t>> const& craftIndex, BagSnapshot const& bags,
                uint32 moneyBudget, std::vector<NewRpgInfo::DoProfessions::CraftStep>& steps)
{
    CraftPlanContext ctx;
    ctx.bot = bot;
    ctx.info = &info;
    ctx.recipes = &recipes;
    ctx.craftIndex = &craftIndex;
    ctx.bags = &bags;
    ctx.plan.moneyBudget = moneyBudget;

    // The recipe's own output is never a valid input for itself.
    if (recipe.outputItemId)
        ctx.plan.visiting.insert(recipe.outputItemId);

    for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
    {
        if (!recipe.reagentCounts[r])
            continue;
        if (!EnsureItem(ctx, recipe.reagents[r], recipe.reagentCounts[r], 0))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} plan recipe {} missing reagent {} x{}", bot->GetName(),
                      recipe.spellId, recipe.reagents[r], recipe.reagentCounts[r]);
            return false;
        }
    }

    if (ctx.plan.steps.empty())
        return false;

    steps = std::move(ctx.plan.steps);
    return true;
}
}  // namespace

void NewRpgDoProfessionsAction::FinishProfession(NewRpgInfo& info, NewRpgInfo::DoProfessions& data,
                                                 ProfessionEndReason reason)
{
    if (data.started)
    {
        uint16 const currentSkill = bot->GetPureSkillValue(data.skillLine);
        if (currentSkill > data.startSkill)
            botAI->RecordProfessionSkillGain(data.skillLine, currentSkill - data.startSkill);
        botAI->RecordProfessionEnd(data.skillLine, reason);
    }

    if (!data.pendingSkillLines.empty())
    {
        uint32 const next = data.pendingSkillLines.front();
        std::vector<uint32> pending(data.pendingSkillLines.begin() + 1, data.pendingSkillLines.end());
        LOG_DEBUG("playerbots", "[New RPG] {} professions switching from {} to {} ({} remaining)", bot->GetName(),
                  data.skillLine, next, pending.size());
        info.ChangeToDoProfessions(next, std::move(pending));
        return;
    }

    info.ChangeToIdle();
}

bool NewRpgDoProfessionsAction::LearnFromTrainer(NewRpgInfo::DoProfessions& data)
{
    Creature* trainer = nullptr;
    float bestTrainerDistance = std::numeric_limits<float>::max();
    uint16 const currentSkill = bot->GetBaseSkillValue(data.skillLine);
    for (ObjectGuid const guid : AI_VALUE(GuidVector, "possible new rpg targets"))
    {
        Creature* creature = ObjectAccessor::GetCreature(*bot, guid);
        if (!creature || !creature->IsInWorld() || !creature->IsTrainer())
            continue;

        Trainer::Trainer const* trainerData = sObjectMgr->GetTrainer(creature->GetEntry());
        if (!trainerData || trainerData->GetTrainerType() != Trainer::Type::Tradeskill ||
            !trainerData->IsTrainerValidForPlayer(bot))
            continue;

        bool teachesProfession = false;
        for (Trainer::Spell const& trainerSpell : trainerData->GetSpells())
        {
            if (trainerSpell.ReqSkillLine == data.skillLine)
            {
                teachesProfession = true;
                break;
            }
        }
        if (!teachesProfession)
            continue;

        bool teachesSkill = false;
        bool teachesNewSkill = false;
        for (Trainer::Spell const& trainerSpell : trainerData->GetSpells())
        {
            if (trainerSpell.ReqSkillLine != data.skillLine && trainerSpell.ReqSkillLine != 0)
                continue;
            if (!trainerData->CanTeachSpell(bot, &trainerSpell))
                continue;

            teachesSkill = true;
            if (trainerSpell.ReqSkillRank > data.lastTrainerSkill)
                teachesNewSkill = true;
        }
        if (!teachesSkill)
            continue;

        // A skill-up can unlock a new rank before the retry window expires. Only the
        // unchanged-skill case is rate-limited, so unaffordable spells do not cause a
        // trainer visit on every tick.
        bool const recentlyVisited = data.trainerGuid == creature->GetGUID() && data.lastTrainer && !teachesNewSkill &&
                                     GetMSTimeDiffToNow(data.lastTrainer) < 5 * 60 * IN_MILLISECONDS;
        if (recentlyVisited)
            continue;

        float const distance = bot->GetDistance(creature);
        if (distance < bestTrainerDistance)
        {
            bestTrainerDistance = distance;
            trainer = creature;
        }
    }

    if (!trainer)
        return false;

    if (bot->GetDistance(trainer) > INTERACTION_DISTANCE)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} professions moving to trainer {} (distance {})", bot->GetName(),
                  trainer->GetEntry(), bot->GetDistance(trainer));
        MoveWorldObjectTo(trainer->GetGUID(), 2.0f);
        return true;
    }

    LOG_DEBUG("playerbots", "[New RPG] {} professions learning from trainer {} (skill {})", bot->GetName(),
              trainer->GetEntry(), data.skillLine);
    data.trainerGuid = trainer->GetGUID();
    data.lastTrainer = getMSTime();
    data.lastTrainerSkill = currentSkill;
    bot->SetSelection(trainer->GetGUID());
    size_t const spellsBefore = bot->GetSpellMap().size();
    botAI->DoSpecificAction("trainer", Event("trainer", "learn"), true);
    size_t const spellsAfter = bot->GetSpellMap().size();
    botAI->RecordProfessionTrainerVisit(
        data.skillLine, spellsAfter > spellsBefore ? uint32(spellsAfter - spellsBefore) : 0u);
    return true;
}

bool NewRpgDoProfessionsAction::HandleLootWait(NewRpgInfo::DoProfessions& data)
{
    if (!data.lootWaitStart)
        return false;

    uint32 const elapsed = GetMSTimeDiffToNow(data.lootWaitStart);
    if (bot->GetLootGUID())
    {
        if (elapsed >= CONVERSION_LOOT_TIMEOUT_MS)
            botAI->DoSpecificAction("release loot", Event(), true);

        // A lost release response must not freeze the whole profession status.
        if (elapsed < CONVERSION_LOOT_HARD_TIMEOUT_MS)
            return true;

        LOG_DEBUG("playerbots", "[New RPG] {} loot window still open after {} ms; continuing", bot->GetName(), elapsed);
    }

    bool const success = bot->GetItemCount(data.lootWaitItem, false) > data.lootWaitCount;
    if (!success && elapsed < CONVERSION_GRACE_MS)
        return true;

    if (!success)
    {
        botAI->rpgInfo.failedSpell = data.lootWaitSpell;
        botAI->rpgInfo.failedInput = data.lootWaitInput;
        botAI->rpgInfo.failedItem = data.lootWaitItem;
        botAI->rpgInfo.failedAt = getMSTime();
        LOG_DEBUG("playerbots", "[New RPG] {} professions conversion {} on item {} did not yield item {}",
                  bot->GetName(), data.lootWaitSpell, data.lootWaitInput, data.lootWaitItem);
    }
    else if (botAI->rpgInfo.failedSpell == data.lootWaitSpell && botAI->rpgInfo.failedInput == data.lootWaitInput &&
             botAI->rpgInfo.failedItem == data.lootWaitItem)
    {
        botAI->rpgInfo.failedSpell = 0;
        botAI->rpgInfo.failedInput = 0;
        botAI->rpgInfo.failedItem = 0;
        botAI->rpgInfo.failedAt = 0;
    }

    data.lootWaitStart = 0;
    data.lootWaitSpell = 0;
    data.lootWaitInput = 0;
    data.lootWaitItem = 0;
    data.lootWaitCount = 0;
    return true;
}

bool NewRpgDoProfessionsAction::RunPendingCrafts(NewRpgInfo::DoProfessions& data)
{
    NewRpgInfo::DoProfessions::CraftStep const step = data.pendingCrafts.front();

    LOG_TRACE("playerbots", "[New RPG] {} pending step {}/{} spell {} focus {} input {}x{} expect {}", bot->GetName(),
              1, data.pendingCrafts.size(), step.spellId, step.spellFocus, step.inputItemId, step.inputCount,
              step.expectedItemId);

    auto failVendor = [this, &data, step](char const* reason)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} vendor purchase of {} x{} failed: {}", bot->GetName(),
                  step.expectedItemId, step.inputCount, reason);
        botAI->rpgInfo.failedVendorItem = step.expectedItemId;
        botAI->rpgInfo.failedVendorAt = getMSTime();
        data.pendingCrafts.clear();
        return false;
    };

    if (!step.spellId)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(step.expectedItemId);
        if (!proto)
            return failVendor("unknown item");

        Creature* vendor = step.vendorGuid.IsEmpty() ? nullptr : ObjectAccessor::GetCreature(*bot, step.vendorGuid);
        if (!vendor || !vendor->IsInWorld() || !vendor->IsVendor() ||
            FindVendorItemSlot(vendor, step.expectedItemId) < 0)
        {
            vendor = nullptr;
            GuidVector const vendors = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
            for (ObjectGuid const guid : vendors)
            {
                Creature* candidate = botAI->GetCreature(guid);
                if (candidate && candidate->IsInWorld() && candidate->IsVendor() &&
                    bot->GetDistance(candidate) <= 100.0f && FindVendorItemSlot(candidate, step.expectedItemId) >= 0)
                {
                    vendor = candidate;
                    break;
                }
            }
        }

        if (!vendor)
            return failVendor("no nearby vendor sells it");

        data.pendingCrafts.front().vendorGuid = vendor->GetGUID();
        if (bot->GetDistance(vendor) > INTERACTION_DISTANCE)
        {
            MoveWorldObjectTo(vendor->GetGUID(), 2.0f);
            return true;
        }

        int32 const slot = FindVendorItemSlot(vendor, step.expectedItemId);
        VendorItemData const* items = vendor->GetVendorItems();
        VendorItem const* vendorItem = items && slot >= 0 ? items->GetItem(slot) : nullptr;
        if (!vendorItem)
            return failVendor("vendor slot missing");

        // Clear any stale list-inventory entry so the core uses the vendor we pass.
        bot->GetSession()->SetCurrentVendor(0);

        uint32 const buyCount = std::max<uint32>(1, proto->BuyCount);
        uint32 const unitPrice = uint32(std::floor(float(proto->BuyPrice) * bot->GetReputationPriceDiscount(vendor)));
        uint32 purchased = 0;
        while (purchased < step.inputCount)
        {
            uint32 const budget = AI_VALUE2(uint32, "free money for", uint32(NeedMoneyFor::tradeskill));
            if (unitPrice > budget)
                return failVendor("tradeskill budget too low");

            if (vendorItem->maxcount && vendor->GetVendorItemCurrentCount(vendorItem) < buyCount)
                return failVendor("vendor out of stock");

            uint32 const before = bot->GetItemCount(step.expectedItemId, false);
            // The core returns false for unlimited-stock items even when the
            // purchase succeeds, so judge success by the item count.
            bot->BuyItemFromVendorSlot(vendor->GetGUID(), uint32(slot), step.expectedItemId, 1, NULL_BAG, NULL_SLOT);

            uint32 const after = bot->GetItemCount(step.expectedItemId, false);
            if (after <= before)
                return failVendor("core purchase rejected");
            purchased += after - before;
        }

        data.pendingCrafts.erase(data.pendingCrafts.begin());
        LOG_DEBUG("playerbots", "[New RPG] {} bought {} x{}", bot->GetName(), step.expectedItemId, purchased);
        botAI->RecordProfessionVendorPurchase(data.skillLine, purchased);
        return true;
    }

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(step.spellId);
    if (!spellInfo || !bot->HasSpell(step.spellId))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} clearing pending: spell {} missing", bot->GetName(), step.spellId);
        data.pendingCrafts.clear();
        return false;
    }

    // A dependency cast can briefly be on cooldown; wait instead of throwing the plan away.
    if (bot->HasSpellCooldown(step.spellId))
        return true;

    for (uint8 t = 0; t < 2; ++t)
    {
        if (spellInfo->TotemCategory[t] && !bot->HasItemTotemCategory(spellInfo->TotemCategory[t]))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} clearing pending: missing tool category {}", bot->GetName(),
                      spellInfo->TotemCategory[t]);
            data.pendingCrafts.clear();
            return false;
        }
    }

    Item* inputItem = nullptr;
    if (step.inputItemId)
    {
        inputItem = FindLargestBagItemByEntry(bot, step.inputItemId);
        if (!inputItem || inputItem->GetCount() < step.inputCount)
        {
            LOG_DEBUG("playerbots", "[New RPG] {} clearing pending: missing input item {} x{}", bot->GetName(),
                      step.inputItemId, step.inputCount);
            data.pendingCrafts.clear();
            return false;
        }

        ItemTemplate const* proto = inputItem->GetTemplate();
        if (!proto)
        {
            data.pendingCrafts.clear();
            return false;
        }

        if (step.spellId == MILLING_SPELL && proto->RequiredSkillRank > bot->GetSkillValue(SKILL_INSCRIPTION))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} clearing pending: inscription skill too low to mill {}",
                      bot->GetName(), step.inputItemId);
            data.pendingCrafts.clear();
            return false;
        }
        if (step.spellId == PROSPECTING_SPELL && proto->RequiredSkillRank > bot->GetSkillValue(SKILL_JEWELCRAFTING))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} clearing pending: jewelcrafting skill too low to prospect {}",
                      bot->GetName(), step.inputItemId);
            data.pendingCrafts.clear();
            return false;
        }

        if (step.spellId == DISENCHANT_SPELL)
        {
            ItemUsage const usage = AI_VALUE2(ItemUsage, "item usage", step.inputItemId);
            if (!IsDisenchantableItem(bot, proto, usage))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} clearing pending: input item {} no longer disenchantable",
                          bot->GetName(), step.inputItemId);
                data.pendingCrafts.clear();
                return false;
            }
        }
    }
    else
    {
        for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
        {
            if (spellInfo->ReagentCount[r] &&
                !bot->HasItemCount(spellInfo->Reagent[r], spellInfo->ReagentCount[r], false))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} clearing pending: missing reagent {} x{}", bot->GetName(),
                          spellInfo->Reagent[r], spellInfo->ReagentCount[r]);
                data.pendingCrafts.clear();
                return false;
            }
        }
    }

    switch (EnsureSpellFocus(data, step.spellFocus))
    {
        case SpellFocusState::Ready:
            break;
        case SpellFocusState::Waiting:
            return true;
        case SpellFocusState::Unavailable:
            data.pendingCrafts.clear();
            return false;
    }

    if (bot->isMoving())
        bot->StopMoving();
    if (!bot->IsStandState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    data.spellId = step.spellId;
    data.lastCast = getMSTime();
    ++data.casts;

    // Sample before the cast: a fast loot store can add the item during CastSpell.
    uint32 const expectedBefore = step.inputItemId ? bot->GetItemCount(step.expectedItemId, false) : 0;
    bool const cast =
        step.inputItemId ? botAI->CastSpell(step.spellId, bot, inputItem) : botAI->CastSpell(step.spellId, bot);
    botAI->RecordProfessionCast(data.skillLine, step.spellId, cast);
    if (!cast)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} professions dependency cast failed for recipe {}", bot->GetName(),
                  step.spellId);
        data.pendingCrafts.clear();
        botAI->SetNextCheckDelay(1000);
        return false;
    }

    data.pendingCrafts.erase(data.pendingCrafts.begin());
    if (step.inputItemId)
    {
        data.lootWaitStart = getMSTime();
        data.lootWaitSpell = step.spellId;
        data.lootWaitInput = step.inputItemId;
        data.lootWaitItem = step.expectedItemId;
        data.lootWaitCount = expectedBefore;
        LOG_DEBUG("playerbots", "[New RPG] {} professions conversion {} on item {} for item {}", bot->GetName(),
                  step.spellId, step.inputItemId, step.expectedItemId);
    }
    else
    {
        LOG_DEBUG("playerbots", "[New RPG] {} professions dependency recipe {} ({})", bot->GetName(), step.spellId,
                  data.skillLine);
    }

    return true;
}

NewRpgDoProfessionsAction::SpellFocusState NewRpgDoProfessionsAction::EnsureSpellFocus(NewRpgInfo::DoProfessions& data,
                                                                                       uint32 spellFocus)
{
    if (!spellFocus)
        return SpellFocusState::Ready;

    Value<ObjectGuid>* focusValue = context->GetValue<ObjectGuid>("spell focus", spellFocus);
    if (!focusValue)
    {
        LOG_ERROR("playerbots", "[New RPG] {} missing 'spell focus' value", bot->GetName());
        return SpellFocusState::Unavailable;
    }

    ObjectGuid const focusGuid = focusValue->Get();
    WorldObject* worldObject = focusGuid.IsEmpty() ? nullptr : botAI->GetWorldObject(focusGuid);
    GameObject* focus = worldObject ? worldObject->ToGameObject() : nullptr;
    SpellFocusSpawn const* cached = (!focus && !focusGuid.IsEmpty()) ? sSpellFocusMgr.Get(focusGuid) : nullptr;

    if (!focus && !cached)
    {
        if (TryBasicCampfire(botAI, bot, data, spellFocus))
            return SpellFocusState::Waiting;
        if (data.lastCampfire && GetMSTimeDiffToNow(data.lastCampfire) < CAMPFIRE_WAIT_MS)
            return SpellFocusState::Waiting;

        LOG_DEBUG("playerbots", "[New RPG] {} no spell focus {} for professions", bot->GetName(), spellFocus);
        return SpellFocusState::Unavailable;
    }

    float distance = 0.0f;
    float focusDistance = 0.0f;
    if (focus)
    {
        distance = bot->GetDistance(focus);
        focusDistance = std::max(0.5f, float(focus->GetGOInfo()->spellFocus.dist) - 1.0f);
    }
    else
    {
        float const dx = cached->pos.GetPositionX() - bot->GetPositionX();
        float const dy = cached->pos.GetPositionY() - bot->GetPositionY();
        float const dz = cached->pos.GetPositionZ() - bot->GetPositionZ();
        distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        focusDistance = std::max(0.5f, cached->distance - 1.0f);
    }

    if (!focus || distance > focusDistance)
    {
        // A campfire is better than walking more than 100 yards to an existing focus.
        if (distance > 100.0f && TryBasicCampfire(botAI, bot, data, spellFocus))
            return SpellFocusState::Waiting;
        if (data.lastCampfire && GetMSTimeDiffToNow(data.lastCampfire) < CAMPFIRE_WAIT_MS)
            return SpellFocusState::Waiting;

        LOG_DEBUG("playerbots", "[New RPG] {} professions moving to spell focus {} (distance {})", bot->GetName(),
                  focusGuid.ToString(), distance);
        context->GetValue<ObjectGuid>("spell focus target")->Set(focusGuid);
        botAI->DoSpecificAction("move to spell focus", Event("spell focus"), true);
        return SpellFocusState::Waiting;
    }

    return SpellFocusState::Ready;
}

bool NewRpgDoProfessionsAction::Execute(Event /*event*/)
{
    NewRpgInfo& info = botAI->rpgInfo;
    LOG_DEBUG("playerbots", "[New RPG] {} do professions execute (status {})", bot->GetName(),
              uint32(info.GetStatus()));
    auto* dataPtr = std::get_if<NewRpgInfo::DoProfessions>(&info.data);
    if (!dataPtr)
        return false;
    auto& data = *dataPtr;

    if (!bot->IsAlive() || bot->IsInCombat())
        return false;

    if (!data.skillLine || !botAI->HasSkill(SkillType(data.skillLine)))
    {
        FinishProfession(info, data, ProfessionEndReason::SkillLost);
        return true;
    }

    if (!data.started)
    {
        data.started = true;
        data.startSkill = bot->GetPureSkillValue(data.skillLine);
        botAI->RecordProfessionSession(data.skillLine);
    }

    // Let the current cast finish before selecting the next recipe.
    if (bot->HasUnitState(UNIT_STATE_CASTING) || bot->IsNonMeleeSpellCast(false))
        return true;

    if (bot->IsInFlight())
    {
        LOG_DEBUG("playerbots", "[New RPG] {} professions waiting: in flight", bot->GetName());
        return true;
    }
    if (bot->IsMounted())
        bot->Dismount();

    if (HandleLootWait(data))
        return true;

    if (!data.pendingCrafts.empty())
    {
        if (!RunPendingCrafts(data))
            FinishProfession(info, data, ProfessionEndReason::CraftFailed);
        return true;
    }

    // Recipe items picked up later are handled by the item-push trigger; this
    // covers items already in the bags when the status starts.
    if (data.casts == 0 && botAI->DoSpecificAction("learn recipe items", Event(), true))
        return true;
    if (LearnFromTrainer(data))
        return true;

    uint8 const bagSpace = AI_VALUE(uint8, "bag space");
    uint32 const skillValue = bot->GetPureSkillValue(data.skillLine);
    bool const skillMaxed = skillValue >= bot->GetMaxSkillValue(data.skillLine);

    std::vector<ProfessionRecipe> allRecipes;
    std::unordered_map<uint32, std::vector<size_t>> craftIndex;
    std::vector<ProfessionRecipe> directUseful;
    std::vector<ProfessionRecipe> directSkillUp;
    std::vector<ProfessionRecipe> plannedUseful;
    std::vector<ProfessionRecipe> plannedSkillUp;

    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            continue;

        uint32 outputItemId = 0;
        uint32 outputCount = 1;
        uint32 scrollItemId = 0;
        bool noItemSpell = false;
        for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
        {
            if (spellInfo->Effects[e].Effect == SPELL_EFFECT_CREATE_ITEM ||
                spellInfo->Effects[e].Effect == SPELL_EFFECT_CREATE_ITEM_2)
            {
                outputItemId = spellInfo->Effects[e].ItemType;
                if (outputItemId)
                {
                    outputCount = uint32(std::max(1, spellInfo->Effects[e].BasePoints + 1));
                    break;
                }
                continue;
            }
            if (spellInfo->Effects[e].Effect == SPELL_EFFECT_ENCHANT_ITEM ||
                spellInfo->Effects[e].Effect == SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY ||
                spellInfo->Effects[e].Effect == SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC)
            {
                noItemSpell = true;
                scrollItemId = spellInfo->Effects[e].ItemType;
                break;
            }
        }
        if (!outputItemId && !noItemSpell)
            continue;

        // CREATE_ITEM_2 loot crafts replace the listed item with random loot; they are not
        // deterministic intermediates.
        if (outputItemId && spellInfo->IsLootCrafting())
            continue;

        int32 chance = 0;
        uint32 tier = 0;
        bool matchedSkillLine = false;
        bool professionSpell = false;
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineAbilityEntry const* skill = itr->second;
            if (skill->SkillLine == data.skillLine)
            {
                matchedSkillLine = true;
                chance =
                    std::max(chance, ItemUsageValue::CraftSkillGainChance(skillValue, skill->TrivialSkillLineRankHigh,
                                                                          skill->TrivialSkillLineRankLow));
                tier = std::max(tier, skill->TrivialSkillLineRankLow);
            }
            if (IsProfessionSkill(skill->SkillLine))
                professionSpell = true;
        }
        if (!matchedSkillLine && !professionSpell)
            continue;

        bool hasAnyReagent = false;
        bool hasReagents = true;
        std::array<uint32, MAX_SPELL_REAGENTS> reagents{};
        std::array<uint8, MAX_SPELL_REAGENTS> reagentCounts{};
        for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
        {
            if (spellInfo->ReagentCount[r] == 0)
                continue;

            hasAnyReagent = true;
            reagents[r] = uint32(spellInfo->Reagent[r]);
            reagentCounts[r] = uint8(spellInfo->ReagentCount[r]);
            hasReagents = hasReagents && bot->HasItemCount(spellInfo->Reagent[r], spellInfo->ReagentCount[r], false);
        }
        if (!hasAnyReagent || bot->HasSpellCooldown(spellId))
            continue;

        bool hasTools = true;
        for (uint8 t = 0; t < 2; ++t)
        {
            if (spellInfo->TotemCategory[t] && !bot->HasItemTotemCategory(spellInfo->TotemCategory[t]))
            {
                hasTools = false;
                break;
            }
        }
        if (!hasTools)
            continue;

        bool useful = false;
        bool marketable = false;
        if (matchedSkillLine)
        {
            if (outputItemId)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(outputItemId);
                if (!proto)
                    continue;

                ItemUsage const usage = AI_VALUE2(ItemUsage, "item usage", outputItemId);
                // Reagents are not final goods: treating ITEM_USAGE_SKILL as useful
                // lets a direct intermediate recipe outrank the finished item that
                // needs it, so the bot crafts bolts forever instead of the bag.
                useful = usage == ITEM_USAGE_USE || usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE ||
                         usage == ITEM_USAGE_QUEST;
            }
            else if (scrollItemId)
            {
                // Vellums produce a tradeable scroll and still grant skill. Until
                // AH pricing exists, treat every scroll as a potential sale and
                // prefer vellums; AH valuation can gate this per enchant later.
                marketable = true;
            }
        }

        // At max skill there is no skill-up to gain; useful outputs (including
        // marketable enchant scrolls) are still crafted. Gray recipes stay in the
        // graph because they can still be intermediates for another recipe.
        bool const worthwhile = useful || marketable;

        ProfessionRecipe recipe;
        recipe.spellId = spellId;
        recipe.outputItemId = outputItemId;
        recipe.outputCount = outputCount;
        recipe.chance = chance;
        recipe.tier = tier;
        recipe.spellFocus = spellInfo->RequiresSpellFocus;
        recipe.useful = useful;
        recipe.noItemSpell = noItemSpell;
        recipe.marketable = marketable;
        recipe.hasCooldown = spellInfo->RecoveryTime || spellInfo->CategoryRecoveryTime;
        recipe.reagents = reagents;
        recipe.reagentCounts = reagentCounts;

        size_t const recipeIndex = allRecipes.size();
        allRecipes.push_back(recipe);
        if (outputItemId)
            craftIndex[outputItemId].push_back(recipeIndex);

        // A recipe whose cast just failed is not retried immediately. Keep it in
        // the graph as a possible intermediate, but not as the next final cast.
        if (IsRecipeFailure(info, spellId))
            continue;

        if (!matchedSkillLine || (!worthwhile && (chance <= 0 || skillMaxed)))
            continue;

        if (hasReagents)
            (worthwhile ? directUseful : directSkillUp).push_back(recipe);
        else
            (worthwhile ? plannedUseful : plannedSkillUp).push_back(recipe);
    }

    auto pickBest = [](std::vector<ProfessionRecipe>& recipes) -> ProfessionRecipe*
    {
        ProfessionRecipe* best = nullptr;
        for (ProfessionRecipe& recipe : recipes)
        {
            if (!best || recipe.chance > best->chance || (recipe.chance == best->chance && recipe.tier > best->tier))
                best = &recipe;
        }
        return best;
    };

    ProfessionRecipe* chosen = pickBest(directUseful);
    if (!chosen)
        chosen = pickBest(directSkillUp);

    bool planNeeded = false;
    if (!chosen)
    {
        auto sortByPriority = [](std::vector<ProfessionRecipe>& recipes)
        {
            std::sort(recipes.begin(), recipes.end(),
                      [](ProfessionRecipe const& left, ProfessionRecipe const& right)
                      {
                          if (left.chance != right.chance)
                              return left.chance > right.chance;
                          return left.tier > right.tier;
                      });
        };
        sortByPriority(plannedUseful);
        sortByPriority(plannedSkillUp);

        if (plannedUseful.empty() && plannedSkillUp.empty())
        {
            LOG_DEBUG("playerbots", "[New RPG] {} no professions recipe with skill-up chance and materials (skill {})",
                      bot->GetName(), data.skillLine);
            FinishProfession(info, data, ProfessionEndReason::NoMaterials);
            return true;
        }

        BagSnapshot bags;
        BuildBagSnapshot(botAI, bags);
        uint32 const tradeskillBudget = AI_VALUE2(uint32, "free money for", uint32(NeedMoneyFor::tradeskill));
        auto tryPlan = [&](std::vector<ProfessionRecipe>& recipes) -> bool
        {
            for (ProfessionRecipe& recipe : recipes)
            {
                LOG_TRACE("playerbots", "[New RPG] {} plan candidate {} chance {} useful {}", bot->GetName(),
                          recipe.spellId, recipe.chance, recipe.useful);
                std::vector<NewRpgInfo::DoProfessions::CraftStep> steps;
                if (!PlanRecipe(bot, info, recipe, allRecipes, craftIndex, bags, tradeskillBudget, steps))
                    continue;

                chosen = &recipe;
                planNeeded = true;
                data.pendingCrafts = std::move(steps);
                LOG_DEBUG("playerbots", "[New RPG] {} professions planned {} steps for recipe {} (skill {})",
                          bot->GetName(), data.pendingCrafts.size(), recipe.spellId, data.skillLine);
                return true;
            }
            return false;
        };

        if (!tryPlan(plannedUseful) && !tryPlan(plannedSkillUp))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} no professions recipe with skill-up chance and materials (skill {})",
                      bot->GetName(), data.skillLine);
            FinishProfession(info, data, ProfessionEndReason::NoMaterials);
            return true;
        }
    }

    // Skill-up surplus respects the bag-space rule; actually useful outputs may go up to
    // their item-usage limit even when the bags are already fairly full.
    if (!chosen->useful && !chosen->noItemSpell && bagSpace > 80)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} stopping professions: bags {}% and recipe {} is surplus", bot->GetName(),
                  bagSpace, chosen->spellId);
        FinishProfession(info, data, ProfessionEndReason::BagFull);
        return true;
    }
    if (chosen->useful && bagSpace >= 100)
    {
        FinishProfession(info, data, ProfessionEndReason::BagFull);
        return true;
    }

    if (planNeeded)
        return true;

    switch (EnsureSpellFocus(data, chosen->spellFocus))
    {
        case SpellFocusState::Ready:
            break;
        case SpellFocusState::Waiting:
            return true;
        case SpellFocusState::Unavailable:
            FinishProfession(info, data, ProfessionEndReason::NoFocus);
            return true;
    }

    // A player stops and stands to craft; do both in the same tick so the sparse
    // random-bot schedule is not spent waiting for the next one.
    if (bot->isMoving())
        bot->StopMoving();
    if (!bot->IsStandState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    data.spellId = chosen->spellId;
    data.lastCast = getMSTime();
    ++data.casts;
    LOG_DEBUG("playerbots", "[New RPG] {} professions skill {} recipe {} chance {} useful {} tier {}", bot->GetName(),
              data.skillLine, chosen->spellId, chosen->chance, chosen->useful, chosen->tier);

    bool cast = false;
    if (chosen->noItemSpell)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(chosen->spellId);
        StatsWeightCalculator calculator(bot);
        if (spellInfo)
        {
            int8 const selfSlot = FindUsefulEnchantSlot(bot, spellInfo, calculator);
            if (selfSlot >= 0)
            {
                Item* equipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, selfSlot);
                if (equipped)
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} professions enchanting equipped slot {} item {} ({})",
                              bot->GetName(), selfSlot, equipped->GetEntry(), equipped->GetGUID().ToString());
                    cast = botAI->CastSpell(chosen->spellId, bot, equipped);
                }
            }
        }

        if (!cast)
        {
            // A genuine equipped-gear upgrade is still worth casting at max skill;
            // only marketable scroll production requires a vellum.
            Item* itemTarget = spellInfo ? FindEnchantTarget(bot, spellInfo, !skillMaxed, &calculator) : nullptr;
            if (!itemTarget)
            {
                LOG_DEBUG("playerbots", "[New RPG] {} professions no non-equipped item for recipe {}", bot->GetName(),
                          chosen->spellId);
                FinishProfession(info, data, ProfessionEndReason::NoTarget);
                return true;
            }
            LOG_DEBUG("playerbots", "[New RPG] {} professions enchanting item {} ({}) for recipe {}", bot->GetName(),
                      itemTarget->GetEntry(), itemTarget->GetGUID().ToString(), chosen->spellId);
            cast = botAI->CastSpell(chosen->spellId, bot, itemTarget);
        }
    }
    else
        cast = botAI->CastSpell(chosen->spellId, bot);

    botAI->RecordProfessionCast(data.skillLine, chosen->spellId, cast);

    if (cast)
    {
        info.failedRecipes.erase(chosen->spellId);
        return true;
    }

    for (auto it = info.failedRecipes.begin(); it != info.failedRecipes.end();)
    {
        if (GetMSTimeDiffToNow(it->second) >= RECIPE_FAILURE_MEMORY_MS)
            it = info.failedRecipes.erase(it);
        else
            ++it;
    }
    info.failedRecipes[chosen->spellId] = getMSTime();
    LOG_DEBUG("playerbots", "[New RPG] {} professions cast failed for recipe {} (skill {})", bot->GetName(),
              chosen->spellId, data.skillLine);
    botAI->SetNextCheckDelay(1000);
    return false;
}
