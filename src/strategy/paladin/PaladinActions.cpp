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

// Blessings by type
static const std::vector<std::string> blessingMight = {"blessing of might", "greater blessing of might", "battle shout"};
static const std::vector<std::string> blessingWisdom = {"blessing of wisdom", "greater blessing of wisdom"};
static const std::vector<std::string> blessingKings = {"blessing of kings", "greater blessing of kings"};
static const std::vector<std::string> blessingSanctuary = {"blessing of sanctuary", "greater blessing of sanctuary"};

// All Blessings
static const std::vector<std::string> blessings = {
    "blessing of might", "blessing of wisdom",
    "blessing of kings", "blessing of sanctuary",
    "greater blessing of might", "greater blessing of wisdom",
    "greater blessing of kings", "greater blessing of sanctuary"
};

// Helper function to check if the target already has a blessing
inline bool HasAnyBlessing(PlayerbotAI* botAI, Unit* target)
{
    if (!target)
        return false;

    for (const auto& blessing : blessings)
    {
        if (botAI->HasAura(blessing, target, false, true)) // Only check bot's blessings
            return true;
    }
    return false;
}

// Generic blessing casting function
inline bool CastBlessing(PlayerbotAI* botAI, Unit* target, std::string (*GetBlessingFunc)(Unit*, PlayerbotAI*))
{
    if (!target || HasAnyBlessing(botAI, target))
        return false;

    return botAI->CastSpell(GetBlessingFunc(target, botAI), target);
}

// Helper function to determine whether to cast Blessing of Kings instead
inline bool ShouldCastKings(Unit* target, PlayerbotAI* botAI, const std::vector<std::string>& blessingCheck)
{
    return botAI->HasAnyAuraOf(target, blessingCheck, nullptr) &&
           !botAI->HasAnyAuraOf(target, blessingKings, nullptr);
}

// Helper function to determine the best blessing based on role
inline std::string SelectBlessingForCaster(Unit* target, PlayerbotAI* botAI)
{
    if (ShouldCastKings(target, botAI, blessingWisdom))
    {
        return "blessing of kings";
    }
    return "blessing of wisdom";
}

inline std::string SelectBlessingForMelee(Unit* target, PlayerbotAI* botAI)
{
    if (ShouldCastKings(target, botAI, blessingMight))
    {
        return "blessing of kings";
    }
    return "blessing of might";
}

inline std::string SelectBlessingForTank(Unit* target, PlayerbotAI* botAI, Player* bot)
{
    if (ShouldCastKings(target, botAI, blessingSanctuary))
    {
        return "blessing of kings";
    }
    return bot->HasSpell(25899) ? "greater blessing of sanctuary" : "blessing of sanctuary";
}

// Function to determine the best blessing of Might
inline std::string GetActualBlessingOfMight(Unit* target, PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return "";

    if (!target->ToPlayer())
        return SelectBlessingForMelee(target, botAI);

    int tab = AiFactory::GetPlayerSpecTab(target->ToPlayer());
    switch (target->getClass())
    {
        case CLASS_MAGE:
        case CLASS_PRIEST:
        case CLASS_WARLOCK:
            return SelectBlessingForCaster(target, botAI);
        case CLASS_SHAMAN:
            if (tab == SHAMAN_TAB_ELEMENTAL || tab == SHAMAN_TAB_RESTORATION)
                return SelectBlessingForCaster(target, botAI);
            break;
        case CLASS_DRUID:
            if (tab == DRUID_TAB_RESTORATION || tab == DRUID_TAB_BALANCE)
                return SelectBlessingForCaster(target, botAI);
            break;
        case CLASS_PALADIN:
            if (tab == PALADIN_TAB_HOLY)
                return SelectBlessingForCaster(target, botAI);
            break;
    }
    return SelectBlessingForMelee(target, botAI);
}

// Function to determine the best blessing of Wisdom
inline std::string GetActualBlessingOfWisdom(Unit* target, PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return "";

    if (!target->ToPlayer())
        return SelectBlessingForCaster(target, botAI);

    int tab = AiFactory::GetPlayerSpecTab(target->ToPlayer());
    switch (target->getClass())
    {
        case CLASS_WARRIOR:
        case CLASS_ROGUE:
        case CLASS_DEATH_KNIGHT:
        case CLASS_HUNTER:
            return SelectBlessingForMelee(target, botAI);
        case CLASS_SHAMAN:
            if (tab == SHAMAN_TAB_ENHANCEMENT)
                return SelectBlessingForMelee(target, botAI);
            break;
        case CLASS_DRUID:
            if (tab == DRUID_TAB_FERAL)
                return SelectBlessingForMelee(target, botAI);
            break;
        case CLASS_PALADIN:
            if (tab == PALADIN_TAB_PROTECTION || tab == PALADIN_TAB_RETRIBUTION)
                return SelectBlessingForMelee(target, botAI);
            break;
    }
    return SelectBlessingForCaster(target, botAI);
}

