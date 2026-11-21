/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_CONSUMABLEUSEPOLICY_H
#define PLAYERBOTS_CONSUMABLEUSEPOLICY_H

#include <vector>

#include "Define.h"

class Item;
class Player;
struct ItemTemplate;

namespace ConsumableUsePolicy
{
    constexpr uint32 FLASK_OF_THE_NORTH = 47499;

    // Food whose spell, or a spell it triggers, grants a Well Fed buff.
    bool IsBuffFood(ItemTemplate const* proto);

    // Buff food beats heal-only food. Buff food is scored for the bot's spec.
    bool IsStrictlyBetterFood(Player* bot, ItemTemplate const* candidate, ItemTemplate const* baseline);
    bool IsAtLeastAsGoodFood(Player* bot, ItemTemplate const* candidate, ItemTemplate const* baseline);

    // Missing Well Fed: best buff food, then conjured, then regular.
    // Well Fed: conjured, then regular, then buff food.
    Item* ChooseFood(Player* bot, std::vector<Item*> const& items);

    // Conjured drink first, then regular drink.
    Item* ChooseDrink(Player* bot, std::vector<Item*> const& items);

    // Flask / elixir / scroll classification.
    bool IsFlask(ItemTemplate const* proto);
    bool IsElixir(ItemTemplate const* proto);
    bool IsScroll(ItemTemplate const* proto);
    bool IsBattleElixir(ItemTemplate const* proto);
    bool IsGuardianElixir(ItemTemplate const* proto);

    // Higher is better; uses StatsWeightCalculator and falls back to item level.
    float Score(Player* bot, ItemTemplate const* proto);
    bool IsStrictlyBetterBuffItem(Player* bot, ItemTemplate const* candidate, ItemTemplate const* baseline);

    // Active buff checks across the bot's auras.
    bool HasFlaskBuff(Player* bot);
    bool HasElixirBuff(Player* bot, bool battle);
    bool HasScrollBuff(Player* bot);

    // Scroll category: an aura type/misc key, 0 if not a scroll buff.
    uint32 ScrollCategory(ItemTemplate const* proto);
    bool ScrollCategoryCovered(Player* bot, ItemTemplate const* proto);

    // Combat bandage safety: not in a raid, no attackers, no DoT tick before
    // half of the channel (the channel is cancelled by any damage).
    bool CanUseBandageInCombat(Player* bot, ItemTemplate const* bandage);
    uint32 BandageChannelMs(ItemTemplate const* bandage);
}  // namespace ConsumableUsePolicy

#endif
