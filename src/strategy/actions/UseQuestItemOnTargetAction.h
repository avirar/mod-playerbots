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

/**
 * @brief Action that automatically uses quest items with spells on valid targets
 * 
 * This action searches the bot's inventory for quest items that have associated spells
 * and can be used on nearby targets. It validates targets using the game's condition
 * system and ensures proper range checking before attempting to use the item.
 * 
 * The action will:
 * - Find quest items with player-castable spells
 * - Locate valid targets within grind distance
 * - Verify spell conditions (aura requirements, creature types, etc.)
 * - Use the quest item on the target if in range
 */
class UseQuestItemOnTargetAction : public UseSpellItemAction
{
public:
    UseQuestItemOnTargetAction(PlayerbotAI* botAI) : UseSpellItemAction(botAI, "quest item use on target") {}

    bool Execute(Event event) override;
    bool isUseful() override;
    bool isPossible() override;

private:
    bool UseQuestItemOnTarget(Item* item, Unit* target);
};

#endif
