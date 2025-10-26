/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license,
 * you may redistribute it and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_USECREATERANDOMITEMSACTION_H
#define _PLAYERBOT_USECREATERANDOMITEMSACTION_H

#include "InventoryAction.h"

class Player;
class Item;
class Event;

class UseCreateRandomItemsAction : public InventoryAction
{
public:
    UseCreateRandomItemsAction(PlayerbotAI* botAI) : InventoryAction(botAI, "use create random items") { }

    bool Execute(Event event) override;
    bool IsValidCreateRandomItemSpell(uint32 spellId);

private:
    Item* FindCreateRandomItem();
};

#endif
