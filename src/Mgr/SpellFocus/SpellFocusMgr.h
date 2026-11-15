/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOT_SPELLFOCUSMGR_H
#define PLAYERBOT_SPELLFOCUSMGR_H

#include <unordered_map>
#include <vector>

#include "ObjectGuid.h"
#include "TravelMgr.h"

struct SpellFocusSpawn
{
    ObjectGuid::LowType spawnId{0};
    uint32 entry{0};
    uint32 focusId{0};
    float distance{0.0f};  // GameObjectTemplate::spellFocus.dist
    WorldPosition pos{};
};

/// Index of GAMEOBJECT_TYPE_SPELL_FOCUS spawns, built once from object manager
/// spawn data at world init. Immutable afterwards.
class SpellFocusMgr
{
public:
    static SpellFocusMgr& instance()
    {
        static SpellFocusMgr instance;
        return instance;
    }

    void Load();

    // Nearest cached spawn for the focus id on the same map, within maxRange.
    SpellFocusSpawn const* Nearest(uint32 focusId, WorldPosition const& from, float maxRange) const;

    // Cached spawn for a gameobject GUID, nullptr for runtime objects (campfires, ...).
    SpellFocusSpawn const* Get(ObjectGuid guid) const;

private:
    std::unordered_map<uint32 /*mapId*/, std::unordered_map<uint32 /*focusId*/, std::vector<SpellFocusSpawn>>> _spawns;
    std::unordered_map<ObjectGuid::LowType, SpellFocusSpawn> _bySpawnId;
};

#define sSpellFocusMgr SpellFocusMgr::instance()

#endif
