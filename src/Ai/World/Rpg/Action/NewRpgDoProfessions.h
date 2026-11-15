/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOT_NEWRPGDOPROFESSIONS_H
#define PLAYERBOT_NEWRPGDOPROFESSIONS_H

#include "NewRpgBaseAction.h"

class NewRpgDoProfessionsAction : public NewRpgBaseAction
{
public:
    NewRpgDoProfessionsAction(PlayerbotAI* botAI) : NewRpgBaseAction(botAI, "new rpg do professions") {}

    bool Execute(Event event) override;
};

#endif
