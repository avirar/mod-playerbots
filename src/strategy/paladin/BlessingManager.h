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
    // Static method to get the BlessingManager instance for a group
    static BlessingManager* getInstance(PlayerbotAI* botAI, uint64 groupId);

    // Assign blessings to Paladins based on the number in the group
    void AssignBlessings();

    // Get assigned blessings for a specific Paladin
    std::vector<GreaterBlessingType> GetAssignedBlessings(PlayerbotAI* botAI) const;

    // Get classes assigned to a specific blessing for a Paladin
    std::vector<ClassID> GetClassesForBlessing(PlayerbotAI* botAI, GreaterBlessingType blessingType) const;
    std::vector<ClassID> GetTargetClasses(GreaterBlessingType blessingType) const;

    // Remove blessings assigned by a specific Paladin
    void RemoveBlessingsByPaladin(ObjectGuid paladinGuid);

private:
    // Private constructor to enforce singleton
    BlessingManager(PlayerbotAI* botAI, uint64 groupId);
    ~BlessingManager();

    PlayerbotAI* botAI;
    uint64 groupId;

    // Mapping from Paladin GUID to their assigned blessings
    std::map<ObjectGuid, std::vector<GreaterBlessingType>> paladinBlessings;

    // Mapping from class to which Paladin is blessing it
    std::map<ClassID, std::map<GreaterBlessingType, ObjectGuid>> classBlessingPaladinMap;

    // Helper functions
    std::vector<Player*> GetPaladinsInGroup() const;
    bool CanCastBlessing(Player* paladin, GreaterBlessingType blessingType) const;

    // Define the Blessing Templates
    static std::map<int, BlessingTemplate> BlessingTemplates;

    // Static map to hold instances per group
    static std::map<uint64, BlessingManager*> instances;
};

#endif
