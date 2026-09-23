/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "LastMovementValue.h"
#include "Timer.h"
#include <cmath>

LastMovement::LastMovement() { clear(); }

LastMovement::LastMovement(LastMovement& other)
    : taxiNodes(other.taxiNodes),
      taxiMaster(other.taxiMaster),
      lastFollow(other.lastFollow),
      lastAreaTrigger(other.lastAreaTrigger),
      lastMoveToX(other.lastMoveToX),
      lastMoveToY(other.lastMoveToY),
      lastMoveToZ(other.lastMoveToZ),
      lastMoveToOri(other.lastMoveToOri),
      lastFlee(other.lastFlee)
{
    lastMoveShort = other.lastMoveShort;
    nextTeleport = other.nextTeleport;
    lastPath = other.lastPath;
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
}

void LastMovement::clear()
{
    lastMoveShort = WorldPosition();
    lastPath.clear();
    lastMoveToMapId = 0;
    lastMoveToX = 0;
    lastMoveToY = 0;
    lastMoveToZ = 0;
    lastMoveToOri = 0;
    lastFollow = nullptr;
    lastAreaTrigger = 0;
    lastFlee = 0;
    nextTeleport = 0;
    msTime = 0;
    lastdelayTime = 0;
    priority = MovementPriority::MOVEMENT_NORMAL;
    lastTransportEntry = 0;
    noPathMs = 0;
    noPathDestMapId = 0;
    noPathDestX = 0.0f;
    noPathDestY = 0.0f;
    frozenRefMap = 0;
    frozenSinceMs = 0;
    boardWalkEntry = 0;
    boardWalkMs = 0;
    disembarkMs = 0;
    boardedEntry = 0;
    boardedX = boardedY = boardedZ = 0.0f;
    boardedMs = 0;
    transportStallEntry = 0;
    transportStallX = transportStallY = transportStallZ = 0.0f;
    transportLastMoveMs = 0;
}

void LastMovement::RefreshFrozenRef(uint32 mapId, float x, float y, float z)
{
    frozenRefMap = mapId;
    frozenRefX = x;
    frozenRefY = y;
    frozenRefZ = z;
    frozenSinceMs = getMSTime();
}

bool LastMovement::IsFrozenStanding(Unit* bot, uint32 nowMs) const
{
    static constexpr float kFrozenRadius = 3.0f;
    static constexpr uint32 kFrozenMs = 20000;

    if (!frozenSinceMs || !bot || !frozenRefMap || frozenRefMap != bot->GetMapId())
        return false;

    if (nowMs - frozenSinceMs < kFrozenMs)
        return false;

    float const dx = bot->GetPositionX() - frozenRefX;
    float const dy = bot->GetPositionY() - frozenRefY;
    float const dz = bot->GetPositionZ() - frozenRefZ;
    return (dx * dx + dy * dy + dz * dz) < kFrozenRadius * kFrozenRadius;
}

bool LastMovement::BoardWalkInFlight(uint32 entry, uint32 nowMs) const
{
    return entry && boardWalkEntry == entry && boardWalkMs && (nowMs - boardWalkMs) < 8000;
}

void LastMovement::SetBoardWalk(uint32 entry, uint32 nowMs)
{
    boardWalkEntry = entry;
    boardWalkMs = nowMs;
}

void LastMovement::ClearBoardWalk()
{
    boardWalkEntry = 0;
    boardWalkMs = 0;
}

void LastMovement::SetDisembark(uint32 nowMs)
{
    disembarkMs = nowMs;
}

bool LastMovement::DisembarkRecent(uint32 nowMs) const
{
    return disembarkMs && (nowMs - disembarkMs) < 5000;
}

void LastMovement::SetBoarded(uint32 entry, float x, float y, float z, uint32 nowMs)
{
    boardedEntry = entry;
    boardedX = x;
    boardedY = y;
    boardedZ = z;
    boardedMs = nowMs;
    ClearTransportStall();
}

