/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NEWRPGMULTIPLIERS_H
#define PLAYERBOTS_NEWRPGMULTIPLIERS_H

#include "Multiplier.h"

class NewRpgQuestGiverMultiplier : public Multiplier
{
public:
    NewRpgQuestGiverMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "new rpg quest giver") {}

    float GetValue(Action* action) override;
};

// Keeps an escorting bot on its leash: no loot/gather wandering, and attacks only while the
// escorted NPC is actually under attack.
class NewRpgEscortMultiplier : public Multiplier
{
public:
    NewRpgEscortMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "new rpg escort") {}

    float GetValue(Action* action) override;
};

#endif
