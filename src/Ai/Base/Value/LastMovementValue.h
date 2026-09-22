/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_LASTMOVEMENTVALUE_H
#define PLAYERBOTS_LASTMOVEMENTVALUE_H

#include "ObjectGuid.h"
#include "TravelNode.h"
#include "Value.h"

class PlayerbotAI;
class Unit;

// High priority movement can override the previous low priority one
enum class MovementPriority
{
    MOVEMENT_IDLE,
    MOVEMENT_WANDER,
    MOVEMENT_NORMAL,
    MOVEMENT_COMBAT,
    MOVEMENT_FORCED
};

class LastMovement
{
public:
    LastMovement();
    LastMovement(LastMovement& other);

    LastMovement& operator=(LastMovement const& other)
    {
        taxiNodes = other.taxiNodes;
        taxiMaster = other.taxiMaster;
        lastFollow = other.lastFollow;
        lastAreaTrigger = other.lastAreaTrigger;
        lastMoveShort = other.lastMoveShort;
        lastPath = other.lastPath;
        nextTeleport = other.nextTeleport;
        priority = other.priority;
        lastTransportEntry = other.lastTransportEntry;
        noPathMs = other.noPathMs;
        noPathDestMapId = other.noPathDestMapId;
        noPathDestX = other.noPathDestX;
        noPathDestY = other.noPathDestY;
        return *this;
    };

    void clear();

    void Set(Unit* follow);
    void Set(uint32 mapId, float x, float y, float z, float ori, float delayTime, MovementPriority priority = MovementPriority::MOVEMENT_NORMAL);

    void setShort(WorldPosition point);
    void setPath(TravelPath path);

    std::vector<uint32> taxiNodes;
    ObjectGuid taxiMaster;
    Unit* lastFollow;
    uint32 lastAreaTrigger;
    time_t lastFlee;
    uint32 lastMoveToMapId;
    float lastMoveToX;
    float lastMoveToY;
    float lastMoveToZ;
    float lastMoveToOri;
    float lastdelayTime;
    WorldPosition lastMoveShort;
    uint32 msTime;
    MovementPriority priority;
    TravelPath lastPath;
    time_t nextTeleport;
    std::future<TravelPath> future;
    // Entry of the transport the bot is mid-journey on; used by the
    // cross-continent transport leg to resume boarding/disembarking.
    uint32 lastTransportEntry = 0;
    // No-path failure state (debounces repeated resolutions): when
    // ResolveMovePath produced no path at all for a destination, re-running
    // the full probe + node-graph search every tick is pure waste (a single
    // resolution can cost ~100ms; with many bots stalling on unreachable POIs
    // that alone inflates world ticks into the hundreds of milliseconds).
    // noPathMs holds getMSTime() of the last such failure plus the failed
    // destination, so MoveTo2 skips re-resolving until the cooldown elapses
    // (by then the unstick nudge / the caller's MoveRandomNear fallback has
    // moved the bot anyway).
    uint32 noPathMs = 0;
    uint32 noPathDestMapId = 0;
    float noPathDestX = 0.0f;
    float noPathDestY = 0.0f;
};

class LastMovementValue : public ManualSetValue<LastMovement&>
{
public:
    LastMovementValue(PlayerbotAI* botAI) : ManualSetValue<LastMovement&>(botAI, data) {}

private:
    LastMovement data = LastMovement();
};

class StayTimeValue : public ManualSetValue<time_t>
{
public:
    StayTimeValue(PlayerbotAI* botAI) : ManualSetValue<time_t>(botAI, 0) {}
};

#endif
