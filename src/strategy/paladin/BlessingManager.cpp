#include "BlessingManager.h"
#include "Playerbots.h"
#include "Player.h"
#include "PlayerbotAI.h"

// Constructor
BlessingManager::BlessingManager(PlayerbotAI* botAI) : botAI(botAI)
{
    AssignBlessings();
}

// Get all Paladins in the raid
std::vector<Player*> BlessingManager::GetPaladinsInRaid() const
{
    std::vector<Player*> paladins;
    std::vector<Player*> raidMembers = AI_VALUE(std::vector<Player*>, "raid members");
    
    for (Player* member : raidMembers)
    {
        if (member->getClass() == CLASS_PALADIN)
            paladins.push_back(member);
    }
    
    return paladins;
}

// Get target classes for a specific blessing
std::vector<ClassID> BlessingManager::GetTargetClasses(GreaterBlessingType blessingType) const
{
    std::vector<ClassID> targetClasses;
    
    for (auto const& [classId, blessings] : BlessingTemplates.at(1).classBlessings) // Use any template for class mapping
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
