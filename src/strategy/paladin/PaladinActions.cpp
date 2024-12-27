/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "PaladinActions.h"
#include "BlessingManager.h"

#include "AiFactory.h"
#include "Event.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "Playerbots.h"
#include "SharedDefines.h"

inline std::string const GetActualBlessingOfMight(Unit* target)
{
    if (!target->ToPlayer())
    {
        return "blessing of might";
    }
    int tab = AiFactory::GetPlayerSpecTab(target->ToPlayer());
    switch (target->getClass())
    {
        case CLASS_MAGE:
        case CLASS_PRIEST:
        case CLASS_WARLOCK:
            return "blessing of wisdom";
            break;
        case CLASS_SHAMAN:
            if (tab == SHAMAN_TAB_ELEMENTAL || tab == SHAMAN_TAB_RESTORATION)
            {
                return "blessing of wisdom";
            }
            break;
        case CLASS_DRUID:
            if (tab == DRUID_TAB_RESTORATION || tab == DRUID_TAB_BALANCE)
            {
                return "blessing of wisdom";
            }
            break;
        case CLASS_PALADIN:
            if (tab == PALADIN_TAB_HOLY)
            {
                return "blessing of wisdom";
            }
            break;
    }

    return "blessing of might";
}

inline std::string const GetActualBlessingOfWisdom(Unit* target)
{
    if (!target->ToPlayer())
    {
        return "blessing of might";
    }
    int tab = AiFactory::GetPlayerSpecTab(target->ToPlayer());
    switch (target->getClass())
    {
        case CLASS_WARRIOR:
        case CLASS_ROGUE:
        case CLASS_DEATH_KNIGHT:
        case CLASS_HUNTER:
            return "blessing of might";
            break;
        case CLASS_SHAMAN:
            if (tab == SHAMAN_TAB_ENHANCEMENT)
            {
                return "blessing of might";
            }
            break;
        case CLASS_DRUID:
            if (tab == DRUID_TAB_FERAL)
            {
                return "blessing of might";
            }
            break;
        case CLASS_PALADIN:
            if (tab == PALADIN_TAB_PROTECTION || tab == PALADIN_TAB_RETRIBUTION)
            {
                return "blessing of might";
            }
            break;
    }

    return "blessing of wisdom";
}

Value<Unit*>* CastBlessingOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", name);
}

bool CastBlessingOfMightAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    return botAI->CastSpell(GetActualBlessingOfMight(target), target);
}

Value<Unit*>* CastBlessingOfMightOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", "blessing of might,blessing of wisdom");
}

bool CastBlessingOfMightOnPartyAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    return botAI->CastSpell(GetActualBlessingOfMight(target), target);
}

bool CastBlessingOfWisdomAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    return botAI->CastSpell(GetActualBlessingOfWisdom(target), target);
}

Value<Unit*>* CastBlessingOfWisdomOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", "blessing of might,blessing of wisdom");
}

bool CastBlessingOfWisdomOnPartyAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    return botAI->CastSpell(GetActualBlessingOfWisdom(target), target);
}

bool CastSealSpellAction::isUseful() { return AI_VALUE2(bool, "combat", "self target"); }

Value<Unit*>* CastTurnUndeadAction::GetTargetValue() { return context->GetValue<Unit*>("cc target", getName()); }

Unit* CastRighteousDefenseAction::GetTarget()
{
    Unit* current_target = AI_VALUE(Unit*, "current target");
    if (!current_target)
    {
        return NULL;
    }
    return current_target->GetVictim();
}


bool CastDivineSacrificeAction::isUseful()
{
    return GetTarget() && (GetTarget() != nullptr) && CastSpellAction::isUseful() &&
           !botAI->HasAura("divine guardian", GetTarget(), false, false, -1, true);
}

bool CastCancelDivineSacrificeAction::Execute(Event event)
{
    botAI->RemoveAura("divine sacrifice");
    return true;
}

