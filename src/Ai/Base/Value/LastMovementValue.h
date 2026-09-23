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
        frozenRefMap = other.frozenRefMap;
        frozenRefX = other.frozenRefX;
        frozenRefY = other.frozenRefY;
        frozenRefZ = other.frozenRefZ;
        frozenSinceMs = other.frozenSinceMs;
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
    // Frozen-in-place escape bookkeeping (MoveTo2): the reference position
    // captured when the bot last made real progress (>=3y from the previous
    // reference). If it stays within 3y of that reference for 20s+, it is
    // stuck (ridge foot / excluded nav poly where neither the probe nor the
    // unstick step can produce movement) and MoveTo2 unstuck-teleports it.
    uint32 frozenRefMap = 0;
    float frozenRefX = 0.0f;
    float frozenRefY = 0.0f;
    float frozenRefZ = 0.0f;
    uint32 frozenSinceMs = 0;
    // Board-walk bookkeeping (walk-on transport boarding, TransportTeleportType=0):
    // entry of the transport the walk-to-deck dispatch was made for and the
    // getMSTime() of that dispatch. UseTransport re-enters every tick while the
    // bot is on the transport leg; the in-flight check keeps it from
    // re-dispatching the same walk, and ages the dispatch so the direct-attach
    // backstop can fire once the walk should have reached the deck.
    uint32 boardWalkEntry = 0;
    uint32 boardWalkMs = 0;
    // getMSTime() of the last transport disembark. The 1s attach check would
    // re-board a bot that just walked off the boat (it is still above the
    // hull for a few ticks); the client's equivalent is the ONTRANSPORT flag
    // being cleared once the player is off the deck. Suppresses re-attach for
    // a short window after disembark.
    uint32 disembarkMs = 0;
    // Where the bot boarded its current/last transport ride (board position
    // + boat entry). The 1s disembark trigger must not fire at the departure
    // stop: the ride starts inside 20y of the departure stop keyframe while
    // the bot waits for the boat to leave the dock.
    uint32 boardedEntry = 0;
    float boardedX = 0.0f;
    float boardedY = 0.0f;
    float boardedZ = 0.0f;
    uint32 boardedMs = 0;
    // Position-stall tracking for the current transport (fed by the 1s
    // transport check). A boat on a taxi path only pauses at stop keyframes.
    // TIME-BASED (not tick-based, 2026-09-23 Elune's fix): the 1s check runs
    // on the main AI cadence, which for remote-move bots is sporadic (5-26s
    // observed) and the map-change Reset(true) wipes the state mid-ride — a
    // tick counter ("4 x 1s samples") can take 40-100s real time to fill,
    // longer than the 60s DBC stop dwell, so the bot never disembarked. A
    // wall-clock "last position change > 5s ago = docked" test is
    // cadence-independent. Public-API stand-in for the private
    // MotionTransport::IsMoving().
    uint32 transportStallEntry = 0;
    float transportStallX = 0.0f;
    float transportStallY = 0.0f;
    float transportStallZ = 0.0f;
    uint32 transportLastMoveMs = 0;

    void RefreshFrozenRef(uint32 mapId, float x, float y, float z);
    bool IsFrozenStanding(Unit* bot, uint32 nowMs) const;
    bool BoardWalkInFlight(uint32 entry, uint32 nowMs) const;
    void SetBoardWalk(uint32 entry, uint32 nowMs);
    void ClearBoardWalk();
    void SetDisembark(uint32 nowMs);
    bool DisembarkRecent(uint32 nowMs) const;
    void SetBoarded(uint32 entry, float x, float y, float z, uint32 nowMs);
    bool BoardedNearStop(uint32 entry, float x, float y, uint32 nowMs, float maxDist) const;
    void ClearBoarded();
    bool NoteTransportMoving(uint32 entry, float x, float y, float z, uint32 nowMs);
    void ClearTransportStall();

    // Position-stall tracking for the bot's OWN movement (fed by the
    // UpdateRemoteMove yield gate). isMoving() stays true on a degenerate
    // zero-length dispatch (a 1-point path to where the bot already stands),
    // which wedges the yield gate forever and starves the special-movement
    // branch (the board leg / WaitForTransport). A 4s position stall with
    // isMoving()==true means the movement generator is wedged — release the
    // gate so the special branch can engage.
    bool NoteBotStalled(float x, float y, float z, uint32 nowMs);
    void ClearBotStall();

private:
    float botStallX = 0.0f;
    float botStallY = 0.0f;
    float botStallZ = 0.0f;
    uint32 botStallFirstMs = 0;
    bool botStalled = false;
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
