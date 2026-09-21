/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_POSSIBLEATTACKTARGETSVALUE_H
#define PLAYERBOTS_POSSIBLEATTACKTARGETSVALUE_H

#include "PlayerbotAIConfig.h"
#include "Value.h"

class Player;
class PlayerbotAI;
class Unit;

// List of hostile targets that are in combat with the bot (or bot group) and can actually be
// attacked (reachable, not CC'ed in a way that hitting them would break, etc.).
// Ported/adapted from cmangos playerbots strategy/values/PossibleAttackTargetsValue.{h,cpp}.
class PossibleAttackTargetsValue : public ObjectGuidListCalculatedValue
{
public:
    PossibleAttackTargetsValue(PlayerbotAI* botAI)
        : ObjectGuidListCalculatedValue(botAI, "possible attack targets", 2)
    {
    }

    GuidVector Calculate() override;

    static bool IsValid(Unit* target, Player* player, float range = sPlayerbotAIConfig.sightDistance);
    static bool HasBreakableCC(Unit* target, Player* player);
    static bool HasUnBreakableCC(Unit* target, Player* player);
    static bool HasIgnoreCCRti(Unit* target, Player* player);

private:
    void RemoveNonThreating(GuidVector const& targets, GuidVector& result);
};

#endif
