/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "MoveToSpellFocusAction.h"

#include "GameObject.h"
#include "Log.h"
#include "Playerbots.h"
#include "SpellFocusMgr.h"

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
        float const focusDistance = cached->distance - 1.0f;
        if (dx * dx + dy * dy + dz * dz <= focusDistance * focusDistance)
        {
            LOG_DEBUG("playerbots", "[spell focus] {} reached cached {} at {:.1f} yd", bot->GetName(), cached->entry,
                      std::sqrt(dx * dx + dy * dy + dz * dz));
            context->GetValue<ObjectGuid>("spell focus target")->Set(ObjectGuid::Empty);
            return true;
        }

        LOG_DEBUG("playerbots", "[spell focus] {} moving to cached {} at {:.1f} yd", bot->GetName(), cached->entry,
                  std::sqrt(dx * dx + dy * dy + dz * dz));
        return MoveTo(cached->pos.GetMapId(), cached->pos.GetPositionX(), cached->pos.GetPositionY(),
                      cached->pos.GetPositionZ());
    }

    float const focusDistance = float(focus->GetGOInfo()->spellFocus.dist) - 1.0f;
    if (focus->IsWithinDistInMap(bot, focusDistance))
    {
        LOG_DEBUG("playerbots", "[spell focus] {} reached {} (distance {})", bot->GetName(), focus->GetEntry(),
                  bot->GetDistance(focus));
        context->GetValue<ObjectGuid>("spell focus target")->Set(ObjectGuid::Empty);
        return true;
    }

    LOG_DEBUG("playerbots", "[spell focus] {} moving to {} (distance {})", bot->GetName(), focus->GetEntry(),
              bot->GetDistance(focus));
    return MoveTo(focus, 3.0f);
}

bool MoveToSpellFocusAction::isUseful() { return !AI_VALUE(ObjectGuid, "spell focus target").IsEmpty(); }
