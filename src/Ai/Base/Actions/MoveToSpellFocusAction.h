/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_MOVETOSPELLFOCUSACTION_H
#define PLAYERBOTS_MOVETOSPELLFOCUSACTION_H

#include "NewRpgBaseAction.h"

class MoveToSpellFocusAction : public NewRpgBaseAction
{
public:
    MoveToSpellFocusAction(PlayerbotAI* botAI) : NewRpgBaseAction(botAI, "move to spell focus") {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

#endif
