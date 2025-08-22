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

// Action that moves the bot towards a valid quest item target
class MoveToQuestItemTargetAction : public MovementAction
{
public:
    MoveToQuestItemTargetAction(PlayerbotAI* botAI) : MovementAction(botAI, "move to quest item target") {}

    bool Execute(Event event) override;
    bool isUseful() override;

private:
    // Helper methods for finding quest targets
    Item* FindBestQuestItem(uint32* outSpellId = nullptr) const;
    Unit* FindBestTargetForQuestItem(uint32 spellId) const;
    bool IsValidQuestItem(Item* item, uint32* outSpellId = nullptr) const;
    bool IsTargetValidForSpell(Unit* target, uint32 spellId) const;
    bool CheckSpellConditions(uint32 spellId, Unit* target) const;
};

#endif