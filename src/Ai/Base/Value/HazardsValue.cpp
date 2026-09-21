/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "HazardsValue.h"
#include "Playerbots.h"
#include <algorithm>

// Ported/adapted from cmangos playerbots strategy/values/HazardsValue.cpp.

Hazard::Hazard()
    : position(WorldPosition()), expirationTime(0), guid(ObjectGuid()), radius(0.0f), hazardNavmeshArea(0),
      previousNavmeshArea(0)
{
}

Hazard::Hazard(WorldPosition const& inPosition, uint64 inExpiration, float inRadius)
    : position(inPosition), expirationTime(time(nullptr) + inExpiration), guid(ObjectGuid()), radius(inRadius),
      hazardNavmeshArea(14), previousNavmeshArea(11)
{
}

Hazard::Hazard(ObjectGuid const& inGuid, uint64 inExpiration, float inRadius)
    : position(WorldPosition()), expirationTime(time(nullptr) + inExpiration), guid(inGuid), radius(inRadius),
      hazardNavmeshArea(14), previousNavmeshArea(11)
{
}

bool Hazard::operator==(Hazard const& other) const
{
    if (!guid.IsEmpty())
        return (guid == other.guid);

    if (position)
        return position == other.position;

    return false;
}

bool Hazard::operator<(Hazard const& other) const
{
    return radius < other.radius;
}

bool Hazard::GetPosition(PlayerbotAI* ai, WorldPosition& outPosition)
{
    WorldObject const* object = GetObject(ai);
    if (object)
    {
        outPosition = position = WorldPosition(object);
        return true;
    }

    if (position)
    {
        outPosition = position;
        return true;
    }

    return false;
}

bool Hazard::IsExpired() const
{
    return time(nullptr) > expirationTime;
}

bool Hazard::IsValid(PlayerbotAI* ai) const
{
    WorldObject const* object = GetObject(ai);
    return (object || position) && !IsExpired();
}

WorldObject const* Hazard::GetObject(PlayerbotAI* ai) const
{
    if (!guid.IsEmpty())
    {
        if (guid.IsCreature() || guid.IsPlayer())
            return ai->GetUnit(guid);

        if (guid.IsGameObject())
            return ai->GetGameObject(guid);
    }

    return nullptr;
}

void AddHazardValue::Set(Hazard hazard)
{
    std::list<Hazard> storedHazards = AI_VALUE(std::list<Hazard>, "stored hazards");

    auto it = std::find(storedHazards.begin(), storedHazards.end(), hazard);
    if (it == storedHazards.end())
    {
        storedHazards.emplace_back(std::move(hazard));
        SET_AI_VALUE(std::list<Hazard>, "stored hazards", storedHazards);
    }
}

std::list<HazardPosition> HazardsValue::Calculate()
{
    std::list<Hazard> storedHazards = AI_VALUE(std::list<Hazard>, "stored hazards");

    bool hazardsUpdated = false;
    std::list<HazardPosition> hazards;
    for (auto it = storedHazards.begin(); it != storedHazards.end();)
    {
        bool validHazard = false;
        Hazard& hazard = (*it);
        if (hazard.IsValid(botAI))
        {
            WorldPosition hazardPosition;
            if (hazard.GetPosition(botAI, hazardPosition))
            {
                hazards.emplace_back(std::make_pair(hazardPosition, hazard.GetRadius()));
                validHazard = true;
            }
        }

        if (!validHazard)
        {
            it = storedHazards.erase(it);
            hazardsUpdated = true;
        }
        else
        {
            ++it;
        }
    }

    if (hazardsUpdated)
        SET_AI_VALUE(std::list<Hazard>, "stored hazards", storedHazards);

    return hazards;
}
