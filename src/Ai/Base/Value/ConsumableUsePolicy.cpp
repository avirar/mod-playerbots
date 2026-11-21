/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ConsumableUsePolicy.h"

#include "Item.h"
#include "ItemTemplate.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StatsWeightCalculator.h"

namespace
{
constexpr uint8 MAX_TRIGGER_DEPTH = 3;

SpellInfo const* GetFoodSpell(ItemTemplate const* proto)
{
    if (!proto || !proto->Spells[0].SpellId)
        return nullptr;

    return sSpellMgr->GetSpellInfo(proto->Spells[0].SpellId);
}

bool SpellGrantsBuff(SpellInfo const* spell, uint8 depth)
{
    if (!spell || depth > MAX_TRIGGER_DEPTH)
        return false;

    for (SpellEffectInfo const& effect : spell->Effects)
    {
        if (effect.Effect == SPELL_EFFECT_APPLY_AURA)
        {
            switch (effect.ApplyAuraName)
            {
                case SPELL_AURA_MOD_REGEN:
                case SPELL_AURA_OBS_MOD_HEALTH:
                case SPELL_AURA_MOD_POWER_REGEN:
                case SPELL_AURA_OBS_MOD_POWER:
                case SPELL_AURA_PERIODIC_DUMMY:
                    break;
                case SPELL_AURA_PERIODIC_TRIGGER_SPELL:
                case SPELL_AURA_PROC_TRIGGER_SPELL:
                    if (SpellGrantsBuff(sSpellMgr->GetSpellInfo(effect.TriggerSpell), depth + 1))
                        return true;
                    break;
                default:
                    return true;
            }
        }
        else if (effect.Effect == SPELL_EFFECT_TRIGGER_SPELL && effect.TriggerSpell)
        {
            if (SpellGrantsBuff(sSpellMgr->GetSpellInfo(effect.TriggerSpell), depth + 1))
                return true;
        }
    }

    return false;
}

bool SpellHasActiveBuff(Player* bot, SpellInfo const* spell, uint8 depth)
{
    if (!spell || depth > MAX_TRIGGER_DEPTH)
        return false;

    bool directBuff = false;
    for (SpellEffectInfo const& effect : spell->Effects)
    {
        if (effect.Effect == SPELL_EFFECT_APPLY_AURA)
        {
            switch (effect.ApplyAuraName)
            {
                case SPELL_AURA_MOD_REGEN:
                case SPELL_AURA_OBS_MOD_HEALTH:
                case SPELL_AURA_MOD_POWER_REGEN:
                case SPELL_AURA_OBS_MOD_POWER:
                case SPELL_AURA_PERIODIC_DUMMY:
                    break;
                case SPELL_AURA_PERIODIC_TRIGGER_SPELL:
                case SPELL_AURA_PROC_TRIGGER_SPELL:
                    if (!effect.TriggerSpell)
                        break;
                    if (bot->HasAura(effect.TriggerSpell))
                        return true;
                    if (SpellHasActiveBuff(bot, sSpellMgr->GetSpellInfo(effect.TriggerSpell), depth + 1))
                        return true;
                    break;
                default:
                    // The food spell itself carries the stat aura.
                    directBuff = true;
                    break;
            }
        }
        else if (effect.Effect == SPELL_EFFECT_TRIGGER_SPELL && effect.TriggerSpell)
        {
            if (bot->HasAura(effect.TriggerSpell))
                return true;
            if (SpellHasActiveBuff(bot, sSpellMgr->GetSpellInfo(effect.TriggerSpell), depth + 1))
                return true;
        }
    }

    return directBuff && bot->HasAura(spell->Id);
}

float FoodScore(Player* bot, ItemTemplate const* proto)
{
    if (!bot || !proto)
        return 0.0f;

    StatsWeightCalculator calculator(bot);
    return calculator.CalculateItem(proto->ItemId);
}
}  // namespace

