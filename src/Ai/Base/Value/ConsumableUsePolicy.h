/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_CONSUMABLEUSEPOLICY_H
#define PLAYERBOTS_CONSUMABLEUSEPOLICY_H

#include <vector>

class Item;
class Player;
struct ItemTemplate;

namespace ConsumableUsePolicy
{
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
}  // namespace ConsumableUsePolicy

#endif
