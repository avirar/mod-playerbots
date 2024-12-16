/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "MoveToTravelTargetAction.h"

#include "ChooseRpgTargetAction.h"
#include "LootObjectStack.h"
#include "PathGenerator.h"
#include "Playerbots.h"

bool MoveToTravelTargetAction::Execute(Event event)
{
    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");
    WorldPosition botLocation(bot);
    WorldLocation location = *target->getPosition();

    Group* group = bot->GetGroup();
    if (group && !urand(0, 1) && bot == botAI->GetGroupMaster())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member == bot || !member->IsAlive() || !member->isMoving())
                continue;

            PlayerbotAI* memberBotAI = GET_PLAYERBOT_AI(member);
            if (memberBotAI && !memberBotAI->HasStrategy("follow", BOT_STATE_NON_COMBAT))
                continue;

            WorldPosition memberPos(member);
            if (botLocation.distance(memberPos) > sPlayerbotAIConfig->reactDistance * 20)
                continue;

            if (!urand(0, 5))
            {
                std::ostringstream out;
                out << (botAI->GetMaster() && !bot->GetGroup()->IsMember(botAI->GetMaster()->GetGUID()) ? "Waiting a bit for " : "Please hurry up ") << member->GetName();
                botAI->TellMasterNoFacing(out);
            }

            target->setExpireIn(target->getTimeLeft() + sPlayerbotAIConfig->maxWaitForMove);
            botAI->SetNextCheckDelay(sPlayerbotAIConfig->maxWaitForMove);
            return true;
        }
    }

    float maxDistance = target->getDestination()->getRadiusMin();
    float x = location.GetPositionX();
    float y = location.GetPositionY();
    float z = location.GetPositionZ();
    float mapId = location.GetMapId();

    // Add flying arc logic if the bot is flying
    bool isFlying = bot->HasUnitMovementFlag(MOVEMENTFLAG_FLYING);
    if (isFlying)
    {
        WorldPosition start(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        WorldPosition end(mapId, x, y, z);

        float arcHeight = std::clamp(start.distance(end) * 0.1f, 10.0f, 50.0f); // 10% of the distance
        auto arcPath = start.createFlyingArc(start, end, arcHeight, 10);        // 10 points for smooth flight

        for (auto& point : arcPath)
        {
            if (!MoveTo(point.getMapId(), point.getX(), point.getY(), point.getZ(), false, false))
            {
                target->incRetry(true);
                if (target->isMaxRetry(true))
                    target->setStatus(TRAVEL_STATUS_COOLDOWN);
                return false;
            }
        }

        target->setRetry(true);
        return true;
    }

    // For non-flying logic, fallback to ground movement
    bool canMove = false;
    if (bot->IsWithinLOS(x, y, z))
        canMove = MoveNear(mapId, x, y, z, 0);
    else
        canMove = MoveTo(mapId, x, y, z, false, false);

    if (!canMove && !target->isForced())
    {
        target->incRetry(true);
        if (target->isMaxRetry(true))
            target->setStatus(TRAVEL_STATUS_COOLDOWN);
    }
    else
        target->setRetry(true);

    return canMove;
}

bool MoveToTravelTargetAction::isUseful()
{
    if (!botAI->AllowActivity(TRAVEL_ACTIVITY))
        return false;

    if (!context->GetValue<TravelTarget*>("travel target")->Get()->isTraveling())
        return false;
/*
    if (bot->HasUnitState(UNIT_STATE_IN_FLIGHT))
        return false;

    if (bot->IsFlying())
        return false;
*/
    if (bot->isMoving())
        return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    LootObject loot = AI_VALUE(LootObject, "loot target");
    if (loot.IsLootPossible(bot))
        return false;

    if (!ChooseRpgTargetAction::isFollowValid(bot,
                                              *context->GetValue<TravelTarget*>("travel target")->Get()->getPosition()))
        return false;

    return true;
}
