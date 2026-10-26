/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NAV3DTESTACTION_H
#define PLAYERBOTS_NAV3DTESTACTION_H

#include "Action.h"

class Nav3DTestAction : public Action
{
public:
    Nav3DTestAction(PlayerbotAI* botAI) : Action(botAI, "nav3d") {}

    bool Execute(Event event) override;
};

#endif
