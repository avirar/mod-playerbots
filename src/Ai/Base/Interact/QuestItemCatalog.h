/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_QUESTITEMCATALOG_H
#define PLAYERBOTS_QUESTITEMCATALOG_H

#include "InteractTypes.h"
#include <vector>

class Player;

// Compiles the bot's active quest list into usable quest-item candidates: each incomplete item
// objective whose item is in the bags and whose on-use spell is classified into an InteractKind.
// Per-bot, cheap (one pass over the quest log), intended for a throttled rebuild.
class QuestItemCatalog
{
public:
    static void Build(Player* bot, std::vector<QuestItemCandidate>& out);
};

#endif
