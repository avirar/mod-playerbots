/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_HAZARDSVALUE_H
#define PLAYERBOTS_HAZARDSVALUE_H

#include "ObjectGuid.h"
#include "TravelMgr.h"
#include "Value.h"

// Ported/adapted from cmangos playerbots strategy/values/HazardsValue.{h,cpp}.

class Hazard
{
public:
    Hazard();
    Hazard(WorldPosition const& position, uint64 expiration, float radius);
    Hazard(ObjectGuid const& guid, uint64 expiration, float radius);
    bool operator==(Hazard const& other) const;
    bool operator<(Hazard const& other) const;

    bool GetPosition(PlayerbotAI* ai, WorldPosition& outPosition);
    float GetRadius() const { return radius; }

    bool IsValid(PlayerbotAI* ai) const;
    bool IsExpired() const;

    uint32 GetNavmeshArea() const { return hazardNavmeshArea; }
    uint32 GetPreviousNavmeshArea() const { return previousNavmeshArea; }

private:
    WorldObject const* GetObject(PlayerbotAI* ai) const;

private:
    WorldPosition position;
    time_t expirationTime;
    ObjectGuid guid;
    float radius;

    uint32 hazardNavmeshArea;
    uint32 previousNavmeshArea;
};

// Hazard position, hazard radius.
typedef std::pair<WorldPosition, float> HazardPosition;

// Storage of hazards the bot must avoid while moving.
class StoredHazardsValue : public ManualSetValue<std::list<Hazard>>
{
public:
    StoredHazardsValue(PlayerbotAI* botAI, std::string const name = "stored hazards")
        : ManualSetValue<std::list<Hazard>>(botAI, {}, name)
    {
    }
};

// Add a hazard (dedup) to the stored list.
class AddHazardValue : public ManualSetValue<Hazard>
{
public:
    AddHazardValue(PlayerbotAI* botAI, std::string const name = "add hazard") : ManualSetValue<Hazard>(botAI, Hazard(), name)
    {
    }

private:
    void Set(Hazard hazard) override;
};

// Position/radius pairs of currently-valid hazards.
class HazardsValue : public CalculatedValue<std::list<HazardPosition>>
{
public:
    HazardsValue(PlayerbotAI* botAI, std::string const name = "hazards")
        : CalculatedValue<std::list<HazardPosition>>(botAI, name, 1)
    {
    }

private:
    std::list<HazardPosition> Calculate() override;
};

#endif
