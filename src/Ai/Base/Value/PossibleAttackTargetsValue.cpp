/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PossibleAttackTargetsValue.h"
#include "AttackersValue.h"
#include "Group.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"
#include <utility>

// Ported/adapted from cmangos playerbots strategy/values/PossibleAttackTargetsValue.cpp.
// Adaptation notes:
// - The OG "attackers" value is multi-qualified (AI_VALUE2(...,"attackers",1) getOne fast path);
//   the target's "attackers" is an unqualified threat-list GuidVector, so the getOne short-circuit
//   is dropped and the full list is filtered.
// - The OG IsValid re-checks threat victim / UNIT_FIELD_TARGET / attack-target / rti-target; the
//   target's "attackers" is already threat-driven, so those fallbacks are redundant and dropped.
// - AttackersValue::IsPossibleTarget is the target's AC-native "can attack" validator (visibility,
//   flags, school immunity, PvP-zone, critter, evade, tap/claim) and is used in place of the OG
//   IsValid/IsPossibleTarget pair.

GuidVector PossibleAttackTargetsValue::Calculate()
{
    GuidVector result;
    if (botAI->AllowActivity(ALL_ACTIVITY) && bot->IsInWorld() && !bot->IsBeingTeleported())
    {
        GuidVector attackers = AI_VALUE(GuidVector, "attackers");
        RemoveNonThreating(attackers, result);
    }

    return result;
}

void PossibleAttackTargetsValue::RemoveNonThreating(GuidVector const& targets, GuidVector& result)
{
    GuidVector breakableCC;
    GuidVector unBreakableCC;

    for (ObjectGuid const& guid : targets)
    {
        Unit* target = botAI->GetUnit(guid);
        if (!IsValid(target, bot, sPlayerbotAIConfig.sightDistance))
            continue;

        if (!HasIgnoreCCRti(target, bot) && HasBreakableCC(target, bot))
            breakableCC.push_back(guid);
        else if (!HasIgnoreCCRti(target, bot) && HasUnBreakableCC(target, bot))
            unBreakableCC.push_back(guid);
        else
            result.push_back(guid);
    }

    // If nothing is attackable, fall back to CC'd targets: prefer unbreakable CC (still controlled,
    // safe to hit) over breakable CC (hitting it breaks the CC).
    if (result.empty())
    {
        if (!unBreakableCC.empty())
            result = std::move(unBreakableCC);
        else if (!breakableCC.empty())
            result = std::move(breakableCC);
    }
}

bool PossibleAttackTargetsValue::IsValid(Unit* target, Player* player, float range)
{
    if (!target)
        return false;

    if (!AttackersValue::IsPossibleTarget(target, player))
        return false;

    // AttackersValue::IsPossibleTarget ignores its range parameter; enforce it here.
    if (!player->IsWithinDistInMap(target, range))
        return false;

    return true;
}

bool PossibleAttackTargetsValue::HasIgnoreCCRti(Unit* target, Player* player)
{
    Group* group = player->GetGroup();
    return group && (group->GetTargetIcon(RtiTargetValue::skullIndex) == target->GetGUID());
}

bool PossibleAttackTargetsValue::HasBreakableCC(Unit* target, Player* player)
{
    if (target->IsPolymorphed() || target->isFrozen())
        return true;

    PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
    if (ai)
    {
        if (ai->HasAura("sap", target))
            return true;

        if (ai->HasAura("gouge", target))
            return true;

        if (ai->HasAura("shackle undead", target))
            return true;
    }

    return false;
}

bool PossibleAttackTargetsValue::HasUnBreakableCC(Unit* target, Player* /*player*/)
{
    if (target->HasUnitState(UNIT_STATE_STUNNED) || target->HasUnitState(UNIT_STATE_FLEEING) ||
        target->HasUnitState(UNIT_STATE_ROOT))
        return true;

    return false;
}
