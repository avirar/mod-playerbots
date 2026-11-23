/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "UsePotionsStrategy.h"

class UsePotionsStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    UsePotionsStrategyActionNodeFactory()
    {
        creators["bandage"] = &bandage;
        creators["healthstone"] = &healthstone;
    }

private:
    // Bandage first, then the one-shot potion; class and racial heals run at higher relevance.
    static ActionNode* bandage(PlayerbotAI* /*botAI*/)
    {
        return new ActionNode("try emergency",
                              /*P*/ {},
                              /*A*/ { NextAction("healthstone"), NextAction("healing potion") },
                              /*C*/ {});
    }

    static ActionNode* healthstone(PlayerbotAI* /*botAI*/)
    {
        return new ActionNode("healthstone",
                              /*P*/ {},
                              /*A*/ { NextAction("healing potion") },
                              /*C*/ {});
    }
};

UsePotionsStrategy::UsePotionsStrategy(PlayerbotAI* botAI) : Strategy(botAI)
{
    actionNodeFactories.Add(new UsePotionsStrategyActionNodeFactory());
}

void UsePotionsStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    Strategy::InitTriggers(triggers);

    // Below class heals, racials and Lifeblood (ACTION_LIGHT_HEAL and up): bandage, then potions.
    triggers.push_back(new TriggerNode(
        "critical health", { NextAction("bandage", ACTION_LIGHT_HEAL - 1) }));
    triggers.push_back(
        new TriggerNode("medium mana", { NextAction("mana potion", ACTION_EMERGENCY) }));
}
