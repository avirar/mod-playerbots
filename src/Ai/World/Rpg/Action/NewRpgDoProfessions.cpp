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
#include <vector>

#include "Bag.h"
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
    std::unordered_set<uint32> outputs;
};

struct BagSnapshot
{
    std::unordered_map<uint32, uint32> counts;
    std::vector<ConversionCandidate> conversions;
};

struct CraftPlan
{
    std::unordered_map<uint32, uint32> available;
    std::unordered_set<uint32> seeded;
    std::unordered_set<uint32> visiting;
    std::unordered_set<uint32> plannedSpells;
    std::vector<NewRpgInfo::DoProfessions::CraftStep> steps;
};

struct CraftPlanContext
{
    NewRpgInfo::DoProfessions const* data = nullptr;
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
constexpr uint32 CONVERSION_GRACE_MS = 6 * IN_MILLISECONDS;
constexpr uint32 CONVERSION_FAILURE_MEMORY_MS = 30 * IN_MILLISECONDS;
constexpr uint8 MAX_CRAFT_DEPTH = 5;
constexpr size_t MAX_PLAN_STEPS = 20;

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

Item* FindBagItemByEntry(Player* bot, uint32 entry)
{
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (item->GetEntry() == entry)
                return item;

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bot->GetItemByPos(bagSlot, slot))
                    if (item->GetEntry() == entry)
                        return item;

    return nullptr;
}

void InspectBagItem(PlayerbotAI* botAI, Item* item, BagSnapshot& snapshot, std::unordered_set<uint64_t>& seen)
{
    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
        return;

    snapshot.counts[proto->ItemId] += item->GetCount();

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
        CollectLootTemplateItems(loot, candidate.outputs);
        if (!candidate.outputs.empty())
            snapshot.conversions.push_back(std::move(candidate));
    };

    Player* bot = botAI->GetBot();
    bool const millable = bot->HasSpell(MILLING_SPELL) && proto->HasFlag(ITEM_FLAG_IS_MILLABLE) &&
                          item->GetCount() >= CONVERSION_INPUT_COUNT;
    bool const prospectable = bot->HasSpell(PROSPECTING_SPELL) && proto->HasFlag(ITEM_FLAG_IS_PROSPECTABLE) &&
                              item->GetCount() >= CONVERSION_INPUT_COUNT;
    bool const disenchantable = bot->HasSpell(DISENCHANT_SPELL) && proto->DisenchantID;
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

    if (disenchantable && usage == ITEM_USAGE_DISENCHANT)
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
}

bool IsConversionFailure(NewRpgInfo::DoProfessions const& data, uint32 spellId, uint32 inputItemId,
                         uint32 expectedItemId)
{
    return data.failedAt && data.failedSpell == spellId && data.failedInput == inputItemId &&
           data.failedItem == expectedItemId && GetMSTimeDiffToNow(data.failedAt) < CONVERSION_FAILURE_MEMORY_MS;
}

bool EnsureRecipeIngredients(CraftPlanContext& ctx, ProfessionRecipe const& recipe, uint8 depth);

bool EnsureItem(CraftPlanContext& ctx, uint32 itemId, uint32 count, uint8 depth)
{
    if (!itemId || count == 0)
        return false;

    uint32& available = ctx.plan.available[itemId];
    if (ctx.plan.seeded.insert(itemId).second)
        if (auto const it = ctx.bags->counts.find(itemId); it != ctx.bags->counts.end())
            available += it->second;

    uint32 const fromBag = std::min(available, count);
    available -= fromBag;
    count -= fromBag;
    if (count == 0)
        return true;

    if (depth >= MAX_CRAFT_DEPTH || ctx.plan.visiting.count(itemId) || ctx.plan.steps.size() >= MAX_PLAN_STEPS)
        return false;

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
                    continue;

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

    uint32 produced = 0;
    for (ConversionCandidate const& conversion : ctx.bags->conversions)
    {
        if (produced >= remaining || ctx.plan.steps.size() >= MAX_PLAN_STEPS)
            break;
        if (conversion.inputItemId == itemId || !conversion.outputs.count(itemId))
            continue;
        if (IsConversionFailure(*ctx.data, conversion.spellId, conversion.inputItemId, itemId))
            continue;

        uint32& inputAvailable = ctx.plan.available[conversion.inputItemId];
        if (ctx.plan.seeded.insert(conversion.inputItemId).second)
            if (auto const it = ctx.bags->counts.find(conversion.inputItemId); it != ctx.bags->counts.end())
                inputAvailable += it->second;

        while (inputAvailable >= conversion.inputCount && produced < remaining &&
               ctx.plan.steps.size() < MAX_PLAN_STEPS)
        {
            inputAvailable -= conversion.inputCount;
            ctx.plan.steps.push_back({conversion.spellId, 0, conversion.inputItemId, conversion.inputCount, itemId});
            ++produced;
        }
    }

    ctx.plan.visiting.erase(itemId);
    return produced >= remaining;
}

