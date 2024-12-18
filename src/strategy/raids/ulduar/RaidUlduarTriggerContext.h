// /*
//  * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
//  and/or modify it under version 2 of the License, or (at your option), any later version.
//  */

#ifndef _PLAYERBOT_RAIDULDUARTRIGGERCONTEXT_H
#define _PLAYERBOT_RAIDULDUARTRIGGERCONTEXT_H

#include "AiObjectContext.h"
#include "NamedObjectContext.h"
#include "RaidUlduarTriggers.h"

class RaidUlduarTriggerContext : public NamedObjectContext<Trigger>
{
public:
    RaidUlduarTriggerContext()
    {
        creators["flame leviathan on vehicle"] = &RaidUlduarTriggerContext::flame_leviathan_on_vehicle;
        creators["flame leviathan vehicle near"] = &RaidUlduarTriggerContext::flame_leviathan_vehicle_near;
        creators["ignis move construct to scorched ground"] = &RaidUlduarTriggerContext::ignis_move_construct_to_scorched_ground;
        creators["ignis move molten construct to water"] = &RaidUlduarTriggerContext::ignis_move_molten_construct_to_water;
        creators["ignis choose target"] = &RaidUlduarTriggerContext::ignis_choose_target;
        creators["ignis position"] = &RaidUlduarTriggerContext::ignis_position;
    }

private:
    static Trigger* flame_leviathan_on_vehicle(PlayerbotAI* ai) { return new FlameLeviathanOnVehicleTrigger(ai); }
    static Trigger* flame_leviathan_vehicle_near(PlayerbotAI* ai) { return new FlameLeviathanVehicleNearTrigger(ai); }
    static Trigger* ignis_move_construct_to_scorched_ground(PlayerbotAI* ai) { return new IgnisMoveConstructToScorchedGroundTrigger(ai); }
    static Trigger* ignis_move_molten_construct_to_water(PlayerbotAI* ai) { return new IgnisMoveMoltenConstructToWaterTrigger(ai); }
    static Trigger* ignis_choose_target(PlayerbotAI* ai) { return new IgnisChooseTargetTrigger(ai); }
    static Trigger* ignis_position(PlayerbotAI* ai) { return new IgnisPositionTrigger(ai); }
};

#endif
