/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_FREEMOVEVALUES_H
#define PLAYERBOTS_FREEMOVEVALUES_H

#include "NamedObjectContext.h"
#include "TravelMgr.h"
#include "Value.h"

class PlayerbotAI;
class Unit;

// Free-move leash value family, ported/adapted from cmangos playerbots
// strategy/values/FreeMoveValues.{h,cpp}. The center/range pair defines the
// anchored leash around which CanFreeMove* gates decide whether a destination
// is still within the bot's free movement area.
class FreeMoveCenterValue : public CalculatedValue<GuidPosition>
{
public:
    FreeMoveCenterValue(PlayerbotAI* botAI) : CalculatedValue<GuidPosition>(botAI, "free move center", 5) {}
    GuidPosition Calculate() override;
};

class FreeMoveRangeValue : public FloatCalculatedValue
{
public:
    FreeMoveRangeValue(PlayerbotAI* botAI) : FloatCalculatedValue(botAI, "free move range", 2) {}
    float Calculate() override;
};

class CanFreeMoveValue : public BoolCalculatedValue, public Qualified
{
public:
    CanFreeMoveValue(PlayerbotAI* botAI, std::string const name = "can free move", int32 checkInterval = 2)
        : BoolCalculatedValue(botAI, name, checkInterval), Qualified()
    {
    }

    static bool CanFreeMoveTo(PlayerbotAI* botAI, WorldPosition dest);
    static bool CanFreeTarget(PlayerbotAI* botAI, WorldPosition dest);
    static bool CanFreeAttack(PlayerbotAI* botAI, WorldPosition dest);

    bool Calculate() override;

private:
    static bool CanFreeMove(PlayerbotAI* botAI, WorldPosition dest, float range);
};

#endif
