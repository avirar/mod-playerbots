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

inline std::string const GetActualBlessingOfMight(Unit* target, PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    if (!bot)
    {
        return "";
    }

    Group* group = bot->GetGroup();
    botAI->TellMaster("Started GetActualBlessingOfMight function");
    if (!target->ToPlayer())
    {
        botAI->TellMaster("No conversion ToPlayer");
        if (botAI->HasAnyAuraOf(target, "blessing of might", "greater blessing of might", "battle shout", nullptr) && 
            !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
        {
            botAI->TellMaster("Might already on target, casting Kings");
            /*
            if (group && bot->HasSpell(25898))
            {
                return "greater blessing of kings";
            }
            */
            return "blessing of kings";
        }
        /*
        else if (group && bot->HasSpell(25782))
        {
            return "greater blessing of might";
        }
        */
        botAI->TellMaster("Casting Might");
        return "blessing of might";
    }
    int tab = AiFactory::GetPlayerSpecTab(target->ToPlayer());
    switch (target->getClass())
    {
        case CLASS_MAGE:
        case CLASS_PRIEST:
        case CLASS_WARLOCK:
            if (botAI->HasAnyAuraOf(target, "blessing of wisdom", "greater blessing of wisdom", nullptr) && 
                !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
            {
                botAI->TellMaster("Wisdom already on target, casting Kings");
                /*
                if (group && bot->HasSpell(25898))
                {
                    return "greater blessing of kings";
                }
                */
                return "blessing of kings";
            }
            /*
            else if (group && bot->HasSpell(25894))
            {
                return "greater blessing of wisdom";
            }
            */
            botAI->TellMaster("Casting Wisdom on Warlock/Priest/Mage");
            return "blessing of wisdom";
            break;
        case CLASS_SHAMAN:
            if (tab == SHAMAN_TAB_ELEMENTAL || tab == SHAMAN_TAB_RESTORATION)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of wisdom", "greater blessing of wisdom", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    botAI->TellMaster("Wisdom already on target, casting Kings");
                    /*
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    */
                    return "blessing of kings";
                }
                /*
                else if (group && bot->HasSpell(25894))
                {
                    return "greater blessing of wisdom";
                }
                */
                botAI->TellMaster("Casting Wisdom on Ele/Resto Shaman");
                return "blessing of wisdom";
            }
            break;
        case CLASS_DRUID:
            if (tab == DRUID_TAB_RESTORATION || tab == DRUID_TAB_BALANCE)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of wisdom", "greater blessing of wisdom", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    botAI->TellMaster("Wisdom already on target, casting Kings");
                    /*
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    */
                    return "blessing of kings";
                }
                /*
                else if (group && bot->HasSpell(25894))
                {
                    return "greater blessing of wisdom";
                }
                */
                botAI->TellMaster("Casting Wisdom on Resto/Boomy Druid");
                return "blessing of wisdom";
            }
            break;
        case CLASS_PALADIN:
            if (tab == PALADIN_TAB_HOLY)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of wisdom", "greater blessing of wisdom", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    botAI->TellMaster("Wisdom already on target, casting Kings");
                    /*
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    */
                    return "blessing of kings";
                }
                /*
                else if (group && bot->HasSpell(25894))
                {
                    return "greater blessing of wisdom";
                }
                */
                botAI->TellMaster("Casting Wisdom on Holy Paladin");
                return "blessing of wisdom";
            }
            break;
    }
    if (botAI->HasAnyAuraOf(target, "blessing of might", "greater blessing of might", "battle shout", nullptr) && 
        !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
    {
        botAI->TellMaster("Might already on target, casting Kings");
        /*
        if (group && bot->HasSpell(25898))
        {
            return "greater blessing of kings";
        }
        */
        return "blessing of kings";
    }
    /*
    else if (group && bot->HasSpell(25782))
    {
        return "greater blessing of might";
    }
    */
    botAI->TellMaster("Casting Might");
    return "blessing of might";
}

