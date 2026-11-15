/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SpellFocusMgr.h"

#include "GameObject.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Timer.h"

void SpellFocusMgr::Load()
{
    uint32 oldMSTime = getMSTime();
    _spawns.clear();
    _bySpawnId.clear();

    for (auto const& [spawnId, goData] : sObjectMgr->GetAllGOData())
    {
        GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(goData.id);
        if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_SPELL_FOCUS || !goInfo->spellFocus.focusId)
            continue;

        SpellFocusSpawn spawn;
        spawn.spawnId = spawnId;
        spawn.entry = goData.id;
        spawn.focusId = goInfo->spellFocus.focusId;
        spawn.distance = float(goInfo->spellFocus.dist);
        spawn.pos = WorldPosition(goData.mapid, goData.posX, goData.posY, goData.posZ, goData.orientation);

        _spawns[goData.mapid][spawn.focusId].push_back(spawn);
        _bySpawnId[spawnId] = spawn;
    }

    LOG_INFO("server.loading", ">> Loaded {} spell focus spawns in {} ms", _bySpawnId.size(),
             GetMSTimeDiffToNow(oldMSTime));
}

SpellFocusSpawn const* SpellFocusMgr::Nearest(uint32 focusId, WorldPosition const& from, float maxRange) const
{
    auto mapIt = _spawns.find(from.GetMapId());
    if (mapIt == _spawns.end())
        return nullptr;

    auto focusIt = mapIt->second.find(focusId);
    if (focusIt == mapIt->second.end())
        return nullptr;

    float bestDistanceSq = maxRange * maxRange;
    SpellFocusSpawn const* best = nullptr;
    for (SpellFocusSpawn const& spawn : focusIt->second)
    {
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

SpellFocusSpawn const* SpellFocusMgr::Get(ObjectGuid guid) const
{
    auto it = _bySpawnId.find(guid.GetCounter());
    return it == _bySpawnId.end() ? nullptr : &it->second;
}
