/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "StuckValues.h"
#include "Playerbots.h"
#include "TravelMgr.h"
#include <cmath>

// Ported/adapted from cmangos playerbots strategy/values/StuckValues.cpp.

uint32 TimeSinceLastChangeValue::Calculate()
{
    UntypedValue* val = context->GetUntypedValue(getQualifier());
    if (!val)
        return 0;

    return val->LastChangeDelay();
}

uint32 DistanceMovedSinceValue::Calculate()
{
    uint32 minTimePassed = getQualifier().empty() ? 0 : static_cast<uint32>(std::stoul(getQualifier()));

    auto* posVal = dynamic_cast<LogCalculatedValue<WorldPosition>*>(context->GetUntypedValue("current position"));
    if (!posVal)
        return 0;

    WorldPosition botPos(bot);
    bool hasEnoughData = false;
    float maxSqDistance = 0.0f;

    for (auto tPos : posVal->ValueLog())
    {
        uint32 timePassed = time(nullptr) - tPos.second;
        if (timePassed > minTimePassed)
            hasEnoughData = true;

        float distance = tPos.first.sqDistance(botPos);
        if (distance > maxSqDistance)
            maxSqDistance = distance;
    }

    if (!hasEnoughData)
        return 0;

    return static_cast<uint32>(std::sqrt(maxSqDistance));
}