bool EnsureRecipeIngredients(CraftPlanContext& ctx, ProfessionRecipe const& recipe, uint8 depth)
{
    for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
        if (recipe.reagentCounts[r] && !EnsureItem(ctx, recipe.reagents[r], recipe.reagentCounts[r], depth))
            return false;

    return true;
}

bool PlanRecipe(NewRpgInfo::DoProfessions const& data, ProfessionRecipe const& recipe,
                std::vector<ProfessionRecipe> const& recipes,
                std::unordered_map<uint32, std::vector<size_t>> const& craftIndex, BagSnapshot const& bags,
                std::vector<NewRpgInfo::DoProfessions::CraftStep>& steps)
{
    CraftPlanContext ctx;
    ctx.data = &data;
    ctx.recipes = &recipes;
    ctx.craftIndex = &craftIndex;
    ctx.bags = &bags;

    // The recipe's own output is never a valid input for itself.
    if (recipe.outputItemId)
        ctx.plan.visiting.insert(recipe.outputItemId);

    for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
        if (recipe.reagentCounts[r] && !EnsureItem(ctx, recipe.reagents[r], recipe.reagentCounts[r], 0))
            return false;

    if (ctx.plan.steps.empty())
        return false;

    steps = std::move(ctx.plan.steps);
    return true;
}
}  // namespace

