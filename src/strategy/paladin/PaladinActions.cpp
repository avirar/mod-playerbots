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

Value<Unit*>* CastGreaterBlessingOfMightAction::GetTargetValue()
{
    // Check if the bot is in a raid group
    Group* group = botAI->GetBot()->GetGroup();
    if (!group || !group->isRaidGroup())
    {
        LOG_INFO("playerbots", "Bot {} <{}> is not in a raid group, cannot cast Greater Blessing of Might", 
                 bot->GetGUID().ToString().c_str(), bot->GetName().c_str());
        return new ManualSetValue<Unit*>(botAI, nullptr);
    }

    uint64 groupId = group->GetGUID(); // Assuming group has a unique GUID

    // Retrieve the BlessingManager instance for this group
    BlessingManager* blessingManager = BlessingManager::getInstance(botAI, groupId);

    // Get assigned blessings for the bot
    std::vector<GreaterBlessingType> blessings = blessingManager->GetAssignedBlessings(botAI);

    // Check if Greater Blessing of Might is assigned
    if (std::find(blessings.begin(), blessings.end(), GREATER_BLESSING_OF_MIGHT) == blessings.end())
    {
        LOG_INFO("playerbots", "Bot {} <{}> is not assigned Greater Blessing of Might", 
                 bot->GetGUID().ToString().c_str(), bot->GetName().c_str());
        return new ManualSetValue<Unit*>(botAI, nullptr);
    }

    // Get the target classes for Greater Blessing of Might
    std::vector<ClassID> targetClasses = blessingManager->GetClassesForBlessing(botAI, GREATER_BLESSING_OF_MIGHT);
    if (targetClasses.empty())
    {
        LOG_INFO("playerbots", "Bot {} <{}> has no assigned classes for Greater Blessing of Might", 
                 bot->GetGUID().ToString().c_str(), bot->GetName().c_str());
        return new ManualSetValue<Unit*>(botAI, nullptr);
    }

    // Find the first raid member who matches the criteria
    GroupReference* ref = group->GetFirstMember();
    while (ref)
    {
        Player* member = ref->GetSource();
        if (member && member->IsInWorld())
        {
            ClassID memberClass = static_cast<ClassID>(member->getClass());
            // Check if the member's class matches the assigned blessing classes
            if (std::find(targetClasses.begin(), targetClasses.end(), memberClass) != targetClasses.end())
            {
                if (!botAI->HasAura("greater blessing of might", member))
                {
                    LOG_INFO("playerbots", "Bot {} <{}> found target {} <{}> for Greater Blessing of Might", 
                             bot->GetGUID().ToString().c_str(), bot->GetName().c_str(),
                             member->GetGUID().ToString().c_str(), member->GetName().c_str());
                    return new ManualSetValue<Unit*>(botAI, member); // Return the first valid target
                }
            }
        }
        ref = ref->next();
    }

    LOG_INFO("playerbots", "Bot {} <{}> found no valid targets for Greater Blessing of Might", 
             bot->GetGUID().ToString().c_str(), bot->GetName().c_str());
    return new ManualSetValue<Unit*>(botAI, nullptr); // No valid targets found
}

bool CastGreaterBlessingOfMightAction::Execute(Event event)
{
    Unit* target = GetTargetValue()->Get();
    if (!target)
        return false;

    // Log the casting action
    botAI->TellMaster("Casting Greater Blessing of Might on " + std::string(target->GetName()));

    // Cast Greater Blessing of Might
    return botAI->CastSpell("greater blessing of might", target);
}


bool CastGreaterBlessingOfWisdomAction::Execute(Event event)
{
    // Check if the bot is in a raid
    if (botAI->GetBot()->GetGroup() == nullptr || !botAI->GetBot()->GetGroup()->isRaidGroup())
        return false; // Only cast in raid

    // Initialize Blessing Manager
    BlessingManager blessingManager(botAI);
    blessingManager.AssignBlessings();

    // Get assigned blessings for this Paladin
    std::vector<GreaterBlessingType> blessings = blessingManager.GetAssignedBlessings(botAI);

    // Iterate through assigned blessings and cast Greater Blessing of Wisdom if assigned
    for (GreaterBlessingType blessing : blessings)
    {
        if (blessing == GREATER_BLESSING_OF_WISDOM)
        {
            // Get the classes assigned to cast Greater Blessing of Wisdom
            std::vector<ClassID> targetClasses = blessingManager.GetClassesForBlessing(botAI, GREATER_BLESSING_OF_WISDOM);
            
            if (targetClasses.empty())
                continue; // No classes assigned for this blessing

            // Retrieve raid members of the target classes
            std::vector<Unit*> targets;
            Group* group = botAI->GetBot()->GetGroup();
            if (group && group->isRaidGroup())
            {
                GroupReference* ref = group->GetFirstMember();
                while (ref)
                {
                    Player* member = ref->GetSource();
                    if (member && member->IsInWorld())
                    {
                        ClassID memberClass = static_cast<ClassID>(member->getClass());
                        // Check if member's class is in the target classes
                        if (std::find(targetClasses.begin(), targetClasses.end(), memberClass) != targetClasses.end())
                        {
                            targets.push_back(member);
                        }
                    }
                    ref = ref->next();
                }
            }

            for (Unit* target : targets)
            {
                if (!botAI->HasAura("greater blessing of wisdom", target))
                {
                    // Log the casting action for debugging purposes
                    botAI->TellMaster("Casting Greater Blessing of Wisdom on " + std::string(target->GetName()));
                    
                    // Cast Greater Blessing of Wisdom
                    if (botAI->CastSpell("greater blessing of wisdom", target))
                        return true; // Successfully casted
                }
            }
        }
    }

    return false; // No casting performed
}

