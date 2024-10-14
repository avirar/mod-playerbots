/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#ifndef _ITEMWEIGHTACTION_H_
#define _ITEMWEIGHTACTION_H_

#include "Action.h"
#include "Playerbots.h"

class ItemWeightAction : public Action
{
public:
    ItemWeightAction(PlayerbotAI* botAI) : Action(botAI, "weight item") {}

    virtual bool Execute(Event event) override;
};

#endif
