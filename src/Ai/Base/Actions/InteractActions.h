/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_INTERACTACTIONS_H
#define PLAYERBOTS_INTERACTACTIONS_H

#include "MovementActions.h"
#include "UseItemAction.h"

class PlayerbotAI;

// Uses the best nearby quest-item interaction (item on creature/GO, at a location, or on self).
class InteractAction : public UseItemAction
{
public:
    InteractAction(PlayerbotAI* botAI) : UseItemAction(botAI, "interact") {}

    bool Execute(Event event) override;
    bool isUseful() override;
    bool isPossible() override { return true; }
};

// Walks to the best interaction target that is outside action range.
class MoveToInteractTargetAction : public MovementAction
{
public:
    MoveToInteractTargetAction(PlayerbotAI* botAI) : MovementAction(botAI, "move to interact target") {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

#endif