inline std::string const GetActualBlessingOfWisdom(Unit* target, PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    
    if (!bot)
    {
        return "";
    }

    Group* group = bot->GetGroup();
    
    if (!target->ToPlayer())
    {
        if (botAI->HasAnyAuraOf(target, "blessing of wisdom", "greater blessing of wisdom", nullptr) && 
            !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
        {
            if (group && bot->HasSpell(25898))
            {
                return "greater blessing of kings";
            }
            return "blessing of kings";
        }
        else if (group && bot->HasSpell(25894))
        {
            return "greater blessing of wisdom";
        }
        return "blessing of wisdom";
    }
    int tab = AiFactory::GetPlayerSpecTab(target->ToPlayer());
    switch (target->getClass())
    {
        case CLASS_WARRIOR:
        case CLASS_ROGUE:
        case CLASS_DEATH_KNIGHT:
        case CLASS_HUNTER:
            if (botAI->HasAnyAuraOf(target, "blessing of might", "greater blessing of might", "battle shout", nullptr) && 
                !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
            {
                if (group && bot->HasSpell(25898))
                {
                    return "greater blessing of kings";
                }
                return "blessing of kings";
            }
            else if (group && bot->HasSpell(25782))
            {
                return "greater blessing of might";
            }
            return "blessing of might";
            break;
        case CLASS_SHAMAN:
            if (tab == SHAMAN_TAB_ENHANCEMENT)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of might", "greater blessing of might", "battle shout", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    return "blessing of kings";
                }
                else if (group && bot->HasSpell(25782))
                {
                    return "greater blessing of might";
                }
                return "blessing of might";
            }
            break;
        case CLASS_DRUID:
            if (tab == DRUID_TAB_FERAL)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of might", "greater blessing of might", "battle shout", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    return "blessing of kings";
                }
                else if (group && bot->HasSpell(25782))
                {
                    return "greater blessing of might";
                }
                return "blessing of might";
            }
            break;
        case CLASS_PALADIN:
            if (tab == PALADIN_TAB_PROTECTION || tab == PALADIN_TAB_RETRIBUTION)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of might", "greater blessing of might", "battle shout", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    return "blessing of kings";
                }
                else if (group && bot->HasSpell(25782))
                {
                    return "greater blessing of might";
                }
                return "blessing of might";
            }
            break;
    }
    if (botAI->HasAnyAuraOf(target, "blessing of wisdom", "greater blessing of wisdom", nullptr) && 
        !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
    {
        if (group && bot->HasSpell(25898))
        {
            return "greater blessing of kings";
        }
        return "blessing of kings";
    }
    else if (group && bot->HasSpell(25894))
    {
        return "greater blessing of wisdom";
    }
    return "blessing of wisdom";
}

inline std::string const GetActualBlessingOfKings(Unit* target, PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    if (!bot)
    {
        return "";
    }

    Group* group = bot->GetGroup();
    
    if (!target->ToPlayer())
    {
        if (botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr) && 
            !botAI->HasAnyAuraOf(target, "blessing of sanctuary", "greater blessing of sanctuary", nullptr))
        {
            if (group && bot->HasSpell(25898))
            {
                return "greater blessing of sanctuary";
            }
            return "blessing of sanctuary";
        }
        else if (group && bot->HasSpell(25898))
        {
            return "greater blessing of kings";
        }
        return "blessing of kings";
    }
    int tab = AiFactory::GetPlayerSpecTab(target->ToPlayer());
    switch (target->getClass())
    {
        case CLASS_DRUID:
            if (tab == DRUID_TAB_FERAL)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of sanctuary", "greater blessing of sanctuary", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    return "blessing of kings";
                }
                else if (group && bot->HasSpell(25899) && botAI->IsTank(target->ToPlayer()))
                {
                    return "greater blessing of sanctuary";
                }
                return "blessing of sanctuary";
            }
            break;
        case CLASS_PALADIN:
            if (tab == PALADIN_TAB_PROTECTION)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of sanctuary", "greater blessing of sanctuary", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    return "blessing of kings";
                }
                else if (group && bot->HasSpell(25899) && botAI->IsTank(target->ToPlayer()))
                {
                    return "greater blessing of sanctuary";
                }
                return "blessing of sanctuary";
            }
            break;
        case CLASS_WARRIOR:
            if (tab == WARRIOR_TAB_PROTECTION)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of sanctuary", "greater blessing of sanctuary", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    return "blessing of kings";
                }
                else if (group && bot->HasSpell(25899) && botAI->IsTank(target->ToPlayer()))
                {
                    return "greater blessing of sanctuary";
                }
                return "blessing of sanctuary";
            }
            break;
        case CLASS_DEATH_KNIGHT:
            if (tab == DEATHKNIGHT_TAB_BLOOD)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of sanctuary", "greater blessing of sanctuary", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    return "blessing of kings";
                }
                else if (group && bot->HasSpell(25899) && botAI->IsTank(target->ToPlayer()))
                {
                    return "greater blessing of sanctuary";
                }
                return "blessing of sanctuary";
            }
            break;
    }
    if (botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr) && 
        !botAI->HasAnyAuraOf(target, "blessing of sanctuary", "greater blessing of sanctuary", nullptr))        
    {
        if (group && bot->HasSpell(25898))
        {
            return "greater blessing of sanctuary";
        }
        return "blessing of sanctuary";
    }
    else if (group && bot->HasSpell(25898))
    {
        return "greater blessing of kings";
    }
    return "blessing of kings";
}

