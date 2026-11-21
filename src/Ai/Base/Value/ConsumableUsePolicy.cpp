/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ConsumableUsePolicy.h"

#include <array>

#include "Item.h"
#include "ItemTemplate.h"
#include "Map.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StatsWeightCalculator.h"

namespace
{
constexpr uint8 MAX_TRIGGER_DEPTH = 3;
constexpr uint32 FLASK_OF_THE_NORTH_SPELL = 67019;
constexpr uint32 RECENTLY_BANDAGED_SPELL = 11196;
constexpr uint32 DEFAULT_BANDAGE_CHANNEL_MS = 8 * IN_MILLISECONDS;

SpellInfo const* GetItemSpell(ItemTemplate const* proto)
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
                    // The spell itself carries the stat aura.
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

}  // namespace

namespace ConsumableUsePolicy
{
bool IsBuffFood(ItemTemplate const* proto)
{
    if (!proto || proto->Spells[0].SpellCategory != 11)
        return false;

    return SpellGrantsBuff(GetItemSpell(proto), 0);
}

float Score(Player* bot, ItemTemplate const* proto)
{
    if (!bot || !proto)
        return 0.0f;

    StatsWeightCalculator calculator(bot);
    return calculator.CalculateItem(proto->ItemId);
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

    float const candidateScore = Score(bot, candidate);
    float const baselineScore = Score(bot, baseline);
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

    float const candidateScore = Score(bot, candidate);
    float const baselineScore = Score(bot, baseline);
    if (candidateScore != baselineScore)
        return candidateScore > baselineScore;

    return candidate->ItemLevel >= baseline->ItemLevel;
}

bool IsFlask(ItemTemplate const* proto)
{
    return proto && proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_FLASK;
}

bool IsElixir(ItemTemplate const* proto)
{
    return proto && proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_ELIXIR;
}
bool IsScroll(ItemTemplate const* proto)
{
    if (!proto || proto->Class != ITEM_CLASS_CONSUMABLE || proto->SubClass != ITEM_SUBCLASS_SCROLL)
        return false;

    SpellInfo const* spell = GetItemSpell(proto);
    return spell && spell->GetSpellSpecific() == SPELL_SPECIFIC_SCROLL;
}

bool IsGuardianElixir(ItemTemplate const* proto)
{
    if (!IsElixir(proto))
        return false;

    SpellInfo const* spell = GetItemSpell(proto);
    return spell && sSpellMgr->IsSpellMemberOfSpellGroup(spell->Id, SPELL_GROUP_ELIXIR_GUARDIAN);
}

bool IsBattleElixir(ItemTemplate const* proto)
{
    if (!IsElixir(proto) || IsGuardianElixir(proto))
        return false;

    SpellInfo const* spell = GetItemSpell(proto);
    return spell && sSpellMgr->IsSpellMemberOfSpellGroup(spell->Id, SPELL_GROUP_ELIXIR_BATTLE);
}

bool IsStrictlyBetterBuffItem(Player* bot, ItemTemplate const* candidate, ItemTemplate const* baseline)
{
    if (!candidate || !baseline)
        return false;

    float const candidateScore = Score(bot, candidate);
    float const baselineScore = Score(bot, baseline);
    if (candidateScore != baselineScore)
        return candidateScore > baselineScore;

    return candidate->ItemLevel > baseline->ItemLevel;
}

bool HasFlaskBuff(Player* bot)
{
    if (!bot)
        return false;

    for (auto const& [auraId, aura] : bot->GetOwnedAuras())
    {
        if (!aura)
            continue;
        if (auraId == FLASK_OF_THE_NORTH_SPELL)
            return true;
        if (sSpellMgr->IsSpellMemberOfSpellGroup(auraId, SPELL_GROUP_ELIXIR_BATTLE) &&
            sSpellMgr->IsSpellMemberOfSpellGroup(auraId, SPELL_GROUP_ELIXIR_GUARDIAN))
            return true;
    }

    return false;
}

bool HasElixirBuff(Player* bot, bool battle)
{
    if (!bot)
        return false;

    SpellGroup const group = battle ? SPELL_GROUP_ELIXIR_BATTLE : SPELL_GROUP_ELIXIR_GUARDIAN;
    for (auto const& [auraId, aura] : bot->GetOwnedAuras())
    {
        if (!aura || auraId == FLASK_OF_THE_NORTH_SPELL)
            continue;
        if (sSpellMgr->IsSpellMemberOfSpellGroup(auraId, SPELL_GROUP_ELIXIR_BATTLE) &&
            sSpellMgr->IsSpellMemberOfSpellGroup(auraId, SPELL_GROUP_ELIXIR_GUARDIAN))
            continue;
        if (sSpellMgr->IsSpellMemberOfSpellGroup(auraId, group))
            return true;
    }

    return false;
}

bool HasScrollBuff(Player* bot)
{
    if (!bot)
        return false;

    for (auto const& [auraId, aura] : bot->GetOwnedAuras())
    {
        if (!aura)
            continue;
        SpellInfo const* spell = aura->GetSpellInfo();
        if (spell && spell->GetSpellSpecific() == SPELL_SPECIFIC_SCROLL)
            return true;
    }

    return false;
}

uint32 ScrollCategory(ItemTemplate const* proto)
{
    SpellInfo const* spell = GetItemSpell(proto);
    if (!spell)
        return 0;

    for (SpellEffectInfo const& effect : spell->Effects)
    {
        if (effect.Effect != SPELL_EFFECT_APPLY_AURA)
            continue;
        switch (effect.ApplyAuraName)
        {
            case SPELL_AURA_MOD_REGEN:
            case SPELL_AURA_OBS_MOD_HEALTH:
            case SPELL_AURA_MOD_POWER_REGEN:
            case SPELL_AURA_OBS_MOD_POWER:
            case SPELL_AURA_PERIODIC_DUMMY:
                continue;
            default:
                return (uint32(effect.ApplyAuraName) << 16) | uint16(effect.MiscValue);
        }
    }

    return 0;
}

bool ScrollCategoryCovered(Player* bot, ItemTemplate const* proto)
{
    SpellInfo const* spell = GetItemSpell(proto);
    if (!bot || !spell)
        return false;

    auto coveredByClassBuff = [bot](AuraType type, int32 misc, int32 minimumAmount)
    {
        for (AuraEffect* aura : bot->GetAuraEffectsByType(type))
        {
            if (!aura)
                continue;
            SpellInfo const* auraSpell = aura->GetSpellInfo();
            if (!auraSpell || auraSpell->SpellFamilyName == SPELLFAMILY_GENERIC)
                continue;

            // Self-armor spells carry dummy stat effects for their scaling
            // (e.g. Fel Armor's Spirit-to-spell-power effect). They are not
            // class stat buffs and must not suppress a scroll.
            SpellSpecificType const specific = auraSpell->GetSpellSpecific();
            if (specific == SPELL_SPECIFIC_WARLOCK_ARMOR || specific == SPELL_SPECIFIC_MAGE_ARMOR ||
                specific == SPELL_SPECIFIC_ELEMENTAL_SHIELD || specific == SPELL_SPECIFIC_PRESENCE)
                continue;

            if (misc >= 0 && aura->GetMiscValue() != misc && aura->GetMiscValue() >= 0)
                continue;

            // Same-stat modifiers are exclusive by absolute value (see
            // SPELL_GROUP_STACK_RULE_EXCLUSIVE_SAME_EFFECT), so a weaker class
            // buff does not block a stronger scroll. Percentage buffs (Kings)
            // have no comparable flat amount.
            if (type != SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE && minimumAmount > 0 &&
                aura->GetAmount() < minimumAmount)
                continue;

            return true;
        }
        return false;
    };

    for (SpellEffectInfo const& effect : spell->Effects)
    {
        if (effect.Effect != SPELL_EFFECT_APPLY_AURA)
            continue;

        int32 const scrollAmount = effect.BasePoints + 1;
        if (coveredByClassBuff(AuraType(effect.ApplyAuraName), effect.MiscValue, scrollAmount))
            return true;

        if (effect.ApplyAuraName == SPELL_AURA_MOD_STAT &&
            coveredByClassBuff(SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE, -1, 0))
            return true;
    }

    return false;
}

uint32 BandageChannelMs(ItemTemplate const* bandage)
{
    SpellInfo const* spell = GetItemSpell(bandage);
    if (!spell)
        return DEFAULT_BANDAGE_CHANNEL_MS;

    int32 const duration = spell->GetDuration();
    return duration > 0 ? uint32(duration) : DEFAULT_BANDAGE_CHANNEL_MS;
}

bool CanUseBandageInCombat(Player* bot, ItemTemplate const* bandage)
{
    if (!bot || !bot->GetMap() || bot->GetMap()->IsRaid())
        return false;

    if (bot->HasAura(RECENTLY_BANDAGED_SPELL))
        return false;

    if (!bot->getAttackers().empty())
        return false;

    uint32 const half = BandageChannelMs(bandage) / 2;
    std::array<AuraType, 3> const damageAuras = {
        SPELL_AURA_PERIODIC_DAMAGE,
        SPELL_AURA_PERIODIC_DAMAGE_PERCENT,
        SPELL_AURA_PERIODIC_LEECH};

    for (AuraType type : damageAuras)
    {
        for (AuraEffect* effect : bot->GetAuraEffectsByType(type))
        {
            if (!effect)
                continue;

            int32 const amplitude = effect->GetAmplitude();
            if (amplitude <= 0)
                continue;

            Aura* aura = effect->GetBase();
            if (!aura)
                continue;

            int32 const remaining = aura->GetDuration();
            if (remaining <= 0)
                continue;

            int32 nextTick = remaining % amplitude;
            if (nextTick == 0)
                nextTick = amplitude;

            if (uint32(nextTick) <= half)
                return false;
        }
    }

    return true;
}

Item* ChooseFood(Player* bot, std::vector<Item*> const& items)
{
    bool wellFed = false;
    Item* bestBuff = nullptr;
    float bestBuffScore = -1.0f;
    Item* bestConjured = nullptr;
    Item* bestRegular = nullptr;

    for (Item* item : items)
    {
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        if (!proto)
            continue;

        // Drinks share the food subclass and can leak into generic food queries.
        if (proto->Spells[0].SpellCategory != 11)
            continue;

        if (IsBuffFood(proto))
        {
            wellFed = wellFed || SpellHasActiveBuff(bot, GetItemSpell(proto), 0);

            float const score = Score(bot, proto);
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
