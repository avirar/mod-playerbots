/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SPELLFOCUSVALUE_H
#define PLAYERBOTS_SPELLFOCUSVALUE_H

#include "NamedObjectContext.h"
#include "ObjectGuid.h"
#include "Value.h"

class PlayerbotAI;

class SpellFocusValue : public CalculatedValue<ObjectGuid>, public Qualified
{
public:
    SpellFocusValue(PlayerbotAI* botAI, std::string const name = "spell focus")
        : CalculatedValue<ObjectGuid>(botAI, name, 2000)
    {
    }

    ObjectGuid Calculate() override;
};

#endif
