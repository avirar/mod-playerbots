/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ConsumeBuffAction.h"

#include "ConsumableUsePolicy.h"
#include "Event.h"
#include "Map.h"
#include "Playerbots.h"

bool ConsumeBuffAction::isPossible()
{
    return bot->IsAlive() && !bot->IsInCombat() && !bot->IsInFlight() && !botAI->IsInVehicle();
}

bool ConsumeBuffAction::isUseful() { return PickBuffItem() != nullptr; }

bool ConsumeBuffAction::Execute(Event /*event*/)
{
    Item* item = PickBuffItem();
    if (!item)
        return false;

    bool const used = UseItemAuto(item);
    if (used)
        botAI->SetNextCheckDelay(1000);
    return used;
}

Item* ConsumeBuffAction::PickBuffItem()
{
    bool const inRaid = bot->GetMap() && bot->GetMap()->IsRaid();
    bool const flaskActive = ConsumableUsePolicy::HasFlaskBuff(bot);

    if (!flaskActive)
    {
        if (Item* flask = BestFlask(inRaid))
            return flask;

        if (!ConsumableUsePolicy::HasElixirBuff(bot, true))
            if (Item* elixir = BestElixir(true))
                return elixir;

        if (!ConsumableUsePolicy::HasElixirBuff(bot, false))
            if (Item* elixir = BestElixir(false))
                return elixir;
    }

    if (!ConsumableUsePolicy::HasScrollBuff(bot))
        if (Item* scroll = BestScroll())
            return scroll;

    return nullptr;
}

Item* ConsumeBuffAction::BestFlask(bool allowNormalFlasks)
{
    std::vector<Item*> const flasks = AI_VALUE2(std::vector<Item*>, "inventory items", "flask");

    Item* best = nullptr;
    float bestScore = -1.0f;
    for (Item* item : flasks)
    {
        if (!item || (!allowNormalFlasks && item->GetEntry() != ConsumableUsePolicy::FLASK_OF_THE_NORTH))
            continue;

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            continue;

        float const score = ConsumableUsePolicy::Score(bot, proto);
        ItemTemplate const* bestProto = best ? best->GetTemplate() : nullptr;
        if (!bestProto || score > bestScore || (score == bestScore && proto->ItemLevel > bestProto->ItemLevel))
        {
            best = item;
            bestScore = score;
        }
    }

    return best;
}

Item* ConsumeBuffAction::BestElixir(bool battle)
{
    std::vector<Item*> const elixirs =
        AI_VALUE2(std::vector<Item*>, "inventory items", battle ? "battle elixir" : "guardian elixir");

    Item* best = nullptr;
    float bestScore = -1.0f;
    for (Item* item : elixirs)
    {
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        if (!proto)
            continue;

        float const score = ConsumableUsePolicy::Score(bot, proto);
        ItemTemplate const* bestProto = best ? best->GetTemplate() : nullptr;
        if (!bestProto || score > bestScore || (score == bestScore && proto->ItemLevel > bestProto->ItemLevel))
        {
            best = item;
            bestScore = score;
        }
    }

    return best;
}

Item* ConsumeBuffAction::BestScroll()
{
    std::vector<Item*> const scrolls = AI_VALUE2(std::vector<Item*>, "inventory items", "scroll");

    Item* best = nullptr;
    float bestScore = -1.0f;
    for (Item* item : scrolls)
    {
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        if (!proto || ConsumableUsePolicy::ScrollCategoryCovered(bot, proto))
            continue;

        float const score = ConsumableUsePolicy::Score(bot, proto);
        ItemTemplate const* bestProto = best ? best->GetTemplate() : nullptr;
        if (!bestProto || score > bestScore || (score == bestScore && proto->ItemLevel > bestProto->ItemLevel))
        {
            best = item;
            bestScore = score;
        }
    }

    return best;
}
