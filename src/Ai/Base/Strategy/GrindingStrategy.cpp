/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "GrindingStrategy.h"

#include "Playerbots.h"

std::vector<NextAction> GrindingStrategy::getDefaultActions()
{
    return {
        NextAction("drink", 4.2f),
        NextAction("food", 4.1f),
    };
}

void GrindingStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Lower priority than loot (5.0f-8.0f), quest items (5.0f-7.0f), food/drink (4.1f-4.2f)
    triggers.push_back(
        new TriggerNode(
            "no target",
            {
                NextAction("attack anything", 3.0f)
            }
        )
    );
}

void MoveRandomStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode(
            "often",
            {
                NextAction("move random", 1.5f)
            }
        )
    );
}