bool CastCancelDivineSacrificeAction::isUseful()
{
    return botAI->HasAura("divine sacrifice", GetTarget(), false, true, -1, true);
}

// Greater Blessing of Might Action
bool CastGreaterBlessingOfMightAction::Execute(Event event)
{
    if (!botAI->IsInRaid())
        return false; // Only cast in raid

    // Initialize Blessing Manager
    BlessingManager blessingManager(botAI);
    blessingManager.AssignBlessings();

    // Get assigned blessings for this Paladin
    std::vector<GreaterBlessingType> blessings = blessingManager.GetAssignedBlessings(botAI);

    // Iterate through assigned blessings and cast Greater Blessing of Might if assigned
    for (GreaterBlessingType blessing : blessings)
    {
        if (blessing == GREATER_BLESSING_OF_MIGHT)
        {
            // Determine target classes for Greater Blessing of Might
            std::vector<Unit*> targets = AI_VALUE2(std::vector<Unit*>, "raid members", "mage, priest, warlock"); // Adjust classes as needed
            
            for (Unit* target : targets)
            {
                if (!botAI->HasAura("greater blessing of might", target))
                {
                    // Cast Greater Blessing of Might
                    if (botAI->CastSpell("greater blessing of might", target))
                        return true;
                }
            }
        }
    }

    return false;
}

// Similarly implement for other Greater Blessings

bool CastGreaterBlessingOfWisdomAction::Execute(Event event)
{
    if (!botAI->IsInRaid())
        return false;

    BlessingManager blessingManager(botAI);
    blessingManager.AssignBlessings();

    std::vector<GreaterBlessingType> blessings = blessingManager.GetAssignedBlessings(botAI);

    for (GreaterBlessingType blessing : blessings)
    {
        if (blessing == GREATER_BLESSING_OF_WISDOM)
        {
            std::vector<Unit*> targets = AI_VALUE2(std::vector<Unit*>, "raid members", "warrior, rogue, death knight, hunter"); // Adjust classes as needed
            
            for (Unit* target : targets)
            {
                if (!botAI->HasAura("greater blessing of wisdom", target))
                {
                    if (botAI->CastSpell("greater blessing of wisdom", target))
                        return true;
                }
            }
        }
    }

    return false;
}

bool CastGreaterBlessingOfKingsAction::Execute(Event event)
{
    if (!botAI->IsInRaid())
        return false;

    BlessingManager blessingManager(botAI);
    blessingManager.AssignBlessings();

    std::vector<GreaterBlessingType> blessings = blessingManager.GetAssignedBlessings(botAI);

    for (GreaterBlessingType blessing : blessings)
    {
        if (blessing == GREATER_BLESSING_OF_KINGS)
        {
            std::vector<Unit*> targets = AI_VALUE2(std::vector<Unit*>, "raid members", "all classes"); // Typically applies to all classes
            
            for (Unit* target : targets)
            {
                if (!botAI->HasAura("greater blessing of kings", target))
                {
                    if (botAI->CastSpell("greater blessing of kings", target))
                        return true;
                }
            }
        }
    }

    return false;
}

bool CastGreaterBlessingOfSanctuaryAction::Execute(Event event)
{
    if (!botAI->IsInRaid())
        return false;

    BlessingManager blessingManager(botAI);
    blessingManager.AssignBlessings();

    std::vector<GreaterBlessingType> blessings = blessingManager.GetAssignedBlessings(botAI);

    for (GreaterBlessingType blessing : blessings)
    {
        if (blessing == GREATER_BLESSING_OF_SANCTUARY)
        {
            std::vector<Unit*> targets = AI_VALUE2(std::vector<Unit*>, "raid members", "tank"); // Typically applies to tanks
            
            for (Unit* target : targets)
            {
                if (!botAI->HasAura("greater blessing of sanctuary", target))
                {
                    if (botAI->CastSpell("greater blessing of sanctuary", target))
                        return true;
                }
            }
        }
    }

    return false;
}
