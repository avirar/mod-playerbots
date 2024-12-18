#ifndef _PLAYERBOT_RAIDULDUARACTIONS_H
#define _PLAYERBOT_RAIDULDUARACTIONS_H

#include "Action.h"
#include "AttackAction.h"
#include "GenericActions.h"
#include "GenericSpellActions.h"
#include "MovementActions.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RaidUlduarBossHelper.h"
#include "Vehicle.h"

class FlameLeviathanVehicleAction : public MovementAction
{
public:
    FlameLeviathanVehicleAction(PlayerbotAI* botAI) : MovementAction(botAI, "flame leviathan vehicle") {}
    bool Execute(Event event) override;

protected:
    bool MoveAvoidChasing(Unit* target);
    bool DemolisherAction(Unit* target);
    bool DemolisherTurretAction(Unit* target);
    bool SiegeEngineAction(Unit* target);
    bool SiegeEngineTurretAction(Unit* target);
    bool ChopperAction(Unit* target);
    Unit* GetAttacker();
    Unit* vehicleBase_;
    Vehicle* vehicle_;
    int avoidChaseIdx = -1;
};

class FlameLeviathanEnterVehicleAction : public MovementAction
{
public:
    FlameLeviathanEnterVehicleAction(PlayerbotAI* botAI) : MovementAction(botAI, "flame leviathan enter vehicle") {}
    bool Execute(Event event);

protected:
    bool EnterVehicle(Unit* vehicleBase, bool moveIfFar);
    bool ShouldEnter(Unit* vehicleBase);
    bool AllMainVehiclesOnUse();
};

class IgnisMoveConstructToScorchedGroundAction : public MovementAction
{
public:
    IgnisMoveConstructToScorchedGroundAction(PlayerbotAI* ai) : MovementAction(ai, "ignis move construct to scorched ground") {}
    bool Execute(Event event) override; // Executes the movement action
    bool isUseful() override;          // Determines if the action is needed
};

class IgnisMoveMoltenConstructToWaterAction : public MovementAction
{
public:
    IgnisMoveMoltenConstructToWaterAction(PlayerbotAI* ai) : MovementAction(ai, "ignis move molten construct to water") {}
    bool Execute(Event event) override;
    bool isUseful() override;
};


#endif
