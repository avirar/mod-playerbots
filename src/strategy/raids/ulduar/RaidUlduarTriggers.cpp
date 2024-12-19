#include "RaidUlduarTriggers.h"

#include "EventMap.h"
#include "Object.h"
#include "Playerbots.h"
#include "RaidUlduarScripts.h"
#include "ScriptedCreature.h"
#include "SharedDefines.h"
#include "Trigger.h"
#include "Vehicle.h"

const std::vector<uint32> availableVehicles = {NPC_VEHICLE_CHOPPER, NPC_SALVAGED_DEMOLISHER,
                                               NPC_SALVAGED_DEMOLISHER_TURRET, NPC_SALVAGED_SIEGE_ENGINE,
                                               NPC_SALVAGED_SIEGE_ENGINE_TURRET};

bool FlameLeviathanOnVehicleTrigger::IsActive()
{
    Unit* vehicleBase = bot->GetVehicleBase();
    Vehicle* vehicle = bot->GetVehicle();
    if (!vehicleBase || !vehicle)
        return false;

    uint32 entry = vehicleBase->GetEntry();
    for (uint32 comp : availableVehicles)
    {
        if (entry == comp)
            return true;
    }
    return false;
}

bool FlameLeviathanVehicleNearTrigger::IsActive()
{
    if (bot->GetVehicle())
        return false;
    
    Player* master = botAI->GetMaster();
    if (!master)
        return false;
    
    if (!master->GetVehicle())
        return false;

    return true;
}

bool IgnisMoveConstructToScorchedGroundTrigger::IsActive()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "ignis the furnace master");
    if (!boss) 
        return false;

    if (botAI->IsTank(bot) && !botAI->IsMainTank(bot))
    {
        GuidVector attackers = AI_VALUE(GuidVector, "nearest hostile npcs");
        for (ObjectGuid guid : attackers)
        {
            Unit* target = botAI->GetUnit(guid);
            if (target && target->GetEntry() == NPC_IRON_CONSTRUCT &&
                target->GetVictim() == bot && !target->HasAura(SPELL_MOLTEN))
            {
                // Ensure scorched ground exists and bot is far from it
                GuidVector nearbyGround = AI_VALUE(GuidVector, "nearest hostile npcs");
                for (ObjectGuid groundGuid : nearbyGround)
                {
                    Unit* scorchedGround = botAI->GetUnit(groundGuid);
                    if (scorchedGround && scorchedGround->GetEntry() == NPC_SCORCHED_GROUND &&
                        bot->GetDistance(scorchedGround->GetPosition()) > 2.0f)
                    {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool IgnisMoveMoltenConstructToWaterTrigger::IsActive()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "ignis the furnace master");
    if (!boss)
        return false;

    if (botAI->IsTank(bot) && !botAI->IsMainTank(bot))
    {
        GuidVector attackers = AI_VALUE(GuidVector, "nearest hostile npcs");
        for (ObjectGuid guid : attackers)
        {
            Unit* target = botAI->GetUnit(guid);
            if (target && target->GetEntry() == NPC_IRON_CONSTRUCT &&
                target->GetVictim() == bot && target->HasAura(SPELL_MOLTEN)) // Target is molten
            {
                // Check distance to water pools
                float distToNorth = bot->GetDistance2d(WATER_CENTER_NORTH_X, WATER_CENTER_NORTH_Y);
                float distToSouth = bot->GetDistance2d(WATER_CENTER_SOUTH_X, WATER_CENTER_SOUTH_Y);

                // Action is useful if the bot is farther than the radius from the closest water pool
                if (distToNorth > WATER_RADIUS && distToSouth > WATER_RADIUS)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

bool IgnisChooseTargetTrigger::IsActive()
{
    // Ensure there are valid targets to process
    Unit* boss = AI_VALUE2(Unit*, "find target", "ignis the furnace master");
    if (!boss)
        return false;
    
    GuidVector attackers = AI_VALUE(GuidVector, "nearest hostile npcs");
    for (GuidVector::iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* unit = botAI->GetUnit(*i);
        if (!unit || !unit->IsAlive())
            continue;

        // Useful if there are constructs or the boss is present
        if ((unit->GetEntry() == NPC_IRON_CONSTRUCT || boss))
        {
            return true;
        }
    }
    return false;
}


bool IgnisPositionTrigger::IsActive()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "ignis the furnace master");
    if (!boss)
        return false;
    
    // Main tank positioning
    if (botAI->IsMainTank(bot))
    {
        float distance = bot->GetDistance2d(IGNIS_ARENA_CENTER_X, IGNIS_ARENA_CENTER_Y);
        return distance > 15.0f; // Positioning is useful if the tank is outside the 10-yard radius
    }

    // Ranged DPS positioning
    if (botAI->IsRanged(bot))
    {
        float distance = bot->GetDistance2d(IGNIS_ARENA_CENTER_X, IGNIS_ARENA_CENTER_Y + 30.0f);
        return distance > 20.0f; // Positioning is useful if ranged DPS are outside the 10-yard radius
    }

    return false; // No positioning required for other roles
}