bool CastGreaterBlessingOfKingsAction::Execute(Event event)
{
    // Check if the bot is in a raid
    if (botAI->GetBot()->GetGroup() == nullptr || !botAI->GetBot()->GetGroup()->isRaidGroup())
        return false; // Only cast in raid

    // Initialize Blessing Manager
    BlessingManager blessingManager(botAI);
    blessingManager.AssignBlessings();

    // Get assigned blessings for this Paladin
    std::vector<GreaterBlessingType> blessings = blessingManager.GetAssignedBlessings(botAI);

    // Iterate through assigned blessings and cast Greater Blessing of Kings if assigned
    for (GreaterBlessingType blessing : blessings)
    {
        if (blessing == GREATER_BLESSING_OF_KINGS)
        {
            // Get the classes assigned to cast Greater Blessing of Kings
            std::vector<ClassID> targetClasses = blessingManager.GetClassesForBlessing(botAI, GREATER_BLESSING_OF_KINGS);
            
            if (targetClasses.empty())
                continue; // No classes assigned for this blessing

            // Retrieve raid members of the target classes
            std::vector<Unit*> targets;
            Group* group = botAI->GetBot()->GetGroup();
            if (group && group->isRaidGroup())
            {
                GroupReference* ref = group->GetFirstMember();
                while (ref)
                {
                    Player* member = ref->GetSource();
                    if (member && member->IsInWorld())
                    {
                        ClassID memberClass = static_cast<ClassID>(member->getClass());
                        // Check if member's class is in the target classes
                        if (std::find(targetClasses.begin(), targetClasses.end(), memberClass) != targetClasses.end())
                        {
                            targets.push_back(member);
                        }
                    }
                    ref = ref->next();
                }
            }

            for (Unit* target : targets)
            {
                if (!botAI->HasAura("greater blessing of kings", target))
                {
                    // Log the casting action for debugging purposes
                    botAI->TellMaster("Casting Greater Blessing of Kings on " + std::string(target->GetName()));
                    
                    // Cast Greater Blessing of Kings
                    if (botAI->CastSpell("greater blessing of kings", target))
                        return true; // Successfully casted
                }
            }
        }
    }

    return false; // No casting performed
}

bool CastGreaterBlessingOfSanctuaryAction::Execute(Event event)
{
    // Check if the bot is in a raid
    if (botAI->GetBot()->GetGroup() == nullptr || !botAI->GetBot()->GetGroup()->isRaidGroup())
        return false; // Only cast in raid

    // Initialize Blessing Manager
    BlessingManager blessingManager(botAI);
    blessingManager.AssignBlessings();

    // Get assigned blessings for this Paladin
    std::vector<GreaterBlessingType> blessings = blessingManager.GetAssignedBlessings(botAI);

    // Iterate through assigned blessings and cast Greater Blessing of Sanctuary if assigned
    for (GreaterBlessingType blessing : blessings)
    {
        if (blessing == GREATER_BLESSING_OF_SANCTUARY)
        {
            // Get the classes assigned to cast Greater Blessing of Sanctuary
            std::vector<ClassID> targetClasses = blessingManager.GetClassesForBlessing(botAI, GREATER_BLESSING_OF_SANCTUARY);
            
            if (targetClasses.empty())
                continue; // No classes assigned for this blessing

            // Retrieve raid members of the target classes
            std::vector<Unit*> targets;
            Group* group = botAI->GetBot()->GetGroup();
            if (group && group->isRaidGroup())
            {
                GroupReference* ref = group->GetFirstMember();
                while (ref)
                {
                    Player* member = ref->GetSource();
                    if (member && member->IsInWorld())
                    {
                        ClassID memberClass = static_cast<ClassID>(member->getClass());
                        // Check if member's class is in the target classes
                        if (std::find(targetClasses.begin(), targetClasses.end(), memberClass) != targetClasses.end())
                        {
                            targets.push_back(member);
                        }
                    }
                    ref = ref->next();
                }
            }

            for (Unit* target : targets)
            {
                if (!botAI->HasAura("greater blessing of sanctuary", target))
                {
                    // Log the casting action for debugging purposes
                    botAI->TellMaster("Casting Greater Blessing of Sanctuary on " + std::string(target->GetName()));
                    
                    // Cast Greater Blessing of Sanctuary
                    if (botAI->CastSpell("greater blessing of sanctuary", target))
                        return true; // Successfully casted
                }
            }
        }
    }

    return false; // No casting performed
}
