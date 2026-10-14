/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "InteractValues.h"

#include "Playerbots.h"

InteractExecutorValue::InteractExecutorValue(PlayerbotAI* botAI, std::string const name)
    : ManualSetValue<InteractExecutor*>(botAI, nullptr, name)
{
    value = new InteractExecutor();
}

InteractExecutorValue::~InteractExecutorValue() { delete value; }
