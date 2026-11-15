/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_LEARNRECIPEITEMSACTION_H
#define PLAYERBOTS_LEARNRECIPEITEMSACTION_H

#include "UseItemAction.h"

class LearnRecipeItemsAction : public UseItemAction
{
public:
    LearnRecipeItemsAction(PlayerbotAI* botAI) : UseItemAction(botAI, "learn recipe items") {}

    bool Execute(Event event) override;
    bool isPossible() override { return true; }
};

#endif
