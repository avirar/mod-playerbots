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
