/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "UseQuestItemsStrategy.h"

#include "Playerbots.h"

void UseQuestItemsStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // High priority trigger for using quest items when valid targets are available and in range
    triggers.push_back(new TriggerNode("quest item usable", NextAction::array(0, new NextAction("quest item use on target", 6.0f), nullptr)));
    
    // Higher priority trigger for moving to quest targets when out of range
    triggers.push_back(
        new TriggerNode("far from quest item target", NextAction::array(0, new NextAction("move to quest item target", 7.0f), nullptr)));
    
    // Lower priority periodic check to see if quest items can be used (should be a different action)
    triggers.push_back(new TriggerNode("often", NextAction::array(0, new NextAction("quest item use on target", 5.0f), nullptr)));
}
