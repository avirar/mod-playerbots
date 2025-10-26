/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "UseQuestItemsStrategy.h"

#include "Playerbots.h"

void UseQuestItemsStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Quest item actions: 5.0f - 6.0f priority band
    // This prevents quest items from destroying corpses before looting (7.0f - 8.0f band)

    // Move to quest item targets
    triggers.push_back(
        new TriggerNode("far from quest item target", NextAction::array(0, new NextAction("move to quest item target", 6.0f), nullptr)));

    // Use quest items when in range
    triggers.push_back(new TriggerNode("quest item usable", NextAction::array(0, new NextAction("quest item use on target", 5.5f), nullptr)));

    // Periodic quest item check
    triggers.push_back(new TriggerNode("often", NextAction::array(0, new NextAction("quest item use on target", 5.0f), nullptr)));

    // Quest spell actions: same priority band as quest items
    // Use quest spells (e.g., Gift of the Naaru on Draenei Survivors)
    triggers.push_back(new TriggerNode("often", NextAction::array(0, new NextAction("quest spell use on target", 5.0f), nullptr)));
}