bool NewRpgDoProfessionsAction::LearnFromTrainer(NewRpgInfo::DoProfessions& data)
{
    Creature* trainer = nullptr;
    float bestTrainerDistance = std::numeric_limits<float>::max();
    for (ObjectGuid const guid : AI_VALUE(GuidVector, "possible new rpg targets"))
    {
        Creature* creature = ObjectAccessor::GetCreature(*bot, guid);
        if (!creature || !creature->IsInWorld() || !creature->IsTrainer())
            continue;

        Trainer::Trainer const* trainerData = sObjectMgr->GetTrainer(creature->GetEntry());
        if (!trainerData || trainerData->GetTrainerType() != Trainer::Type::Tradeskill ||
            !trainerData->IsTrainerValidForPlayer(bot))
            continue;

        bool const recentlyVisited = data.trainerGuid == creature->GetGUID() && data.lastTrainer &&
                                     GetMSTimeDiffToNow(data.lastTrainer) < 5 * 60 * IN_MILLISECONDS;
        if (recentlyVisited)
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
        for (Trainer::Spell const& trainerSpell : trainerData->GetSpells())
        {
            if (trainerSpell.ReqSkillLine != data.skillLine && trainerSpell.ReqSkillLine != 0)
                continue;
            if (trainerData->CanTeachSpell(bot, &trainerSpell))
            {
                teachesSkill = true;
                break;
            }
        }
        if (!teachesSkill)
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
    bot->SetSelection(trainer->GetGUID());
    botAI->DoSpecificAction("trainer", Event("trainer", "learn"), true);
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
        return true;
    }

    bool const success = bot->GetItemCount(data.lootWaitItem, false) > data.lootWaitCount;
    if (!success && elapsed < CONVERSION_GRACE_MS)
        return true;

    if (!success)
    {
        data.failedSpell = data.lootWaitSpell;
        data.failedInput = data.lootWaitInput;
        data.failedItem = data.lootWaitItem;
        data.failedAt = getMSTime();
        LOG_DEBUG("playerbots", "[New RPG] {} professions conversion {} on item {} did not yield item {}",
                  bot->GetName(), data.lootWaitSpell, data.lootWaitInput, data.lootWaitItem);
    }
    else if (data.failedSpell == data.lootWaitSpell && data.failedInput == data.lootWaitInput &&
             data.failedItem == data.lootWaitItem)
    {
        data.failedSpell = 0;
        data.failedInput = 0;
        data.failedItem = 0;
        data.failedAt = 0;
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

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(step.spellId);
    if (!spellInfo || !bot->HasSpell(step.spellId) || bot->HasSpellCooldown(step.spellId))
    {
        data.pendingCrafts.clear();
        return true;
    }

    for (uint8 t = 0; t < 2; ++t)
    {
        if (spellInfo->TotemCategory[t] && !bot->HasItemTotemCategory(spellInfo->TotemCategory[t]))
        {
            data.pendingCrafts.clear();
            return true;
        }
    }

    Item* inputItem = nullptr;
    if (step.inputItemId)
    {
        inputItem = FindBagItemByEntry(bot, step.inputItemId);
        if (!inputItem || inputItem->GetCount() < step.inputCount)
        {
            data.pendingCrafts.clear();
            return true;
        }

        if (step.spellId == DISENCHANT_SPELL)
        {
            ItemTemplate const* proto = inputItem->GetTemplate();
            if (!proto || !proto->DisenchantID ||
                AI_VALUE2(ItemUsage, "item usage", step.inputItemId) != ITEM_USAGE_DISENCHANT)
            {
                data.pendingCrafts.clear();
                return true;
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
                data.pendingCrafts.clear();
                return true;
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

    bool const cast =
        step.inputItemId ? botAI->CastSpell(step.spellId, bot, inputItem) : botAI->CastSpell(step.spellId, bot);
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
        data.lootWaitCount = bot->GetItemCount(step.expectedItemId, false);
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
        info.ChangeToIdle();
        return true;
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
            info.ChangeToIdle();
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
                useful = usage == ITEM_USAGE_USE || usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE ||
                         usage == ITEM_USAGE_QUEST || usage == ITEM_USAGE_SKILL;
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
            info.ChangeToIdle();
            return true;
        }

        BagSnapshot bags;
        BuildBagSnapshot(botAI, bags);

        auto tryPlan = [&](std::vector<ProfessionRecipe>& recipes) -> bool
        {
            for (ProfessionRecipe& recipe : recipes)
            {
                std::vector<NewRpgInfo::DoProfessions::CraftStep> steps;
                if (!PlanRecipe(data, recipe, allRecipes, craftIndex, bags, steps))
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
            info.ChangeToIdle();
            return true;
        }
    }

    // Skill-up surplus respects the bag-space rule; actually useful outputs may go up to
    // their item-usage limit even when the bags are already fairly full.
    if (!chosen->useful && !chosen->noItemSpell && bagSpace > 80)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} stopping professions: bags {}% and recipe {} is surplus", bot->GetName(),
                  bagSpace, chosen->spellId);
        info.ChangeToIdle();
        return true;
    }
    if (chosen->useful && bagSpace >= 100)
    {
        info.ChangeToIdle();
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
            info.ChangeToIdle();
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
                info.ChangeToIdle();
                return true;
            }
            LOG_DEBUG("playerbots", "[New RPG] {} professions enchanting item {} ({}) for recipe {}", bot->GetName(),
                      itemTarget->GetEntry(), itemTarget->GetGUID().ToString(), chosen->spellId);
            cast = botAI->CastSpell(chosen->spellId, bot, itemTarget);
        }
    }
    else
        cast = botAI->CastSpell(chosen->spellId, bot);

    if (cast)
        return true;

    LOG_DEBUG("playerbots", "[New RPG] {} professions cast failed for recipe {} (skill {})", bot->GetName(),
              chosen->spellId, data.skillLine);
    botAI->SetNextCheckDelay(1000);
    return false;
}