inline std::string const GetActualBlessingOfSanctuary(Unit* target, PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    if (!bot)
    {
        return "";
    }

    Group* group = bot->GetGroup();
    
    if (!target->ToPlayer())
    {
        if (botAI->HasAnyAuraOf(target, "blessing of sanctuary", "greater blessing of sanctuary", nullptr) && 
            !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
        {
            if (group && bot->HasSpell(25898))
            {
                return "greater blessing of kings";
            }
            return "blessing of kings";
        }
        else if (group && bot->HasSpell(25899))
        {
            return "greater blessing of sanctuary";
        }
        return "blessing of sanctuary";
    }
    int tab = AiFactory::GetPlayerSpecTab(target->ToPlayer());
    switch (target->getClass())
    {
        case CLASS_DRUID:
            if (tab == DRUID_TAB_FERAL)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of sanctuary", "greater blessing of sanctuary", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    return "blessing of kings";
                }
                else if (group && bot->HasSpell(25899) && botAI->IsTank(target->ToPlayer()))
                {
                    return "greater blessing of sanctuary";
                }
                return "blessing of sanctuary";
            }
            break;
        case CLASS_PALADIN:
            if (tab == PALADIN_TAB_PROTECTION)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of sanctuary", "greater blessing of sanctuary", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    return "blessing of kings";
                }
                else if (group && bot->HasSpell(25899) && botAI->IsTank(target->ToPlayer()))
                {
                    return "greater blessing of sanctuary";
                }
                return "blessing of sanctuary";
            }
            break;
        case CLASS_WARRIOR:
            if (tab == WARRIOR_TAB_PROTECTION)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of sanctuary", "greater blessing of sanctuary", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    return "blessing of kings";
                }
                else if (group && bot->HasSpell(25899) && botAI->IsTank(target->ToPlayer()))
                {
                    return "greater blessing of sanctuary";
                }
                return "blessing of sanctuary";
            }
            break;
        case CLASS_DEATH_KNIGHT:
            if (tab == DEATHKNIGHT_TAB_BLOOD)
            {
                if (botAI->HasAnyAuraOf(target, "blessing of sanctuary", "greater blessing of sanctuary", nullptr) && 
                    !botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr))
                {
                    if (group && bot->HasSpell(25898))
                    {
                        return "greater blessing of kings";
                    }
                    return "blessing of kings";
                }
                else if (group && bot->HasSpell(25899) && botAI->IsTank(target->ToPlayer()))
                {
                    return "greater blessing of sanctuary";
                }
                return "blessing of sanctuary";
            }
            break;
    }
    if (botAI->HasAnyAuraOf(target, "blessing of kings", "greater blessing of kings", nullptr) && 
        !botAI->HasAnyAuraOf(target, "blessing of sanctuary", "greater blessing of sanctuary", nullptr))
    {
        if (group && bot->HasSpell(25898))
        {
            return "greater blessing of sanctuary";
        }
        return "blessing of sanctuary";
    }
    else if (group && bot->HasSpell(25898))
    {
        return "greater blessing of kings";
    }
    return "blessing of kings";
}

Value<Unit*>* CastBlessingOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", name);
}

