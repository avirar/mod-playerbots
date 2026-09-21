/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_STUCKVALUES_H
#define PLAYERBOTS_STUCKVALUES_H

#include "NamedObjectContext.h"
#include "Value.h"

// Ported/adapted from cmangos playerbots strategy/values/StuckValues.{h,cpp}
// (TimeSinceLastChangeValue / DistanceMovedSinceValue).

class TimeSinceLastChangeValue : public Uint32CalculatedValue, public Qualified
{
public:
    TimeSinceLastChangeValue(PlayerbotAI* botAI, std::string const name = "time since last change")
        : Uint32CalculatedValue(botAI, name), Qualified()
    {
    }

    uint32 Calculate() override;
};

class DistanceMovedSinceValue : public Uint32CalculatedValue, public Qualified
{
public:
    DistanceMovedSinceValue(PlayerbotAI* botAI, std::string const name = "distance moved since")
        : Uint32CalculatedValue(botAI, name)
    {
    }

    uint32 Calculate() override;
};

#endif
