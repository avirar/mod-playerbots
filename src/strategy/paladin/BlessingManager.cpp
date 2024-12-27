#include "BlessingManager.h"
#include "Playerbots.h"
#include "Player.h"
#include "PlayerbotAI.h"

// Define the Blessing Templates based on the provided PallyPower.Templates
std::map<int, BlessingTemplate> BlessingTemplates = {
    {1, BlessingTemplate{
            {
                {WARRIOR, {GREATER_BLESSING_OF_KINGS}},
                {ROGUE, {GREATER_BLESSING_OF_KINGS}},
                {PRIEST, {GREATER_BLESSING_OF_KINGS}},
                {DRUID, {GREATER_BLESSING_OF_KINGS}},
                {PALADIN, {GREATER_BLESSING_OF_KINGS}},
                {HUNTER, {GREATER_BLESSING_OF_KINGS}},
                {MAGE, {GREATER_BLESSING_OF_KINGS}},
                {WARLOCK, {GREATER_BLESSING_OF_KINGS}},
                {SHAMAN, {GREATER_BLESSING_OF_KINGS}},
                {DEATHKNIGHT, {GREATER_BLESSING_OF_KINGS}},
                {PET, {GREATER_BLESSING_OF_KINGS}}
            }
        }
    },
    {2, BlessingTemplate{
            {
                {WARRIOR, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {ROGUE, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {PRIEST, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {DRUID, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {PALADIN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {HUNTER, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {MAGE, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {WARLOCK, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {SHAMAN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {DEATHKNIGHT, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {PET, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}}
            }
        }
    },
    {3, BlessingTemplate{
            {
                {WARRIOR, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {ROGUE, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PRIEST, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {DRUID, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {PALADIN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {HUNTER, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {MAGE, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {WARLOCK, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {SHAMAN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {DEATHKNIGHT, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PET, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}}
            }
        }
    },
    {4, BlessingTemplate{
            {
                {WARRIOR, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {ROGUE, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PRIEST, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {DRUID, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PALADIN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {HUNTER, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {MAGE, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {WARLOCK, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {SHAMAN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {DEATHKNIGHT, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PET, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}}
            }
        }
    }
};

// Constructor
BlessingManager::BlessingManager(PlayerbotAI* botAI) : botAI(botAI)
{
    AssignBlessings();
}

// Get all Paladins in the raid
std::vector<Player*> BlessingManager::GetPaladinsInRaid() const
{
    std::vector<Player*> paladins;

    // Get the bot's group
    Group* group = botAI->GetBot()->GetGroup();
    if (!group || !group->isRaidGroup())
        return paladins; // Return empty if not in a raid

    // Iterate through the group members
    GroupReference* ref = group->GetFirstMember();
    while (ref)
    {
        Player* member = ref->GetSource();
        if (member && member->IsInWorld() && member->getClass() == CLASS_PALADIN)
        {
            paladins.push_back(member); // Add Paladin to the list
        }
        ref = ref->next();
    }

    return paladins;
}

// Get target classes for a specific blessing
std::vector<ClassID> BlessingManager::GetTargetClasses(GreaterBlessingType blessingType) const
{
    std::vector<ClassID> targetClasses;
    
    // This function should reference the current template based on the number of Paladins
    // However, since this is a const function, we'll need to pass the number of Paladins or the current template
    // For simplicity, let's assume the maximum number of Paladins is 4
    
    // Find the template with the highest number of Paladins not exceeding 4
    int applicablePaladins = 1;
    if (BlessingTemplates.find(4) != BlessingTemplates.end())
        applicablePaladins = 4;
    else if (BlessingTemplates.find(3) != BlessingTemplates.end())
        applicablePaladins = 3;
    else if (BlessingTemplates.find(2) != BlessingTemplates.end())
        applicablePaladins = 2;
    else
        applicablePaladins = 1;
    
    BlessingTemplate currentTemplate = BlessingTemplates.at(applicablePaladins);
    
    for (auto const& [classId, blessings] : currentTemplate.classBlessings)
    {
        for (GreaterBlessingType blessing : blessings)
        {
            if (blessing == blessingType)
            {
                targetClasses.push_back(classId);
                break;
            }
        }
    }
    
    return targetClasses;
}

// Assign blessings based on the number of Paladins
void BlessingManager::AssignBlessings()
{
    paladinBlessings.clear();
    classBlessingPaladinMap.clear();
    
    std::vector<Player*> paladins = GetPaladinsInRaid();
    int numPaladins = paladins.size();
    
    if (numPaladins == 0)
        return; // No Paladins to assign blessings
    
    // Clamp the number of Paladins to the available templates
    if (numPaladins > 4)
        numPaladins = 4;
    
    BlessingTemplate currentTemplate = BlessingTemplates[numPaladins];
    
    for (int i = 0; i < paladins.size(); ++i)
    {
        Player* paladin = paladins[i];
        ObjectGuid paladinGuid = paladin->GetGUID();
        
        // Determine which blessings this Paladin should cast based on the template
        std::vector<GreaterBlessingType> assignedBlessings;
        
        for (auto const& [classId, blessings] : currentTemplate.classBlessings)
        {
            for (GreaterBlessingType blessing : blessings)
            {
                // Assign this blessing to the Paladin if not already assigned
                if (classBlessingPaladinMap.find(classId) == classBlessingPaladinMap.end())
                {
                    classBlessingPaladinMap[classId] = paladinGuid;
                    assignedBlessings.push_back(blessing);
                }
            }
        }
        
        paladinBlessings[paladinGuid] = assignedBlessings;
    }
}

// Get assigned blessings for a specific Paladin
std::vector<GreaterBlessingType> BlessingManager::GetAssignedBlessings(PlayerbotAI* botAI) const
{
    ObjectGuid paladinGuid = botAI->GetBot()->GetGUID();
    auto it = paladinBlessings.find(paladinGuid);
    if (it != paladinBlessings.end())
        return it->second;
    
    return {};
}

// Get classes assigned to a specific blessing for a Paladin
std::vector<ClassID> BlessingManager::GetClassesForBlessing(PlayerbotAI* botAI, GreaterBlessingType blessingType) const
{
    std::vector<ClassID> targetClasses;
    ObjectGuid paladinGuid = botAI->GetBot()->GetGUID();

    // Retrieve the list of Paladins in the raid
    std::vector<Player*> paladins = GetPaladinsInRaid();

    // Iterate through the assigned blessings for this Paladin
    auto it = paladinBlessings.find(paladinGuid);
    if (it != paladinBlessings.end())
    {
        for (GreaterBlessingType blessing : it->second)
        {
            if (blessing == blessingType)
            {
                // Find which classes are mapped to this blessing in the current template
                for (auto const& [classId, assignedPaladinGuid] : classBlessingPaladinMap)
                {
                    if (assignedPaladinGuid == paladinGuid)
                    {
                        // Check if this class has this blessing type in the current template
                        // Find the number of Paladins to determine the current template
                        int numPaladins = paladins.size();
                        if (numPaladins > 4)
                            numPaladins = 4;

                        if (BlessingTemplates.find(numPaladins) == BlessingTemplates.end())
                            numPaladins = 1; // Default to 1 if template not found

                        BlessingTemplate currentTemplate = BlessingTemplates.at(numPaladins);

                        auto classIt = currentTemplate.classBlessings.find(classId);
                        if (classIt != currentTemplate.classBlessings.end())
                        {
                            if (std::find(classIt->second.begin(), classIt->second.end(), blessingType) != classIt->second.end())
                            {
                                targetClasses.push_back(classId);
                            }
                        }
                    }
                }
            }
        }
    }

    return targetClasses;
}

