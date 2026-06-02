/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_NEWRPGMULTIPLIERS_H
#define _PLAYERBOT_NEWRPGMULTIPLIERS_H

#include "Multiplier.h"

class NewRpgLootPriorityMultiplier : public Multiplier
{
public:
    NewRpgLootPriorityMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "new rpg loot priority") {}
    float GetValue(Action* action) override;
};

#endif
