#ifndef _BLESSINGMANAGER_H
#define _BLESSINGMANAGER_H

#include "PaladinConstants.h"
#include "ObjectGuid.h"
#include "Player.h"
#include <map>
#include <vector>
#include <string>

class PlayerbotAI;

class BlessingManager
{
public:
    BlessingManager(PlayerbotAI* botAI);
    
    // Assign blessings to Paladins based on the number in the raid
    void AssignBlessings();
    
    // Get assigned blessings for a specific Paladin
    std::vector<GreaterBlessingType> GetAssignedBlessings(PlayerbotAI* botAI) const;
    
    // Get target classes for a specific blessing
    std::vector<ClassID> GetTargetClasses(GreaterBlessingType blessingType) const;
    
private:
    PlayerbotAI* botAI;
    
    // Mapping from Paladin GUID to their assigned blessings
    std::map<ObjectGuid, std::vector<GreaterBlessingType>> paladinBlessings;
    
    // Mapping from class to which Paladin is blessing it
    std::map<ClassID, ObjectGuid> classBlessingPaladinMap;
    
    // Helper function to get all Paladins in the raid
    std::vector<Player*> GetPaladinsInRaid() const;
};

#endif
