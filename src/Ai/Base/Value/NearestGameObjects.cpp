/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "NearestGameObjects.h"

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "SpellMgr.h"

GuidVector NearestGameObjects::Calculate()
{
    std::list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck u_check(bot, range);
    Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);

    GuidVector result;
    for (GameObject* go : targets)
    {
        // if (ignoreLos || bot->IsWithinLOSInMap(go))
        result.push_back(go->GetGUID());
    }

    return result;
}

GuidVector NearestTrapWithDamageValue::Calculate()
{
    std::list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck u_check(bot, range);
    Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);

    GuidVector result;
    for (GameObject* go : targets)
    {
        GameobjectTypes goType = go->GetGoType();

        // Check if this is a GameObject type that can deal damage
        // Type 6 = TRAP, Type 10 = GOOBER, Type 22 = SPELLCASTER, Type 30 = AURA_GENERATOR
        if (goType != GAMEOBJECT_TYPE_TRAP &&
            goType != GAMEOBJECT_TYPE_GOOBER &&
            goType != GAMEOBJECT_TYPE_SPELLCASTER &&
            goType != GAMEOBJECT_TYPE_AURA_GENERATOR)
        {
            continue;
        }

        Unit* owner = go->GetOwner();
        if (owner && owner->IsFriendlyTo(bot))
        {
            continue;
        }

        const GameObjectTemplate* goInfo = go->GetGOInfo();
        if (!goInfo)
        {
            continue;
        }

        // Extract spell ID based on GameObject type
        std::vector<uint32> spellIds;

        switch (goType)
        {
            case GAMEOBJECT_TYPE_TRAP:
                if (goInfo->trap.spellId > 0)
                    spellIds.push_back(goInfo->trap.spellId);
                break;

            case GAMEOBJECT_TYPE_GOOBER:
                if (goInfo->goober.spellId > 0)
                    spellIds.push_back(goInfo->goober.spellId);
                break;

            case GAMEOBJECT_TYPE_SPELLCASTER:
                if (goInfo->spellcaster.spellId > 0)
                    spellIds.push_back(goInfo->spellcaster.spellId);
                break;

            case GAMEOBJECT_TYPE_AURA_GENERATOR:
                // AURA_GENERATOR can have up to 2 auras
                if (goInfo->auraGenerator.auraID1 > 0)
                    spellIds.push_back(goInfo->auraGenerator.auraID1);
                if (goInfo->auraGenerator.auraID2 > 0)
                    spellIds.push_back(goInfo->auraGenerator.auraID2);
                break;

            default:
                continue;
        }

        if (spellIds.empty())
        {
            continue;
        }

        // Check each spell to see if it deals damage
        bool isDamaging = false;
        for (uint32 spellId : spellIds)
        {
            const SpellInfo* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!spellInfo || spellInfo->IsPositive())
            {
                continue;
            }

            for (int i = 0; i < MAX_SPELL_EFFECTS; i++)
            {
                if (spellInfo->Effects[i].Effect == SPELL_EFFECT_APPLY_AURA)
                {
                    if (spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE)
                    {
                        isDamaging = true;
                        break;
                    }
                }
                else if (spellInfo->Effects[i].Effect == SPELL_EFFECT_SCHOOL_DAMAGE)
                {
                    isDamaging = true;
                    break;
                }
            }

            if (isDamaging)
                break;
        }

        if (isDamaging)
        {
            result.push_back(go->GetGUID());
        }
    }
    return result;
}