// True when the bot boarded the given transport (recently) at a position
// within maxDist of (x, y) — i.e. (x, y) is the departure dock. The ride
// itself moves the boat many yards from that stop before the arrival dock
// is anywhere near the board position.
bool LastMovement::BoardedNearStop(uint32 entry, float x, float y, uint32 nowMs, float maxDist) const
{
    if (!boardedEntry || boardedEntry != entry || !boardedMs || (nowMs - boardedMs) > 20 * 60 * 1000u)
        return false;
    float const dx = boardedX - x;
    float const dy = boardedY - y;
    return std::sqrt(dx * dx + dy * dy) < maxDist;
}

void LastMovement::ClearBoarded()
{
    boardedEntry = 0;
    boardedX = boardedY = boardedZ = 0.0f;
    boardedMs = 0;
    ClearTransportStall();
}

// Feeds one position sample of the bot's current transport (any cadence).
// Returns true while the boat is considered moving (position changed since
// the last sample, or the last observed change was < 5s ago in wall-clock
// time), false once it has been stationary for >= 5s real time — i.e. docked
// at a stop keyframe (boats on taxi paths only pause at stops). Time-based
// so the verdict is independent of how often the caller samples (the 1s
// check runs on the main AI cadence, which is sporadic for remote-move
// bots — 5-26s observed 2026-09-23).
bool LastMovement::NoteTransportMoving(uint32 entry, float x, float y, float z, uint32 nowMs)
{
    if (!transportStallEntry || transportStallEntry != entry)
    {
        transportStallEntry = entry;
        transportStallX = x;
        transportStallY = y;
        transportStallZ = z;
        transportLastMoveMs = nowMs;
        return true;
    }
    float const dx = x - transportStallX;
    float const dy = y - transportStallY;
    float const dz = z - transportStallZ;
    // 0.5m threshold: a docked boat bobs/tilts slightly (a few-decm of z and
    // heading); changes below that are the bob, not forward motion. A boat on
    // a taxi path moves many yards per second, so 0.5m is far below real travel.
    if (dx * dx + dy * dy + dz * dz > 0.25f)
    {
        transportStallX = x;
        transportStallY = y;
        transportStallZ = z;
        transportLastMoveMs = nowMs;
        return true;
    }
    return nowMs - transportLastMoveMs <= 5000u;
}

void LastMovement::ClearTransportStall()
{
    transportStallEntry = 0;
    transportStallX = transportStallY = transportStallZ = 0.0f;
    transportLastMoveMs = 0;
}

bool LastMovement::NoteBotStalled(float x, float y, float z, uint32 nowMs)
{
    if (botStallFirstMs &&
        std::fabsf(x - botStallX) < 0.2f && std::fabsf(y - botStallY) < 0.2f && std::fabsf(z - botStallZ) < 0.5f)
    {
        if (!botStalled && nowMs - botStallFirstMs >= 4000)
            botStalled = true;
        return botStalled;
    }
    botStallX = x; botStallY = y; botStallZ = z;
    botStallFirstMs = nowMs;
    botStalled = false;
    return false;
}

void LastMovement::ClearBotStall()
{
    botStallFirstMs = 0;
    botStalled = false;
}

void LastMovement::Set(Unit* follow)
{
    Set(0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    setShort(WorldPosition());
    setPath(TravelPath());
    lastFollow = follow;
}

void LastMovement::Set(uint32 mapId, float x, float y, float z, float ori, float delayTime, MovementPriority pri)
{
    lastMoveToMapId = mapId;
    lastMoveToX = x;
    lastMoveToY = y;
    lastMoveToZ = z;
    lastMoveToOri = ori;
    lastFollow = nullptr;
    lastMoveShort = WorldPosition(mapId, x, y, z, ori);
    msTime = getMSTime();
    lastdelayTime = delayTime;
    priority = pri;
}

void LastMovement::setShort(WorldPosition point)
{
    lastMoveShort = point;
    lastFollow = nullptr;
}

void LastMovement::setPath(TravelPath path) { lastPath = path; }
