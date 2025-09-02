/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DEBUGSTRATEGY_H
#define _PLAYERBOT_DEBUGSTRATEGY_H

#include "Strategy.h"

class DebugStrategy : public Strategy
{
public:
    DebugStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    uint32 GetType() const override { return STRATEGY_TYPE_NONCOMBAT; }
    std::string const getName() override { return "debug"; }
};

class DebugMoveStrategy : public Strategy
{
public:
    DebugMoveStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    uint32 GetType() const override { return STRATEGY_TYPE_NONCOMBAT; }
    std::string const getName() override { return "debug move"; }
};

class DebugRpgStrategy : public Strategy
{
public:
    DebugRpgStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    uint32 GetType() const override { return STRATEGY_TYPE_NONCOMBAT; }
    std::string const getName() override { return "debug rpg"; }
};

class DebugSpellStrategy : public Strategy
{
public:
    DebugSpellStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    uint32 GetType() const override { return STRATEGY_TYPE_NONCOMBAT; }
    std::string const getName() override { return "debug spell"; }
};

class DebugQuestStrategy : public Strategy
{
public:
    DebugQuestStrategy(PlayerbotAI* botAI) : Strategy(botAI) { }

    uint32 GetType() const override { return STRATEGY_TYPE_NONCOMBAT; }
    std::string const getName() override { return "debug quest"; }
};

class DebugQuestItemsStrategy : public Strategy
{
public:
    DebugQuestItemsStrategy(PlayerbotAI* botAI) : Strategy(botAI) { }

    uint32 GetType() const override { return STRATEGY_TYPE_NONCOMBAT; }
    std::string const getName() override { return "debug questitems"; }
};

class DebugNewRpgStrategy : public Strategy
{
public:
    DebugNewRpgStrategy(PlayerbotAI* botAI) : Strategy(botAI) { }

    uint32 GetType() const override { return STRATEGY_TYPE_NONCOMBAT; }
    std::string const getName() override { return "debug newrpg"; }
};

class DebugLootStrategy : public Strategy
{
public:
    DebugLootStrategy(PlayerbotAI* botAI) : Strategy(botAI) { }

    uint32 GetType() const override { return STRATEGY_TYPE_NONCOMBAT; }
    std::string const getName() override { return "debug loot"; }
};

class DebugTargetsStrategy : public Strategy
{
public:
    DebugTargetsStrategy(PlayerbotAI* botAI) : Strategy(botAI) { }

    uint32 GetType() const override { return STRATEGY_TYPE_NONCOMBAT; }
    std::string const getName() override { return "debug targets"; }
};

#endif
