/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "NewRpgMultipliers.h"
#include "Playerbots.h"

float NewRpgLootPriorityMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    if (AI_VALUE(bool, "has available loot"))
    {
        std::string name = action->getName();
        if (name.find("new rpg") == 0)
            return 0.0f;
    }

    return 1.0f;
}
