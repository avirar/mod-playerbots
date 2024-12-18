// /*
//  * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
//  and/or modify it under version 2 of the License, or (at your option), any later version.
//  */

#ifndef _PLAYERBOT_RAIDULDUARACTIONCONTEXT_H
#define _PLAYERBOT_RAIDULDUARACTIONCONTEXT_H

#include "Action.h"
#include "NamedObjectContext.h"
#include "RaidUlduarActions.h"

class RaidUlduarActionContext : public NamedObjectContext<Action>
{
public:
    RaidUlduarActionContext()
    {
        creators["flame leviathan vehicle"] = &RaidUlduarActionContext::flame_leviathan_vehicle;
        creators["flame leviathan enter vehicle"] = &RaidUlduarActionContext::flame_leviathan_enter_vehicle;
        creators["ignis move construct to scorched ground"] = &RaidUlduarActionContext::ignis_move_construct_to_scorched_ground;
        creators["ignis move molten construct to water"] = &RaidUlduarActionContext::ignis_move_molten_construct_to_water;
    }

private:
    static Action* flame_leviathan_vehicle(PlayerbotAI* ai) { return new FlameLeviathanVehicleAction(ai); }
    static Action* flame_leviathan_enter_vehicle(PlayerbotAI* ai) { return new FlameLeviathanEnterVehicleAction(ai); }
    static Action* ignis_move_construct_to_scorched_ground(PlayerbotAI* ai) { return new IgnisMoveConstructToScorchedGroundAction(ai); }
    static Action* ignis_move_molten_construct_to_water(PlayerbotAI* ai) { return new IgnisMoveMoltenConstructToWaterAction(ai); }


};

#endif
