/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "MoveToSpellFocusAction.h"

#include <algorithm>

#include "GameObject.h"
#include "Log.h"
#include "Playerbots.h"
#include "SpellFocusMgr.h"
#include "Timer.h"

bool MoveToSpellFocusAction::Execute(Event /*event*/)
{
    ObjectGuid const focusGuid = AI_VALUE(ObjectGuid, "spell focus target");
    if (focusGuid.IsEmpty())
        return false;

    WorldObject* worldObject = botAI->GetWorldObject(focusGuid);
    GameObject* focus = worldObject ? worldObject->ToGameObject() : nullptr;
    if (!focus || !focus->isSpawned() || focus->GetGOInfo()->type != GAMEOBJECT_TYPE_SPELL_FOCUS)
    {
        // Far spawns are cached by position; their grid, and therefore the live
        // object, is not loaded until the bot gets closer.
        SpellFocusSpawn const* cached = sSpellFocusMgr.Get(focusGuid);
        if (!cached)
        {
            LOG_DEBUG("playerbots", "[spell focus] {} target {} is gone", bot->GetName(), focusGuid.ToString());
            context->GetValue<ObjectGuid>("spell focus target")->Set(ObjectGuid::Empty);
            return false;
        }

        float const dx = cached->pos.GetPositionX() - bot->GetPositionX();
        float const dy = cached->pos.GetPositionY() - bot->GetPositionY();
        float const dz = cached->pos.GetPositionZ() - bot->GetPositionZ();
        float const focusDistance = std::max(0.5f, cached->distance - SPELL_FOCUS_DISTANCE_BUFFER);
        if (dx * dx + dy * dy + dz * dz <= focusDistance * focusDistance)
        {
            // The grid is loaded at this distance. If the live object is still
            // missing, the spawn is phase-restricted or despawned; skip it for a
            // while instead of returning to the same empty spot.
            LOG_DEBUG("playerbots", "[spell focus] {} cached {} is absent; marking it unavailable", bot->GetName(),
                      cached->entry);
            sSpellFocusMgr.MarkUnavailable(cached->spawnId, 5 * MINUTE * IN_MILLISECONDS);
            context->GetValue<ObjectGuid>("spell focus target")->Set(ObjectGuid::Empty);
            return false;
        }

        LOG_DEBUG("playerbots", "[spell focus] {} moving to cached {} at {:.1f} yd", bot->GetName(), cached->entry,
                  std::sqrt(dx * dx + dy * dy + dz * dz));
        return MoveTo(cached->pos.GetMapId(), cached->pos.GetPositionX(), cached->pos.GetPositionY(),
                      cached->pos.GetPositionZ());
    }

    float const focusDistance =
        std::max(0.5f, float(focus->GetGOInfo()->spellFocus.dist) - SPELL_FOCUS_DISTANCE_BUFFER);
    if (focus->IsWithinDistInMap(bot, focusDistance))
    {
        LOG_DEBUG("playerbots", "[spell focus] {} reached {} (distance {})", bot->GetName(), focus->GetEntry(),
                  bot->GetDistance(focus));
        context->GetValue<ObjectGuid>("spell focus target")->Set(ObjectGuid::Empty);
        return true;
    }

    LOG_DEBUG("playerbots", "[spell focus] {} moving to {} (distance {})", bot->GetName(), focus->GetEntry(),
              bot->GetDistance(focus));
    return MoveTo(focus, std::max(0.2f, focusDistance - 0.5f));
}

bool MoveToSpellFocusAction::isUseful() { return !AI_VALUE(ObjectGuid, "spell focus target").IsEmpty(); }
