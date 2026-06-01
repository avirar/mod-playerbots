#include "NewRpgInfo.h"

#include <cmath>

#include "Timer.h"

void NewRpgInfo::ChangeToGoGrind(WorldPosition pos)
{
    Reset();
    status = RPG_GO_GRIND;
    go_grind = GoGrind();
    go_grind.pos = pos;
}

void NewRpgInfo::ChangeToGoCamp(WorldPosition pos)
{
    Reset();
    status = RPG_GO_CAMP;
    go_camp = GoCamp();
    go_camp.pos = pos;
}

void NewRpgInfo::ChangeToWanderNpc()
{
    Reset();
    status = RPG_WANDER_NPC;
    wander_npc = WanderNpc();
}

void NewRpgInfo::ChangeToWanderRandom()
{
    Reset();
    status = RPG_WANDER_RANDOM;
    WANDER_RANDOM = WanderRandom();
}

void NewRpgInfo::ChangeToDoQuest(uint32 questId, const Quest* quest)
{
    Reset();
    status = RPG_DO_QUEST;
    do_quest = DoQuest();
    do_quest.questId = questId;
    do_quest.quest = quest;
}

void NewRpgInfo::ChangeToTravelFlight(ObjectGuid fromFlightMaster, uint32 fromNode, uint32 toNode)
{
    Reset();
    status = RPG_TRAVEL_FLIGHT;
    flight = TravelFlight();
    flight.fromFlightMaster = fromFlightMaster;
    flight.fromNode = fromNode;
    flight.toNode = toNode;
}

  void NewRpgInfo::ChangeToOutdoorPvp(ObjectGuid::LowType capturePointSpawnId)
{
    Reset();
    status = RPG_OUTDOOR_PVP;
    outdoor_pvp = OutdoorPvP();
    outdoor_pvp.capturePointSpawnId = capturePointSpawnId;
}

void NewRpgInfo::ChangeToRest()
{
    Reset();
    status = RPG_REST;
    rest = Rest();
}

void NewRpgInfo::ChangeToIdle()
{
    Reset();
    status = RPG_IDLE;
}

bool NewRpgInfo::CanChangeTo(NewRpgStatus status) { return true; }

void NewRpgInfo::Reset()
{
    auto savedNpcVisits = std::move(ignoredRpgNpcs);
    auto savedDistrictVisits = std::move(recentDistrictVisits);
    uint32 savedDistrictId = currentDistrictId;
    WorldPosition savedDistrictCenter = currentDistrictCenter;
    auto savedCityDistricts = std::move(cityDistricts);
    uint32 savedLastCityDiscovery = lastCityDiscovery;

    *this = NewRpgInfo();
    startT = getMSTime();

    ignoredRpgNpcs = std::move(savedNpcVisits);
    recentDistrictVisits = std::move(savedDistrictVisits);
    currentDistrictId = savedDistrictId;
    currentDistrictCenter = savedDistrictCenter;
    cityDistricts = std::move(savedCityDistricts);
    lastCityDiscovery = savedLastCityDiscovery;
}

void NewRpgInfo::SetMoveFarTo(WorldPosition pos)
{
    nearestMoveFarDis = FLT_MAX;
    stuckTs = 0;
    stuckAttempts = 0;
    moveFarPos = pos;
}