bool CastBlessingOfMightAction::Execute(Event event)
{
    botAI->TellMaster("Starting CastBlessingOfMightAction");
    Unit* target = GetTarget();
    if (!target)
        return false;

    // Define all possible blessings
    std::vector<std::string> blessings = {
        "blessing of might", "blessing of wisdom",
        "blessing of kings", "blessing of sanctuary",
        "greater blessing of might", "greater blessing of wisdom",
        "greater blessing of kings", "greater blessing of sanctuary"
    };

    // Check if this Paladin has already applied *any* blessing to the target
    for (const auto& blessing : blessings)
    {
        if (botAI->HasAura(blessing, target, false, true)) // Only check bot's blessings
        {
            return false; // If any blessing from this Paladin exists, don't cast another
        }
    }

    return botAI->CastSpell(GetActualBlessingOfMight(target, botAI), target);
}

Value<Unit*>* CastBlessingOfMightOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", "blessing of might");
}

bool CastBlessingOfMightOnPartyAction::Execute(Event event)
{
    botAI->TellMaster("Started CastBlessingOfMightOnPartyAction");
    Unit* target = GetTarget();
    if (!target)
    {
        botAI->TellMaster("Invalid/No target");
        return false;
    }

    // Define all possible blessings
    std::vector<std::string> blessings = {
        "blessing of might", "blessing of wisdom",
        "blessing of kings", "blessing of sanctuary",
        "greater blessing of might", "greater blessing of wisdom",
        "greater blessing of kings", "greater blessing of sanctuary"
    };

    // Check if this Paladin has already applied *any* blessing to the target
    for (const auto& blessing : blessings)
    {
        if (botAI->HasAura(blessing, target, false, true)) // Only check bot's blessings
        {
            std::ostringstream str;
            str << target->GetName() << " already has " << blessing << " from me.";
            botAI->TellMaster(str.str());
            return false; // If any blessing from this Paladin exists, don't cast another
        }
    }

    return botAI->CastSpell(GetActualBlessingOfMight(target, botAI), target);
}

bool CastBlessingOfWisdomAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    // Define all possible blessings
    std::vector<std::string> blessings = {
        "blessing of might", "blessing of wisdom",
        "blessing of kings", "blessing of sanctuary",
        "greater blessing of might", "greater blessing of wisdom",
        "greater blessing of kings", "greater blessing of sanctuary"
    };

    // Check if this Paladin has already applied *any* blessing to the target
    for (const auto& blessing : blessings)
    {
        if (botAI->HasAura(blessing, target, false, true)) // Only check bot's blessings
        {
            return false; // If any blessing from this Paladin exists, don't cast another
        }
    }

    return botAI->CastSpell(GetActualBlessingOfWisdom(target, botAI), target);
}

Value<Unit*>* CastBlessingOfWisdomOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", "blessing of wisdom");
}

bool CastBlessingOfWisdomOnPartyAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    // Define all possible blessings
    std::vector<std::string> blessings = {
        "blessing of might", "blessing of wisdom",
        "blessing of kings", "blessing of sanctuary",
        "greater blessing of might", "greater blessing of wisdom",
        "greater blessing of kings", "greater blessing of sanctuary"
    };

    // Check if this Paladin has already applied *any* blessing to the target
    for (const auto& blessing : blessings)
    {
        if (botAI->HasAura(blessing, target, false, true)) // Only check bot's blessings
        {
            return false; // If any blessing from this Paladin exists, don't cast another
        }
    }

    return botAI->CastSpell(GetActualBlessingOfWisdom(target, botAI), target);
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