namespace ConsumableUsePolicy
{
bool IsBuffFood(ItemTemplate const* proto)
{
    if (!proto || proto->Spells[0].SpellCategory != 11)
        return false;

    return SpellGrantsBuff(GetFoodSpell(proto), 0);
}

bool IsStrictlyBetterFood(Player* bot, ItemTemplate const* candidate, ItemTemplate const* baseline)
{
    if (!candidate || !baseline)
        return false;

    bool const candidateBuff = IsBuffFood(candidate);
    bool const baselineBuff = IsBuffFood(baseline);
    if (candidateBuff != baselineBuff)
        return candidateBuff;

    if (!candidateBuff)
        return candidate->ItemLevel > baseline->ItemLevel;

    float const candidateScore = FoodScore(bot, candidate);
    float const baselineScore = FoodScore(bot, baseline);
    if (candidateScore != baselineScore)
        return candidateScore > baselineScore;

    return candidate->ItemLevel > baseline->ItemLevel;
}

bool IsAtLeastAsGoodFood(Player* bot, ItemTemplate const* candidate, ItemTemplate const* baseline)
{
    if (!candidate || !baseline)
        return false;

    bool const candidateBuff = IsBuffFood(candidate);
    bool const baselineBuff = IsBuffFood(baseline);
    if (candidateBuff != baselineBuff)
        return candidateBuff;

    if (!candidateBuff)
        return candidate->ItemLevel >= baseline->ItemLevel;

    float const candidateScore = FoodScore(bot, candidate);
    float const baselineScore = FoodScore(bot, baseline);
    if (candidateScore != baselineScore)
        return candidateScore > baselineScore;

    return candidate->ItemLevel >= baseline->ItemLevel;
}

Item* ChooseFood(Player* bot, std::vector<Item*> const& items)
{
    bool wellFed = false;
    Item* bestBuff = nullptr;
    float bestBuffScore = -1.0f;
    Item* bestConjured = nullptr;
    Item* bestRegular = nullptr;

    StatsWeightCalculator calculator(bot);
    for (Item* item : items)
    {
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        if (!proto)
            continue;

        SpellInfo const* spell = GetFoodSpell(proto);
        if (IsBuffFood(proto))
        {
            wellFed = wellFed || SpellHasActiveBuff(bot, spell, 0);

            float const score = calculator.CalculateItem(proto->ItemId);
            ItemTemplate const* bestProto = bestBuff ? bestBuff->GetTemplate() : nullptr;
            if (!bestProto || proto->ItemLevel > bestProto->ItemLevel ||
                (proto->ItemLevel == bestProto->ItemLevel && score > bestBuffScore))
            {
                bestBuff = item;
                bestBuffScore = score;
            }
        }
        else if (proto->IsConjuredConsumable())
        {
            ItemTemplate const* bestProto = bestConjured ? bestConjured->GetTemplate() : nullptr;
            if (!bestProto || proto->ItemLevel > bestProto->ItemLevel)
                bestConjured = item;
        }
        else
        {
            ItemTemplate const* bestProto = bestRegular ? bestRegular->GetTemplate() : nullptr;
            if (!bestProto || proto->ItemLevel > bestProto->ItemLevel)
                bestRegular = item;
        }
    }

    if (!wellFed && bestBuff)
        return bestBuff;
    if (bestConjured)
        return bestConjured;
    if (bestRegular)
        return bestRegular;
    return bestBuff;
}

Item* ChooseDrink(Player* bot, std::vector<Item*> const& items)
{
    (void)bot;
    Item* bestConjured = nullptr;
    Item* bestRegular = nullptr;

    for (Item* item : items)
    {
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        if (!proto || !proto->Spells[0].SpellId || proto->Spells[0].SpellCategory != 59)
            continue;

        if (proto->IsConjuredConsumable())
        {
            ItemTemplate const* bestProto = bestConjured ? bestConjured->GetTemplate() : nullptr;
            if (!bestProto || proto->ItemLevel > bestProto->ItemLevel)
                bestConjured = item;
        }
        else
        {
            ItemTemplate const* bestProto = bestRegular ? bestRegular->GetTemplate() : nullptr;
            if (!bestProto || proto->ItemLevel > bestProto->ItemLevel)
                bestRegular = item;
        }
    }

    return bestConjured ? bestConjured : bestRegular;
}
}  // namespace ConsumableUsePolicy
