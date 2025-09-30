/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_MOVETOQUESTITEMTARGETACTION_H
#define _PLAYERBOT_MOVETOQUESTITEMTARGETACTION_H

#include "MovementActions.h"

class Item;
class PlayerbotAI;
class Unit;

/**
 * @brief Action that moves the bot towards a valid quest item target
 * 
 * This action works in conjunction with UseQuestItemOnTargetAction to position
 * the bot within range of valid quest targets. It uses the same target finding
 * logic but focuses on movement rather than item usage.
 * 
 * The action will:
 * - Find quest items that need targets
 * - Locate the closest valid target
 * - Move the bot within spell/interaction range of the target
 */
class MoveToQuestItemTargetAction : public MovementAction
{
public:
    MoveToQuestItemTargetAction(PlayerbotAI* botAI) : MovementAction(botAI, "move to quest item target") {}

    bool Execute(Event event) override;
    bool isUseful() override;

private:
};

#endif