bool CastGreaterBlessingAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsAlive())
        return false;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    // Gather assignments
    auto assignment = AssignBlessingsForGroup(botAI);

    // Find what is assigned to *this* Paladin
    auto paladinIt = assignment.find(bot);
    if (paladinIt == assignment.end())
        return false; // No blessings assigned

    // For each (class -> blessing) assigned, search for a player missing it
    for (auto& kv : paladinIt->second)
    {
        uint8 classId = kv.first;
        GreaterBlessingType gBlessing = kv.second;

        // Convert the blessing to a spell name
        std::string spellName;
        std::string auraName; 
        switch (gBlessing)
        {
            case GREATER_BLESSING_OF_MIGHT:
                spellName = "greater blessing of might";
                auraName  = "greater blessing of might";
                break;
            case GREATER_BLESSING_OF_WISDOM:
                spellName = "greater blessing of wisdom";
                auraName  = "greater blessing of wisdom";
                break;
            case GREATER_BLESSING_OF_KINGS:
                spellName = "greater blessing of kings";
                auraName  = "greater blessing of kings";
                break;
            case GREATER_BLESSING_OF_SANCTUARY:
                spellName = "greater blessing of sanctuary";
                auraName  = "greater blessing of sanctuary";
                break;
        }

        // Find the first raid member of that class who lacks the aura
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld())
                continue;

            // Skip if the member already has the blessing
            if (member->getClass() != classId || botAI->HasAura(auraName, member))
                continue;

            if (!member->IsAlive())
            {
                continue;
            }

            if (!bot->IsWithinDistInMap(member, 30.0f))
            {
                continue;
            }

            // Check if the bot can actually cast the Greater Blessing on this target
            if (!botAI->CanCastSpell(spellName, member))
            {
                continue;
            }

            // Found a valid target
            botAI->TellMaster("Casting " + spellName + " on " + member->GetName());

            return botAI->CastSpell(spellName, member);
        }
    }

    // If we reach here, we didn't find any missing aura
    return false;
}

bool CastBlessingOfKingsAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    // Define all possible blessings
    std::vector<std::string> blessings = {
        "blessing of might", "blessing of wisdom",
        "blessing of kings", "blessing of sanctuary",
        "greater blessing of might", "greater blessing of wisdom",
        "greater blessing of kings", "greater blessing of sanctuary"
    };

    // Check if this Paladin has already applied *any* blessing to the target
    for (const auto& blessing : blessings)
    {
        if (botAI->HasAura(blessing, target, false, true)) // Only check bot's blessings
        {
            return false; // If any blessing from this Paladin exists, don't cast another
        }
    }

    return botAI->CastSpell(GetActualBlessingOfKings(target, botAI), target);
}

Value<Unit*>* CastBlessingOfKingsOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", "blessing of kings");
}

bool CastBlessingOfKingsOnPartyAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    // Define all possible blessings
    std::vector<std::string> blessings = {
        "blessing of might", "blessing of wisdom",
        "blessing of kings", "blessing of sanctuary",
        "greater blessing of might", "greater blessing of wisdom",
        "greater blessing of kings", "greater blessing of sanctuary"
    };

    // Check if this Paladin has already applied *any* blessing to the target
    for (const auto& blessing : blessings)
    {
        if (botAI->HasAura(blessing, target, false, true)) // Only check bot's blessings
        {
            return false; // If any blessing from this Paladin exists, don't cast another
        }
    }

    return botAI->CastSpell(GetActualBlessingOfKings(target, botAI), target);
}

bool CastBlessingOfSanctuaryAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    // Define all possible blessings
    std::vector<std::string> blessings = {
        "blessing of might", "blessing of wisdom",
        "blessing of kings", "blessing of sanctuary",
        "greater blessing of might", "greater blessing of wisdom",
        "greater blessing of kings", "greater blessing of sanctuary"
    };

    // Check if this Paladin has already applied *any* blessing to the target
    for (const auto& blessing : blessings)
    {
        if (botAI->HasAura(blessing, target, false, true)) // Only check bot's blessings
        {
            return false; // If any blessing from this Paladin exists, don't cast another
        }
    }

    return botAI->CastSpell(GetActualBlessingOfSanctuary(target, botAI), target);
}

Value<Unit*>* CastBlessingOfSanctuaryOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", "blessing of sanctuary");
}

bool CastBlessingOfSanctuaryOnPartyAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    // Define all possible blessings
    std::vector<std::string> blessings = {
        "blessing of might", "blessing of wisdom",
        "blessing of kings", "blessing of sanctuary",
        "greater blessing of might", "greater blessing of wisdom",
        "greater blessing of kings", "greater blessing of sanctuary"
    };

    // Check if this Paladin has already applied *any* blessing to the target
    for (const auto& blessing : blessings)
    {
        if (botAI->HasAura(blessing, target, false, true)) // Only check bot's blessings
        {
            return false; // If any blessing from this Paladin exists, don't cast another
        }
    }

    return botAI->CastSpell(GetActualBlessingOfSanctuary(target, botAI), target);
}
