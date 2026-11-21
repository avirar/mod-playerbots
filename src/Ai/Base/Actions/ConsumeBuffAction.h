/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_CONSUMEBUFFACTION_H
#define PLAYERBOTS_CONSUMEBUFFACTION_H

#include "UseItemAction.h"

class ConsumeBuffAction : public UseItemAction
{
public:
    ConsumeBuffAction(PlayerbotAI* botAI) : UseItemAction(botAI, "consume buff", true) {}

    bool Execute(Event event) override;
    bool isUseful() override;
    bool isPossible() override;

private:
    Item* PickBuffItem();
    Item* BestFlask(bool allowNormalFlasks);
    Item* BestElixir(bool battle);
    Item* BestScroll();
};

#endif