std::string NewRpgInfo::ToString()
{
    std::stringstream out;
    out << "Status: ";
    switch (status)
    {
        case RPG_GO_GRIND:
            out << "GO_GRIND";
            out << "\nGrindPos: " << go_grind.pos.GetMapId() << " " << go_grind.pos.GetPositionX() << " "
                << go_grind.pos.GetPositionY() << " " << go_grind.pos.GetPositionZ();
            out << "\nlastGoGrind: " << startT;
            break;
        case RPG_GO_CAMP:
            out << "GO_CAMP";
            out << "\nCampPos: " << go_camp.pos.GetMapId() << " " << go_camp.pos.GetPositionX() << " "
                << go_camp.pos.GetPositionY() << " " << go_camp.pos.GetPositionZ();
            out << "\nlastGoCamp: " << startT;
            break;
        case RPG_WANDER_NPC:
            out << "WANDER_NPC";
            out << "\nnpcOrGoEntry: " << wander_npc.npcOrGo.GetCounter();
            out << "\nlastWanderNpc: " << startT;
            out << "\nlastReachNpcOrGo: " << wander_npc.lastReach;
            break;
        case RPG_WANDER_RANDOM:
            out << "WANDER_RANDOM";
            out << "\nlastWanderRandom: " << startT;
            break;
        case RPG_IDLE:
            out << "IDLE";
            break;
        case RPG_REST:
            out << "REST";
            out << "\nlastRest: " << startT;
            break;
        case RPG_DO_QUEST:
            out << "DO_QUEST";
            out << "\nquestId: " << do_quest.questId;
            out << "\nobjectiveIdx: " << do_quest.objectiveIdx;
            out << "\npoiPos: " << do_quest.pos.GetMapId() << " " << do_quest.pos.GetPositionX() << " "
                << do_quest.pos.GetPositionY() << " " << do_quest.pos.GetPositionZ();
            out << "\nlastReachPOI: " << do_quest.lastReachPOI ? GetMSTimeDiffToNow(do_quest.lastReachPOI) : 0;
            break;
        case RPG_TRAVEL_FLIGHT:
            out << "TRAVEL_FLIGHT";
            out << "\nfromFlightMaster: " << flight.fromFlightMaster.GetEntry();
            out << "\nfromNode: " << flight.fromNode;
            out << "\ntoNode: " << flight.toNode;
            out << "\ninFlight: " << flight.inFlight;
            break;
        case RPG_OUTDOOR_PVP:
            out << "OUTDOOR_PVP";
            if (!outdoor_pvp.capturePointSpawnId)
                out << "\nNo capture point assigned.";
            else
                out << "\ncapturePointSpawnId: " << outdoor_pvp.capturePointSpawnId;
            break;
        default:
            out << "UNKNOWN";
    }
    return out.str();
}

void NewRpgInfo::PruneOldVisits(uint32 expirationTimeMs)
{
    uint32 now = getMSTime();
    for (auto it = ignoredRpgNpcs.begin(); it != ignoredRpgNpcs.end(); )
    {
        if (getMSTimeDiff((uint32)it->second, (uint32)now) > expirationTimeMs)
            it = ignoredRpgNpcs.erase(it);
        else
            ++it;
    }
}

void NewRpgInfo::PruneDistrictVisits(uint32 cooldownMs, uint32 maxVisits)
{
    uint32 now = getMSTime();
    for (auto it = recentDistrictVisits.begin(); it != recentDistrictVisits.end(); )
    {
        if (getMSTimeDiff(it->lastVisitTs, now) > cooldownMs)
            it = recentDistrictVisits.erase(it);
        else
            ++it;
    }
    // Keep only the most recent maxVisits entries
    while (recentDistrictVisits.size() > maxVisits)
        recentDistrictVisits.erase(recentDistrictVisits.begin());
}

void NewRpgInfo::RecordDistrictVisit(uint32 areaId, uint32 npcsVisited, uint32 npcsTotal)
{
    // Remove existing entry for same area if present
    for (auto it = recentDistrictVisits.begin(); it != recentDistrictVisits.end(); ++it)
    {
        if (it->areaId == areaId)
        {
            recentDistrictVisits.erase(it);
            break;
        }
    }
    recentDistrictVisits.push_back(DistrictVisit{areaId, getMSTime(), npcsVisited, npcsTotal});
}

DistrictVisit* NewRpgInfo::GetCurrentDistrictVisit()
{
    for (auto& dv : recentDistrictVisits)
    {
        if (dv.areaId == currentDistrictId)
            return &dv;
    }
    // Create new entry
    recentDistrictVisits.push_back(DistrictVisit{currentDistrictId, getMSTime(), 0, 0});
    return &recentDistrictVisits.back();
}
