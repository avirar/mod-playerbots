/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "PaladinTriggers.h"
#include "BlessingManager.h"

#include "PaladinActions.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

bool SealTrigger::IsActive()
{
    Unit* target = GetTarget();
    return !botAI->HasAura("seal of justice", target) && !botAI->HasAura("seal of command", target) &&
           !botAI->HasAura("seal of vengeance", target) && !botAI->HasAura("seal of corruption", target) &&
           !botAI->HasAura("seal of righteousness", target) && !botAI->HasAura("seal of light", target) &&
           (!botAI->HasAura("seal of wisdom", target) || AI_VALUE2(uint8, "mana", "self target") > 70);
}

bool CrusaderAuraTrigger::IsActive()
{
    Unit* target = GetTarget();
    return AI_VALUE2(bool, "mounted", "self target") && !botAI->HasAura("crusader aura", target);
}

// Helper function to check target for any existing blessing from the paladin
bool HasBlessing(PlayerbotAI* botAI, Unit* target)
{
    if (!target)
        return false;

    static const std::vector<std::string> blessings = {
        "blessing of might", "blessing of wisdom",
        "blessing of kings", "blessing of sanctuary",
        "greater blessing of might", "greater blessing of wisdom",
        "greater blessing of kings", "greater blessing of sanctuary"
    };

    for (const auto& blessing : blessings)
    {
        if (botAI->HasAura(blessing, target, false, true))
            return true;
    }
    return false;
}

bool BlessingTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!target || HasBlessing(botAI, target))
        return false;

    return SpellTrigger::IsActive();
}

bool BlessingOfKingsOnPartyTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!target || HasBlessing(botAI, target))
        return false;

    return SpellTrigger::IsActive();
}

bool BlessingOfWisdomOnPartyTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!target || HasBlessing(botAI, target))
        return false;

    return SpellTrigger::IsActive();
}

bool BlessingOfMightOnPartyTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!target || HasBlessing(botAI, target))
        return false;

    return SpellTrigger::IsActive();
}

bool BlessingOfSanctuaryOnPartyTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!target || HasBlessing(botAI, target))
        return false;

    return SpellTrigger::IsActive();
}

bool BlessingOnPartyTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!target || HasBlessing(botAI, target))
        return false;

    return SpellTrigger::IsActive();
}

bool CastGreaterBlessingTrigger::IsActive()
{
    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsAlive())
        return false;

    // Check if the bot has the required item (Symbol of Kings)
    if (!bot->HasItemCount(21177, 1))
        return false;

    Group* group = bot->GetGroup();
    if (!group)
        return false;
/*
    // Require at least 6 total members (the bot plus 5 others), or an explicit raid check
    if (group->GetMembersCount() <= 5 && !group->isRaidGroup())
        return false;
*/
    // Get the list of assigned blessings
    auto assignment = AssignBlessingsForGroup(botAI);
    auto paladinIt = assignment.find(bot);
    if (paladinIt == assignment.end())
        return false; // No blessings assigned

    for (auto& kv : paladinIt->second)
    {
        uint8 classId = kv.first;
        GreaterBlessingType gBlessing = kv.second;

        // Manually assign spell names (instead of calling GetGreaterBlessingSpellName)
        std::string blessingSpell;
        std::string auraName;

        switch (gBlessing)
        {
            case GREATER_BLESSING_OF_MIGHT:
                blessingSpell = "greater blessing of might";
                auraName = "greater blessing of might";
                break;
            case GREATER_BLESSING_OF_WISDOM:
                blessingSpell = "greater blessing of wisdom";
                auraName = "greater blessing of wisdom";
                break;
            case GREATER_BLESSING_OF_KINGS:
                blessingSpell = "greater blessing of kings";
                auraName = "greater blessing of kings";
                break;
            case GREATER_BLESSING_OF_SANCTUARY:
                blessingSpell = "greater blessing of sanctuary";
                auraName = "greater blessing of sanctuary";
                break;
        }

        // Find a target who needs the blessing
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld())
                continue;

            // Check if the member is of the correct class and missing the aura
            if (member->getClass() != classId || botAI->HasAura(auraName, member))
                continue;

            // Ensure the member is alive
            if (!member->IsAlive())
            {
                continue;
            }

            // Ensure the member is within range (30 yards)
            if (!bot->IsWithinDistInMap(member, 30.0f))
            {
                continue;
            }

            // **New: Check if the bot can actually cast the Greater Blessing on this target**
            if (!botAI->CanCastSpell(blessingSpell, member))
            {
                continue;
            }

            // If we find a valid target, return true to activate the trigger
            return true;
        }
    }

    // No missing blessings found
    return false;
}
