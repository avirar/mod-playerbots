/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SpellFocusMgr.h"

#include <algorithm>
#include <mutex>

#include "CreatureData.h"
#include "DataStores/DBCStores.h"
#include "GameObject.h"
#include "Log.h"
#include "MapMgr.h"
#include "Object.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "Timer.h"
#include "Unit.h"

namespace
{
// Same range as the live focus search in SpellFocusValue: creatures this close to a
// focus count for the spawn's level and safety classification.
constexpr float SAFE_RADIUS = 100.0f;
// A focus outside the bot's zone is only walked to when nearby creatures are at or
// below this level slack, as in the TravelMgr quest POI gate.
constexpr int32 NEARBY_LEVEL_SLACK = 4;

// The bot's live reaction to a faction, mirroring Unit::GetFactionReactionTo(fte,
// unit): forced rank (at war, auras), then reputation when the faction has a
// reputation list, otherwise the static faction table.
ReputationRank GetBotReactionTo(Player const* bot, FactionTemplateEntry const* factionTemplateEntry)
{
    if (!factionTemplateEntry)
        return REP_NEUTRAL;

    ReputationMgr const& repMgr = bot->GetReputationMgr();
    if (ReputationRank const* forcedRank = repMgr.GetForcedRankIfAny(factionTemplateEntry))
        return *forcedRank;

    if (FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionTemplateEntry->faction))
    {
        if (factionEntry->CanHaveReputation())
        {
            ReputationRank const rank = repMgr.GetRank(factionEntry);
            return repMgr.IsAtWar(factionEntry) ? std::min(REP_NEUTRAL, rank) : rank;
        }
    }

    return Unit::GetFactionReactionTo(bot->GetFactionTemplateEntry(), factionTemplateEntry);
}

// A spawn is safe when no creature stands within SAFE_RADIUS (empty area) or when
// at least one nearby creature is not hostile to this bot.
bool IsSafeFor(SpellFocusSpawn const& spawn, Player const* bot)
{
    if (spawn.nearbyFactions.empty())
        return true;

    for (uint32 const factionTemplateId : spawn.nearbyFactions)
        if (GetBotReactionTo(bot, sFactionTemplateStore.LookupEntry(factionTemplateId)) >= REP_NEUTRAL)
            return true;

    return false;
}
}  // namespace

void SpellFocusMgr::Load()
{
    uint32 oldMSTime = getMSTime();
    _spawns.clear();
    _bySpawnId.clear();
    _unavailableUntil.clear();

    // Static creature spawns per map, used to classify the focus spawns below: a focus
    // next to high-level or hostile creatures must not become a long-distance target.
    struct NearbyCreature
    {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
        uint8 maxLevel{0};
        uint32 factionTemplate{0};  // 0 = no faction (neutral)
    };
    std::unordered_map<uint32, std::vector<NearbyCreature>> creaturesByMap;
    for (auto const& [spawnGuid, creatureSpawn] : sObjectMgr->GetAllCreatureData())
    {
        CreatureTemplate const* creatureTemplate = sObjectMgr->GetCreatureTemplate(creatureSpawn.id);
        if (!creatureTemplate)
            continue;

        FactionTemplateEntry const* factionTemplateEntry = sFactionTemplateStore.LookupEntry(creatureTemplate->faction);
        uint32 const factionTemplate = factionTemplateEntry ? factionTemplateEntry->ID : 0;
        NearbyCreature creature{creatureSpawn.posX, creatureSpawn.posY, creatureSpawn.posZ,
                                creatureTemplate->maxlevel, factionTemplate};
        creaturesByMap[creatureSpawn.mapid].push_back(creature);
    }

    uint32 guardedSpawns = 0;
    for (auto const& [spawnId, goData] : sObjectMgr->GetAllGOData())
    {
        GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(goData.id);
        if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_SPELL_FOCUS || !goInfo->spellFocus.focusId)
            continue;

        SpellFocusSpawn spawn;
        spawn.spawnId = spawnId;
        spawn.entry = goData.id;
        spawn.focusId = goInfo->spellFocus.focusId;
        spawn.phaseMask = goData.phaseMask;
        spawn.distance = float(goInfo->spellFocus.dist);
        spawn.pos = WorldPosition(goData.mapid, goData.posX, goData.posY, goData.posZ, goData.orientation);
        spawn.zoneId = sMapMgr->GetZoneId(goData.phaseMask ? goData.phaseMask : PHASEMASK_NORMAL, goData.mapid,
                                          goData.posX, goData.posY, goData.posZ);

        if (auto const creatures = creaturesByMap.find(goData.mapid); creatures != creaturesByMap.end())
        {
            float const safeRadiusSq = SAFE_RADIUS * SAFE_RADIUS;
            for (NearbyCreature const& creature : creatures->second)
            {
                float const dx = creature.x - goData.posX;
                float const dy = creature.y - goData.posY;
                float const dz = creature.z - goData.posZ;
                if (dx * dx + dy * dy + dz * dz > safeRadiusSq)
                    continue;

                spawn.maxNearbyLevel = std::max(spawn.maxNearbyLevel, creature.maxLevel);
                if (creature.factionTemplate)
                    spawn.nearbyFactions.push_back(creature.factionTemplate);
            }
            if (!spawn.nearbyFactions.empty())
            {
                std::sort(spawn.nearbyFactions.begin(), spawn.nearbyFactions.end());
                spawn.nearbyFactions.erase(std::unique(spawn.nearbyFactions.begin(), spawn.nearbyFactions.end()),
                                           spawn.nearbyFactions.end());
                ++guardedSpawns;
            }
        }

        _spawns[goData.mapid][spawn.focusId].push_back(spawn);
        _bySpawnId[spawnId] = spawn;
    }

    LOG_INFO("server.loading", ">> Loaded {} spell focus spawns ({} guarded by nearby creatures) in {} ms",
             _bySpawnId.size(), guardedSpawns, GetMSTimeDiffToNow(oldMSTime));
}

