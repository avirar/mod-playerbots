/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "InteractStrategy.h"
#include "Playerbots.h"

void InteractStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("interact target available", { NextAction("interact", 5.0f) }));
    triggers.push_back(new TriggerNode("far from interact target", { NextAction("move to interact target", 5.5f) }));
}
