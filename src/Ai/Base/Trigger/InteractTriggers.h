/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_INTERACTTRIGGERS_H
#define PLAYERBOTS_INTERACTTRIGGERS_H

#include "PlayerbotAIConfig.h"
#include "Trigger.h"

class PlayerbotAI;

class InteractTargetAvailableTrigger : public Trigger
{
public:
    InteractTargetAvailableTrigger(PlayerbotAI* botAI) : Trigger(botAI, "interact target available") {}

    bool IsActive() override;
};

class FarFromInteractTargetTrigger : public Trigger
{
public:
    FarFromInteractTargetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "far from interact target") {}

    bool IsActive() override;
};

#endif
