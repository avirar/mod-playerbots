/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_USEQUESTITEMONTARGETACTION_H
#define _PLAYERBOT_USEQUESTITEMONTARGETACTION_H

#include "UseItemAction.h"

class Item;
class PlayerbotAI;
class Unit;

// Action that uses quest items with spells on valid targets
class UseQuestItemOnTargetAction : public UseSpellItemAction
{
public:
    UseQuestItemOnTargetAction(PlayerbotAI* botAI) : UseSpellItemAction(botAI, "quest item use on target") {}

    bool Execute(Event event) override;
    bool isUseful() override;

private:
    // Helper methods for quest item usage
    Item* FindBestQuestItem(uint32* outSpellId = nullptr) const;
    Unit* FindBestTargetForQuestItem(uint32 spellId) const;
    bool IsValidQuestItem(Item* item, uint32* outSpellId = nullptr) const;
    bool IsTargetValidForSpell(Unit* target, uint32 spellId) const;
    bool CheckSpellConditions(uint32 spellId, Unit* target) const;
    bool UseQuestItemOnTarget(Item* item, Unit* target) const;
};

#endif