SpellFocusSpawn const* SpellFocusMgr::Nearest(uint32 focusId, WorldPosition const& from, float maxRange,
                                              Player const* bot) const
{
    if (!bot)
        return nullptr;

    auto mapIt = _spawns.find(from.GetMapId());
    if (mapIt == _spawns.end())
        return nullptr;

    auto focusIt = mapIt->second.find(focusId);
    if (focusIt == mapIt->second.end())
        return nullptr;

    uint32 const phaseMask = bot->GetPhaseMask();
    uint32 const botZoneId = bot->GetZoneId();
    int32 const levelLimit = int32(bot->GetLevel()) + NEARBY_LEVEL_SLACK;

    uint32 const now = getMSTime();
    std::shared_lock<std::shared_mutex> lock(_unavailableMutex);
    float bestDistanceSq = maxRange * maxRange;
    SpellFocusSpawn const* best = nullptr;
    for (SpellFocusSpawn const& spawn : focusIt->second)
    {
        if (phaseMask && !(spawn.phaseMask & phaseMask))
            continue;

        auto unavailable = _unavailableUntil.find(spawn.spawnId);
        if (unavailable != _unavailableUntil.end() && unavailable->second > now)
            continue;

        // Only walk long distances to places the bot can survive: same zone as the bot
        // (or no nearby creatures above its level), and no hostile creatures nearby.
        if (spawn.zoneId != botZoneId && int32(spawn.maxNearbyLevel) > levelLimit)
            continue;
        if (!IsSafeFor(spawn, bot))
            continue;

        float const dx = spawn.pos.GetPositionX() - from.GetPositionX();
        float const dy = spawn.pos.GetPositionY() - from.GetPositionY();
        float const dz = spawn.pos.GetPositionZ() - from.GetPositionZ();
        float const distanceSq = dx * dx + dy * dy + dz * dz;
        if (distanceSq > bestDistanceSq)
            continue;

        bestDistanceSq = distanceSq;
        best = &spawn;
    }

    return best;
}

void SpellFocusMgr::MarkUnavailable(ObjectGuid::LowType spawnId, uint32 durationMs)
{
    std::unique_lock<std::shared_mutex> lock(_unavailableMutex);
    _unavailableUntil[spawnId] = getMSTime() + durationMs;
}

SpellFocusSpawn const* SpellFocusMgr::Get(ObjectGuid guid) const
{
    auto it = _bySpawnId.find(guid.GetCounter());
    return it == _bySpawnId.end() ? nullptr : &it->second;
}
