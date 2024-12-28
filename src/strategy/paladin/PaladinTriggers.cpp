/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "PaladinTriggers.h"

#include "PaladinActions.h"
#include "PaladinConstants.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

#include "BlessingManager.h"

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

bool BlessingTrigger::IsActive()
{
    botAI->TellMaster("Bot Blessing of Might Trigger");
    Unit* target = GetTarget();
    return SpellTrigger::IsActive() && !botAI->HasAnyAuraOf(target, "blessing of might", "blessing of wisdom",
                                                            "blessing of kings", "blessing of sanctuary", nullptr);
}

bool GreaterBlessingOfMightNeededTrigger::IsActive()
{
    botAI->TellMaster("Bot Greater Blessing of Might Trigger");
    // Ensure botAI and bot are valid
    if (!botAI || !botAI->GetBot())
    {
        botAI->TellMaster("Error: BotAI or bot is not valid.");
        return false;
    }

    // Get the bot's group
    Group* group = botAI->GetBot()->GetGroup();
    if (!group || !group->isRaidGroup())
    {
        botAI->TellMaster("Bot is not in a raid group.");
        return false; // Only consider raid members
    }

    uint64 groupId = group->GetGUID(); // Assuming group has a unique GUID

    // Retrieve the BlessingManager singleton instance for this group
    BlessingManager* blessingManager = BlessingManager::getInstance(botAI, groupId);

    // Get assigned blessings for this Paladin
    std::vector<GreaterBlessingType> blessings = blessingManager->GetAssignedBlessings(botAI);

    // Check if Greater Blessing of Might is among the assigned blessings
    if (std::find(blessings.begin(), blessings.end(), GREATER_BLESSING_OF_MIGHT) == blessings.end())
    {
        botAI->TellMaster("This Paladin is not assigned to cast Greater Blessing of Might.");
        return false; // This Paladin is not assigned to cast Greater Blessing of Might
    }

    // Retrieve classes assigned to Greater Blessing of Might for this Paladin
    std::vector<ClassID> targetClasses = blessingManager->GetClassesForBlessing(botAI, GREATER_BLESSING_OF_MIGHT);

    if (targetClasses.empty())
    {
        botAI->TellMaster("No classes assigned for Greater Blessing of Might.");
        return false; // No classes assigned for this blessing
    }

    // Iterate through group members and check for targets without the aura
    GroupReference* ref = group->GetFirstMember();
    while (ref)
    {
        Player* member = ref->GetSource();
        if (member && member->IsInWorld())
        {
            ClassID memberClass = static_cast<ClassID>(member->getClass());
            // Check if the member's class is in the target classes
            if (std::find(targetClasses.begin(), targetClasses.end(), memberClass) != targetClasses.end())
            {
                // Check if the target lacks Greater Blessing of Might
                if (!botAI->HasAura("greater blessing of might", member))
                {
                    botAI->TellMaster("Greater Blessing of Might is needed for a member in the raid.");
                    return true; // Trigger is active
                }
            }
        }
        ref = ref->next();
    }

    botAI->TellMaster("All members have Greater Blessing of Might or are not eligible.");
    return false; // No eligible targets found
}


bool GreaterBlessingOfWisdomNeededTrigger::IsActive()
{
    // Ensure botAI and bot are valid
    if (!botAI || !botAI->GetBot())
        return false;

    // Get the bot's group
    Group* group = botAI->GetBot()->GetGroup();
    if (!group || !group->isRaidGroup())
        return false; // Only consider raid members

    // Initialize Blessing Manager
    BlessingManager blessingManager(botAI);
    blessingManager.AssignBlessings();

    // Get assigned blessings for this Paladin
    std::vector<GreaterBlessingType> blessings = blessingManager.GetAssignedBlessings(botAI);

    // Check if Greater Blessing of Wisdom is among the assigned blessings
    if (std::find(blessings.begin(), blessings.end(), GREATER_BLESSING_OF_WISDOM) == blessings.end())
        return false; // This Paladin is not assigned to cast Greater Blessing of Wisdom

    // Retrieve classes assigned to Greater Blessing of Wisdom for this Paladin
    std::vector<ClassID> targetClasses = blessingManager.GetClassesForBlessing(botAI, GREATER_BLESSING_OF_WISDOM);

    if (targetClasses.empty())
        return false; // No classes assigned for this blessing

    // Iterate through group members and check for targets without the aura
    GroupReference* ref = group->GetFirstMember();
    while (ref)
    {
        Player* member = ref->GetSource();
        if (member && member->IsInWorld())
        {
            ClassID memberClass = static_cast<ClassID>(member->getClass());
            // Check if the member's class is in the target classes
            if (std::find(targetClasses.begin(), targetClasses.end(), memberClass) != targetClasses.end())
            {
                // Check if the target lacks Greater Blessing of Wisdom
                if (!botAI->HasAura("greater blessing of wisdom", member))
                {
                    return true; // Trigger is active
                }
            }
        }
        ref = ref->next();
    }

    return false; // No eligible targets found
}


