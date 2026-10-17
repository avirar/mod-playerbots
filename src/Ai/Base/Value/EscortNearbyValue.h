/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ESCORTNEARBYVALUE_H
#define PLAYERBOTS_ESCORTNEARBYVALUE_H

#include "Value.h"

class PlayerbotAI;

// True while the bot is following a live escort NPC for its active do-quest. Cached: the escortee
// search walks the nearby NPC caches.
class EscortNearbyValue : public BoolCalculatedValue
{
public:
    EscortNearbyValue(PlayerbotAI* botAI) : BoolCalculatedValue(botAI, "escort npc nearby", 500) {}

    bool Calculate() override;
};

// True while the escorted NPC is under attack: the one case where the bot must be free to fight.
class EscortAttackedValue : public BoolCalculatedValue
{
public:
    EscortAttackedValue(PlayerbotAI* botAI) : BoolCalculatedValue(botAI, "escort npc attacked", 500) {}

    bool Calculate() override;
};

#endif
