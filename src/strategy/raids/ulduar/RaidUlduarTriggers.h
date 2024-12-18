#ifndef _PLAYERBOT_RAIDULDUARTRIGGERS_H
#define _PLAYERBOT_RAIDULDUARTRIGGERS_H

#include "EventMap.h"
#include "GenericTriggers.h"
#include "PlayerbotAIConfig.h"
#include "RaidUlduarBossHelper.h"
#include "Trigger.h"


class FlameLeviathanOnVehicleTrigger : public Trigger
{
public:
    FlameLeviathanOnVehicleTrigger(PlayerbotAI* ai) : Trigger(ai, "flame leviathan on vehicle") {}
    bool IsActive() override;
};

class FlameLeviathanVehicleNearTrigger : public Trigger
{
public:
    FlameLeviathanVehicleNearTrigger(PlayerbotAI* ai) : Trigger(ai, "flame leviathan vehicle near") {}
    bool IsActive() override;
};

class IgnisMoveConstructToScorchedGroundTrigger : public Trigger
{
public:
    IgnisMoveConstructToScorchedGroundTrigger(PlayerbotAI* ai) : Trigger(ai, "ignis move construct to scorched ground") {}
    bool IsActive() override;
};

class IgnisMoveMoltenConstructToWaterTrigger : public Trigger
{
public:
    IgnisMoveMoltenConstructToWaterTrigger(PlayerbotAI* ai) : Trigger(ai, "ignis move molten construct to water") {}
    bool IsActive() override;
};

class IgnisChooseTargetTrigger : public Trigger
{
public:
    IgnisChooseTargetTrigger(PlayerbotAI* ai) : Trigger(ai, "ignis choose target") {}
    bool IsActive() override;
};

class IgnisPositionTrigger : public Trigger
{
public:
    IgnisPositionTrigger(PlayerbotAI* ai) : Trigger(ai, "ignis position") {}
    bool IsActive() override;
};

#endif
