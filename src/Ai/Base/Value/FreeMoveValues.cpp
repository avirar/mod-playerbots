/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "FreeMoveValues.h"
#include "Formations.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "TargetValue.h"
#include <algorithm>

// Ported/adapted from cmangos playerbots strategy/values/FreeMoveValues.cpp.
// Adaptations (documented in inventory-movement-values.md section 2):
// - The target has no "follow target" value; the follow anchor is the formation target name
//   (default "group leader").
// - Formation::GetLocation() returns WorldLocation in the target (GuidPosition in OG); the
//   result GuidPosition is built from the follow target's guid + that location.
// - OG's `loc += (lastLongMove - player)` uses operators absent in the target's WorldPosition,
//   so the delta is built via operator-= / operator+=.

GuidPosition FreeMoveCenterValue::Calculate()
{
    if (botAI->HasStrategy("follow", botAI->GetState()) || botAI->HasStrategy("wander", botAI->GetState()))
    {
        Unit* followTarget = AI_VALUE(Unit*, "group leader");

        if (!followTarget)
            return bot;

        // Use bot as center when follow target is on a different map.
        if (followTarget->GetMapId() != bot->GetMapId())
            return followTarget;

        Player* player = dynamic_cast<Player*>(followTarget);

        // Use bot as center when follow target is being teleported.
        if (player && player->IsBeingTeleported())
            return bot;

        // Use formation location as reference point.
        Formation* formation = AI_VALUE(Formation*, "formation");
        WorldLocation const loc = formation->GetLocation();

        if (Formation::IsNullLocation(loc) || loc.GetMapId() == MAPID_INVALID)
            return followTarget;

        GuidPosition result;
        static_cast<ObjectGuid&>(result) = followTarget->GetGUID();
        static_cast<WorldPosition&>(result) = WorldPosition(loc);

        // Move the location to a location around the follow target's destination.
        if (player)
        {
            PlayerbotAI* followAI = GET_PLAYERBOT_AI(player);
            if (followAI && botAI->IsSafe(player))
            {
                WorldPosition lastLongMove =
                    followAI->GetAiObjectContext()->GetValue<WorldPosition>("last long move")->Get();
                if (lastLongMove)
                {
                    WorldPosition delta = lastLongMove;
                    delta -= WorldPosition(player);
                    result += delta;
                }
            }
        }

        return result;
    }

    PositionInfo pos;

    if (botAI->HasStrategy("stay", botAI->GetState()) && (pos = AI_VALUE2(PositionInfo, "pos", "stay")).isSet())
    {
        GuidPosition result;
        static_cast<ObjectGuid&>(result) = bot->GetGUID();
        static_cast<WorldPosition&>(result) = WorldPosition(pos.mapId, pos.x, pos.y, pos.z);
        return result;
    }

    if (botAI->HasStrategy("guard", botAI->GetState()) && (pos = AI_VALUE2(PositionInfo, "pos", "guard")).isSet())
    {
        GuidPosition result;
        static_cast<ObjectGuid&>(result) = bot->GetGUID();
        static_cast<WorldPosition&>(result) = WorldPosition(pos.mapId, pos.x, pos.y, pos.z);
        return result;
    }

    return bot;
}

float FreeMoveRangeValue::Calculate()
{
    if (botAI->HasStrategy("stay", botAI->GetState()))
        return INTERACTION_DISTANCE;

    if (AI_VALUE(Unit*, "rti cc target"))
        return botAI->GetRange("spell");

    Unit* followTarget = AI_VALUE(Unit*, "group leader");

    if (!followTarget || followTarget == bot)
        return 0;

    if (botAI->HasStrategy("wander", botAI->GetState()))
        return botAI->GetRange("wandermax");

    if (botAI->HasStrategy("follow", botAI->GetState()))
        return botAI->GetRange("follow");

    if (botAI->HasStrategy("guard", botAI->GetState()))
        return botAI->GetRange("guard");

    return 0;
}

bool CanFreeMoveValue::CanFreeMove(PlayerbotAI* botAI, WorldPosition dest, float range)
{
    if (!dest)
        return true;

    if (!range)
        return true;

    AiObjectContext* context = botAI->GetAiObjectContext();

    GuidPosition center = AI_VALUE(GuidPosition, "free move center");
    return center.distance(dest) < range;
}

bool CanFreeMoveValue::CanFreeMoveTo(PlayerbotAI* botAI, WorldPosition dest)
{
    AiObjectContext* context = botAI->GetAiObjectContext();
    return CanFreeMove(botAI, dest, AI_VALUE(float, "free move range"));
}

bool CanFreeMoveValue::CanFreeTarget(PlayerbotAI* botAI, WorldPosition dest)
{
    AiObjectContext* context = botAI->GetAiObjectContext();
    float range = botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT)
                      ? std::min(botAI->GetRange("spell"), AI_VALUE(float, "free move range"))
                      : AI_VALUE(float, "free move range");

    return CanFreeMove(botAI, dest, range);
}

bool CanFreeMoveValue::CanFreeAttack(PlayerbotAI* botAI, WorldPosition dest)
{
    return CanFreeMove(botAI, dest, botAI->GetRange("attack"));
}

bool CanFreeMoveValue::Calculate()
{
    float range = 0.0f;

    if (qualifier.empty())
        range = AI_VALUE(float, "free move range");
    else
        range = botAI->GetRange(qualifier);

    return CanFreeMove(botAI, bot, range);
}
