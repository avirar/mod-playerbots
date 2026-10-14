/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "InteractActions.h"

#include "InteractValues.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Playerbots.h"

bool InteractAction::isUseful()
{
    if (!sPlayerbotAIConfig.InteractStrategy || !bot || !bot->IsInWorld() || bot->IsInCombat())
        return false;

    InteractExecutor* executor = AI_VALUE(InteractExecutor*, "interact executor");
    if (!executor)
        return false;

    executor->Refresh(bot);
    return executor->BestNearby() != nullptr;
}

bool InteractAction::Execute(Event /*event*/)
{
    InteractExecutor* executor = AI_VALUE(InteractExecutor*, "interact executor");
    if (!executor)
        return false;

    executor->Refresh(bot);

    InteractTarget const* target = executor->BestNearby();
    if (!target)
        return false;

    Item* item = bot->GetItemByEntry(target->candidate.itemId);
    if (!item || !item->GetTemplate())
    {
        executor->NotifyFailed(*target);
        return false;
    }

    bool used = false;

    switch (target->candidate.kind)
    {
        case InteractKind::UseOnCreature:
        {
            Unit* unit = ObjectAccessor::GetUnit(*bot, target->guid);
            if (!unit || !unit->IsAlive())
            {
                executor->NotifyFailed(*target);
                return false;
            }

            used = UseItem(item, ObjectGuid::Empty, nullptr, unit);
            break;
        }
        case InteractKind::UseOnGameObject:
        {
            GameObject* go = ObjectAccessor::GetGameObject(*bot, target->guid);
            if (!go)
            {
                executor->NotifyFailed(*target);
                return false;
            }

            used = UseItem(item, target->guid, nullptr);
            break;
        }
        case InteractKind::SelfUse:
        case InteractKind::UseAtLocation:
        default:
            used = UseItem(item, ObjectGuid::Empty, nullptr);
            break;
    }

    if (used)
        executor->NotifyUsed(*target);
    else
        executor->NotifyFailed(*target);

    return used;
}

bool MoveToInteractTargetAction::isUseful()
{
    if (!sPlayerbotAIConfig.InteractStrategy || !bot || !bot->IsInWorld() || bot->IsInCombat())
        return false;

    InteractExecutor* executor = AI_VALUE(InteractExecutor*, "interact executor");
    if (!executor)
        return false;

    executor->Refresh(bot);
    return executor->BestNearby() == nullptr && executor->BestFar() != nullptr;
}

bool MoveToInteractTargetAction::Execute(Event /*event*/)
{
    InteractExecutor* executor = AI_VALUE(InteractExecutor*, "interact executor");
    if (!executor)
        return false;

    executor->Refresh(bot);

    InteractTarget const* target = executor->BestFar();
    if (!target)
        return false;

    return MoveTo(target->mapId, target->x, target->y, target->z);
}
