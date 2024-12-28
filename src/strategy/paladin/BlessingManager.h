// BlessingManager.h

#ifndef _BLESSINGMANAGER_H
#define _BLESSINGMANAGER_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "ObjectGuid.h"
#include "Player.h"

// Enum for Class IDs aligned with the server's Classes enum
enum ClassID
{
    WARRIOR       = 1, // CLASS_WARRIOR
    PALADIN       = 2, // CLASS_PALADIN
    HUNTER        = 3, // CLASS_HUNTER
    ROGUE         = 4, // CLASS_ROGUE
    PRIEST        = 5, // CLASS_PRIEST
    DEATH_KNIGHT  = 6, // CLASS_DEATH_KNIGHT
    SHAMAN        = 7, // CLASS_SHAMAN
    MAGE          = 8, // CLASS_MAGE
    WARLOCK       = 9, // CLASS_WARLOCK
    UNK           = 10, // CLASS_UNK (if applicable)
    DRUID         = 11  // CLASS_DRUID
};

// Enum for Greater Blessing Types
enum GreaterBlessingType
{
    GREATER_BLESSING_OF_WISDOM = 1,      // GSpells[1]
    GREATER_BLESSING_OF_MIGHT,           // GSpells[2]
    GREATER_BLESSING_OF_KINGS,           // GSpells[3]
    GREATER_BLESSING_OF_SANCTUARY        // GSpells[4]
};

// Struct for Blessing Assignment per Paladin
struct BlessingAssignment
{
    std::vector<GreaterBlessingType> blessings;
};

// Struct for Templates based on number of Paladins
struct BlessingTemplate
{
    std::map<ClassID, std::vector<GreaterBlessingType>> classBlessings;
};

class PlayerbotAI; // Forward declaration

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

    // Get target classes for a specific blessing (possibly used elsewhere)
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

    // Static map to hold instances per group using raw pointers (to be updated to smart pointers)
    static std::map<uint64, std::unique_ptr<BlessingManager>> instances;

    // Static method to clean up a specific instance by groupId
    static void cleanupInstance(uint64 groupId);

};

std::string GreaterBlessingTypeToString(GreaterBlessingType blessingType);
std::string ClassIDToString(ClassID classId);

#endif
