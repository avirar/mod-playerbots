/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SpellFocusValue.h"

#include "CellImpl.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Player.h"
#include "Playerbots.h"
#include "SpellFocusMgr.h"

namespace
{
class NearestSpellFocusCheck
{
public:
    NearestSpellFocusCheck(WorldObject const& searcher, uint32 focusId, float range)
        : _searcher(searcher), _focusId(focusId), _range(range)
    {
    }

    bool operator()(GameObject* go)
    {
        if (go->GetGOInfo()->type != GAMEOBJECT_TYPE_SPELL_FOCUS || !go->isSpawned())
            return false;
        if (!go->InSamePhase(&_searcher))
            return false;
        if (go->GetGOInfo()->spellFocus.focusId != _focusId)
            return false;
        if (!go->IsWithinDistInMap(&_searcher, _range))
            return false;

        _range = _searcher.GetDistance(go);
        return true;
    }

private:
    WorldObject const& _searcher;
    uint32 _focusId;
    float _range;
};
}  // namespace

ObjectGuid SpellFocusValue::Calculate()
{
    uint32 const focusId = uint32(atoi(getQualifier().c_str()));
    if (!focusId)
        return ObjectGuid();

    // Player-created campfires are ordinary spawned focus GOs, so this search
    // finds them regardless of creator or faction. The cache below only covers
    // static DB spawns whose grid is not loaded yet.
    constexpr float SEARCH_RANGE = 100.0f;
    GameObject* focus = nullptr;
    NearestSpellFocusCheck check(*bot, focusId, SEARCH_RANGE);
    Acore::GameObjectLastSearcher<NearestSpellFocusCheck> searcher(bot, focus, check);
    Cell::VisitObjects(bot, searcher, SEARCH_RANGE);
    if (focus)
        return focus->GetGUID();

    constexpr float CACHE_RANGE = 900.0f;
    if (SpellFocusSpawn const* cached =
            sSpellFocusMgr.Nearest(focusId, WorldPosition(bot), CACHE_RANGE, bot->GetPhaseMask()))
        return ObjectGuid::Create<HighGuid::GameObject>(cached->entry, cached->spawnId);

    return ObjectGuid();
}
