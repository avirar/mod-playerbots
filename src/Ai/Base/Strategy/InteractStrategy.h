/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_INTERACTSTRATEGY_H
#define PLAYERBOTS_INTERACTSTRATEGY_H

#include "Strategy.h"

class PlayerbotAI;

// Quest-item / world interaction strategy (non-combat half of the interact family; the combat
// variant is planned behind Playerbots.InteractInCombat).
class InteractStrategy : public Strategy
{
public:
    InteractStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "interact"; }
};

#endif
