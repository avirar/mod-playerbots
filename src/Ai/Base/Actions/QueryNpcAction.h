/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_QUERYNPCACTION_H
#define PLAYERBOTS_QUERYNPCACTION_H

#include "Action.h"

class PlayerbotAI;
class Creature;

// Dumps the server-side state of a creature relevant to questing: template and spawn data, runtime
// motion/stand state, the bot's relative perception, and the escort/quest bindings.
class QueryNpcAction : public Action
{
public:
    QueryNpcAction(PlayerbotAI* botAI, std::string const name = "query npc") : Action(botAI, name) {}

    bool Execute(Event event) override;

private:
    Creature* FindTarget(std::string const& param);
    void DumpNpc(Creature* npc);
};

#endif
