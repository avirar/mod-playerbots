/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_USEQUESTSPELLONTARGETACTION_H
#define _PLAYERBOT_USEQUESTSPELLONTARGETACTION_H

#include "Action.h"

class PlayerbotAI;
class Unit;
class WorldObject;

/**
 * @brief Action that automatically casts quest-required spells on valid targets
 *
 * This action handles quests that require the player to cast a spell on creatures
 * (e.g., Gift of the Naaru on Draenei Survivors). It searches active quests for
 * spell-cast requirements and locates valid targets to complete the objective.
 *
 * The action will:
 * - Find active quests requiring spell casts on creatures (RequiredNpcOrGo objectives)
 * - Determine the appropriate spell to cast (racial spells, class spells, etc.)
 * - Locate valid target creatures within range
 * - Cast the spell on the target if conditions are met
 * - Track usage to prevent spam casting
 */
class UseQuestSpellOnTargetAction : public Action
{
public:
    UseQuestSpellOnTargetAction(PlayerbotAI* botAI) : Action(botAI, "quest spell use on target") {}

    bool Execute(Event event) override;
    bool isUseful() override;
    bool isPossible() override;

private:
    bool CastQuestSpellOnTarget(uint32 spellId, Unit* target);
};

#endif
