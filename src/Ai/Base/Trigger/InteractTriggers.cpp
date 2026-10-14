/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "InteractTriggers.h"

#include "InteractValues.h"
#include "Player.h"
#include "Playerbots.h"

bool InteractTargetAvailableTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.InteractStrategy || !bot || !bot->IsInWorld() || bot->IsInCombat())
        return false;

    InteractExecutor* executor = AI_VALUE(InteractExecutor*, "interact executor");
    if (!executor)
        return false;

    executor->Refresh(bot);
    return executor->BestNearby() != nullptr;
}

bool FarFromInteractTargetTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.InteractStrategy || !bot || !bot->IsInWorld() || bot->IsInCombat())
        return false;

    InteractExecutor* executor = AI_VALUE(InteractExecutor*, "interact executor");
    if (!executor)
        return false;

    executor->Refresh(bot);
    return executor->BestNearby() == nullptr && executor->BestFar() != nullptr;
}
