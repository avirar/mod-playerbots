/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license,
 * you may redistribute it and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_UNLOCKITEMACTION_H
#define _PLAYERBOT_UNLOCKITEMACTION_H

#include "CastCustomSpellAction.h"

class Player;
class Item;
class Event;

class UnlockItemAction : public CastCustomSpellAction
{
public:
    UnlockItemAction(PlayerbotAI* botAI) : CastCustomSpellAction(botAI, "unlock item") { }

    bool isUseful() override;
    bool Execute(Event event) override;
    // Rename the function to avoid conflicts
    // bool Unlock(Item* item, uint8 bag, uint8 slot);
    void UnlockItem(Item* item, uint8 bag, uint8 slot);
};

#endif
