/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_CONSUMEBUFFACTION_H
#define PLAYERBOTS_CONSUMEBUFFACTION_H

#include <vector>

#include "UseItemAction.h"

class ConsumeBuffAction : public UseItemAction
{
public:
    ConsumeBuffAction(PlayerbotAI* botAI) : UseItemAction(botAI, "consume buff", true) {}

    bool Execute(Event event) override;
    bool isUseful() override;
    bool isPossible() override;

private:
    Item* _pendingBuffItem = nullptr;
    Item* PickBuffItem();
    Item* BestFlask(std::vector<Item*> const& items, bool allowNormalFlasks);
    Item* BestElixir(std::vector<Item*> const& items, bool battle);
    Item* BestScroll(std::vector<Item*> const& items);
};

#endif