bool GreaterBlessingOfKingsNeededTrigger::IsActive()
{
    // Ensure botAI and bot are valid
    if (!botAI || !botAI->GetBot())
        return false;

    // Get the bot's group
    Group* group = botAI->GetBot()->GetGroup();
    if (!group || !group->isRaidGroup())
        return false; // Only consider raid members

    // Initialize Blessing Manager
    BlessingManager blessingManager(botAI);
    blessingManager.AssignBlessings();

    // Get assigned blessings for this Paladin
    std::vector<GreaterBlessingType> blessings = blessingManager.GetAssignedBlessings(botAI);

    // Check if Greater Blessing is among the assigned blessings
    if (std::find(blessings.begin(), blessings.end(), GREATER_BLESSING_OF_KINGS) == blessings.end())
        return false; // This Paladin is not assigned to cast this Greater Blessing

    // Retrieve classes assigned to Greater Blessing for this Paladin
    std::vector<ClassID> targetClasses = blessingManager.GetClassesForBlessing(botAI, GREATER_BLESSING_OF_KINGS);

    if (targetClasses.empty())
        return false; // No classes assigned for this blessing

    // Iterate through group members and check for targets without the aura
    GroupReference* ref = group->GetFirstMember();
    while (ref)
    {
        Player* member = ref->GetSource();
        if (member && member->IsInWorld())
        {
            ClassID memberClass = static_cast<ClassID>(member->getClass());
            // Check if the member's class is in the target classes
            if (std::find(targetClasses.begin(), targetClasses.end(), memberClass) != targetClasses.end())
            {
                // Check if the target lacks Greater Blessing of Might
                if (!botAI->HasAura("greater blessing of kings", member))
                {
                    return true; // Trigger is active
                }
            }
        }
        ref = ref->next();
    }

    return false; // No eligible targets found
}

bool GreaterBlessingOfSanctuaryNeededTrigger::IsActive()
{
    // Ensure botAI and bot are valid
    if (!botAI || !botAI->GetBot())
        return false;

    // Get the bot's group
    Group* group = botAI->GetBot()->GetGroup();
    if (!group || !group->isRaidGroup())
        return false; // Only consider raid members

    // Initialize Blessing Manager
    BlessingManager blessingManager(botAI);
    blessingManager.AssignBlessings();

    // Get assigned blessings for this Paladin
    std::vector<GreaterBlessingType> blessings = blessingManager.GetAssignedBlessings(botAI);

    // Check if Greater Blessing is among the assigned blessings
    if (std::find(blessings.begin(), blessings.end(), GREATER_BLESSING_OF_SANCTUARY) == blessings.end())
        return false; // This Paladin is not assigned to cast this Greater Blessing

    // Retrieve classes assigned to Greater Blessing for this Paladin
    std::vector<ClassID> targetClasses = blessingManager.GetClassesForBlessing(botAI, GREATER_BLESSING_OF_SANCTUARY);

    if (targetClasses.empty())
        return false; // No classes assigned for this blessing

    // Iterate through group members and check for targets without the aura
    GroupReference* ref = group->GetFirstMember();
    while (ref)
    {
        Player* member = ref->GetSource();
        if (member && member->IsInWorld())
        {
            ClassID memberClass = static_cast<ClassID>(member->getClass());
            // Check if the member's class is in the target classes
            if (std::find(targetClasses.begin(), targetClasses.end(), memberClass) != targetClasses.end())
            {
                // Check if the target lacks Greater Blessing
                if (!botAI->HasAura("greater blessing of sanctuary", member))
                {
                    return true; // Trigger is active
                }
            }
        }
        ref = ref->next();
    }

    return false; // No eligible targets found
}

