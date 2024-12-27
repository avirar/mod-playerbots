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

// Assign blessings based on the number of Paladins
void BlessingManager::AssignBlessings()
{
    paladinBlessings.clear();
    
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
        // For simplicity, distribute blessings in the order defined
        std::vector<GreaterBlessingType> assignedBlessings;
        
        // Iterate through each class and assign blessings
        for (auto const& [classId, blessings] : currentTemplate.classBlessings)
        {
            // Assign blessings based on index
            // This logic can be adjusted to better distribute blessings among Paladins
            // Here, each Paladin casts the blessings in sequence
            // Alternatively, you can assign specific blessings to specific Paladins
            
            for (GreaterBlessingType blessing : blessings)
            {
                assignedBlessings.push_back(blessing);
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
