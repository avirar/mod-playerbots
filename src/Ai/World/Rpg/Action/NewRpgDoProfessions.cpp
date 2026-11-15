/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgDoProfessions.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "Bag.h"
#include "Item.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "GameObject.h"
#include "ItemUsageValue.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Playerbots.h"
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
    int32 chance = 0;
    uint32 tier = 0;
    uint32 spellFocus = 0;
    bool useful = false;
    bool noItemSpell = false;  // enchanting: target a bag item instead of creating one
    bool marketable = false;   // enchanting: the scroll output is auction-worthy
};

constexpr uint32 BASIC_CAMPFIRE_SPELL = 818;
constexpr uint32 COOKING_FIRE_FOCUS = 4;
constexpr uint32 CAMPFIRE_WAIT_MS = 10 * IN_MILLISECONDS;

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

    if (enchant->requiredSkill &&
        (!bot->HasSkill(enchant->requiredSkill) || bot->GetSkillValue(enchant->requiredSkill) < enchant->requiredSkillValue))
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

    // Recipe items picked up later are handled by the item-push trigger; this
    // covers items already in the bags when the status starts.
    if (data.casts == 0 && botAI->DoSpecificAction("learn recipe items", Event(), true))
        return true;
    if (LearnFromTrainer(data))
        return true;

    uint8 const bagSpace = AI_VALUE(uint8, "bag space");
    uint32 const skillValue = bot->GetPureSkillValue(data.skillLine);
    bool const skillMaxed = skillValue >= bot->GetMaxSkillValue(data.skillLine);

    std::vector<ProfessionRecipe> usefulRecipes;
    std::vector<ProfessionRecipe> skillUpRecipes;

    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            continue;

        uint32 outputItemId = 0;
        uint32 scrollItemId = 0;
        bool noItemSpell = false;
        for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
        {
            if (spellInfo->Effects[e].Effect == SPELL_EFFECT_CREATE_ITEM)
            {
                outputItemId = spellInfo->Effects[e].ItemType;
                break;
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

        int32 chance = 0;
        uint32 tier = 0;
        bool matchedSkillLine = false;
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineAbilityEntry const* skill = itr->second;
            if (skill->SkillLine != data.skillLine)
                continue;

            matchedSkillLine = true;
            chance = std::max(chance, ItemUsageValue::CraftSkillGainChance(skillValue, skill->TrivialSkillLineRankHigh,
                                                                           skill->TrivialSkillLineRankLow));
            tier = std::max(tier, skill->TrivialSkillLineRankLow);
        }
        if (!matchedSkillLine)
            continue;
        bool hasAnyReagent = false;
        bool hasReagents = true;
        for (uint8 r = 0; r < MAX_SPELL_REAGENTS && hasReagents; ++r)
        {
            if (spellInfo->ReagentCount[r] == 0)
                continue;

            hasAnyReagent = true;
            hasReagents = bot->HasItemCount(spellInfo->Reagent[r], spellInfo->ReagentCount[r], false);
        }
        // CanCastSpell rejects every recipe while the bot is walking to the spell focus,
        // so only gate on the recipe itself here; CastSpell validates the actual cast.
        if (!hasAnyReagent || !hasReagents || bot->HasSpellCooldown(spellId))
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

        // At max skill there is no skill-up to gain; useful outputs (including
        // marketable enchant scrolls) are still crafted.
        bool const worthwhile = useful || marketable;
        if (!worthwhile && (chance <= 0 || skillMaxed))
            continue;

        (worthwhile ? usefulRecipes : skillUpRecipes)
            .push_back({spellId, chance, tier, spellInfo->RequiresSpellFocus, useful, noItemSpell, marketable});
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

    ProfessionRecipe* chosen = pickBest(usefulRecipes);
    if (!chosen)
        chosen = pickBest(skillUpRecipes);

    if (!chosen)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} no professions recipe with skill-up chance and materials (skill {})",
                  bot->GetName(), data.skillLine);
        info.ChangeToIdle();
        return true;
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

    if (chosen->spellFocus)
    {
        Value<ObjectGuid>* focusValue = context->GetValue<ObjectGuid>("spell focus", chosen->spellFocus);
        if (!focusValue)
        {
            LOG_ERROR("playerbots", "[New RPG] {} missing 'spell focus' value", bot->GetName());
            info.ChangeToIdle();
            return true;
        }

        ObjectGuid const focusGuid = focusValue->Get();
        WorldObject* worldObject = focusGuid.IsEmpty() ? nullptr : botAI->GetWorldObject(focusGuid);
        GameObject* focus = worldObject ? worldObject->ToGameObject() : nullptr;
        SpellFocusSpawn const* cached = (!focus && !focusGuid.IsEmpty()) ? sSpellFocusMgr.Get(focusGuid) : nullptr;

        if (!focus && !cached)
        {
            if (TryBasicCampfire(botAI, bot, data, chosen->spellFocus))
                return true;
            if (data.lastCampfire && GetMSTimeDiffToNow(data.lastCampfire) < CAMPFIRE_WAIT_MS)
                return true;

            LOG_DEBUG("playerbots", "[New RPG] {} no spell focus {} for recipe {}", bot->GetName(), chosen->spellFocus,
                      chosen->spellId);
            info.ChangeToIdle();
            return true;
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
            if (distance > 100.0f && TryBasicCampfire(botAI, bot, data, chosen->spellFocus))
                return true;
            if (data.lastCampfire && GetMSTimeDiffToNow(data.lastCampfire) < CAMPFIRE_WAIT_MS)
                return true;

            LOG_DEBUG("playerbots", "[New RPG] {} professions moving to spell focus {} (distance {})", bot->GetName(),
                      focusGuid.ToString(), distance);
            context->GetValue<ObjectGuid>("spell focus target")->Set(focusGuid);
            botAI->DoSpecificAction("move to spell focus", Event("spell focus"), true);
            return true;
        }
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