// Function to determine the best blessing of Kings
inline std::string GetActualBlessingOfKings(Unit* target, PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return "";

    if (!target->ToPlayer())
    {
        if (ShouldCastKings(target, botAI, blessingKings))
            return "blessing of sanctuary";

        return bot->HasSpell(25898) ? "greater blessing of kings" : "blessing of kings";
    }

    int tab = AiFactory::GetPlayerSpecTab(target->ToPlayer());
    switch (target->getClass())
    {
        case CLASS_DRUID:
            if (tab == DRUID_TAB_FERAL)
                return SelectBlessingForTank(target, botAI, bot);
            break;
        case CLASS_PALADIN:
            if (tab == PALADIN_TAB_PROTECTION)
                return SelectBlessingForTank(target, botAI, bot);
            break;
        case CLASS_WARRIOR:
            if (tab == WARRIOR_TAB_PROTECTION)
                return SelectBlessingForTank(target, botAI, bot);
            break;
        case CLASS_DEATH_KNIGHT:
            if (tab == DEATHKNIGHT_TAB_BLOOD)
                return SelectBlessingForTank(target, botAI, bot);
            break;
    }
    return bot->HasSpell(25898) ? "greater blessing of kings" : "blessing of kings";
}

// Function to determine the best blessing of Sanctuary
inline std::string GetActualBlessingOfSanctuary(Unit* target, PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    if (!bot)
        return "";

    if (!target->ToPlayer())
    {
        if (ShouldCastKings(target, botAI, blessingSanctuary))
            return "blessing of kings";

        return bot->HasSpell(25899) ? "greater blessing of sanctuary" : "blessing of sanctuary";
    }

    int tab = AiFactory::GetPlayerSpecTab(target->ToPlayer());
    switch (target->getClass())
    {
        case CLASS_DRUID:
            if (tab == DRUID_TAB_FERAL)
                return SelectBlessingForTank(target, botAI, bot);
            break;
        case CLASS_PALADIN:
            if (tab == PALADIN_TAB_PROTECTION)
                return SelectBlessingForTank(target, botAI, bot);
            break;
        case CLASS_WARRIOR:
            if (tab == WARRIOR_TAB_PROTECTION)
                return SelectBlessingForTank(target, botAI, bot);
            break;
        case CLASS_DEATH_KNIGHT:
            if (tab == DEATHKNIGHT_TAB_BLOOD)
                return SelectBlessingForTank(target, botAI, bot);
            break;
    }
    return bot->HasSpell(25899) ? "greater blessing of sanctuary" : "blessing of sanctuary";
}

Value<Unit*>* CastBlessingOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", name);
}

Value<Unit*>* CastBlessingOfMightOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", "blessing of might,blessing of wisdom");
}

Value<Unit*>* CastBlessingOfWisdomOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", "blessing of wisdom,blessing of might");
}

Value<Unit*>* CastBlessingOfKingsOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", "blessing of kings");
}

Value<Unit*>* CastBlessingOfSanctuaryOnPartyAction::GetTargetValue()
{
    return context->GetValue<Unit*>("party member without aura", "blessing of sanctuary");
}

// Individual blessing actions
bool CastBlessingOfMightAction::Execute(Event event)
{
    botAI->TellMaster("Starting CastBlessingOfMightAction");
    return CastBlessing(botAI, GetTarget(), GetActualBlessingOfMight);
}

bool CastBlessingOfMightOnPartyAction::Execute(Event event)
{
    botAI->TellMaster("Started CastBlessingOfMightOnPartyAction");
    return CastBlessing(botAI, GetTarget(), GetActualBlessingOfMight);
}

bool CastBlessingOfWisdomAction::Execute(Event event)
{
    return CastBlessing(botAI, GetTarget(), GetActualBlessingOfWisdom);
}

bool CastBlessingOfWisdomOnPartyAction::Execute(Event event)
{
    return CastBlessing(botAI, GetTarget(), GetActualBlessingOfWisdom);
}

bool CastBlessingOfKingsAction::Execute(Event event)
{
    return CastBlessing(botAI, GetTarget(), GetActualBlessingOfKings);
}

bool CastBlessingOfKingsOnPartyAction::Execute(Event event)
{
    return CastBlessing(botAI, GetTarget(), GetActualBlessingOfKings);
}

bool CastBlessingOfSanctuaryAction::Execute(Event event)
{
    return CastBlessing(botAI, GetTarget(), GetActualBlessingOfSanctuary);
}

bool CastBlessingOfSanctuaryOnPartyAction::Execute(Event event)
{
    return CastBlessing(botAI, GetTarget(), GetActualBlessingOfSanctuary);
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
