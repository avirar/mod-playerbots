#ifndef _PALADINCONSTANTS_H
#define _PALADINCONSTANTS_H

#include <string>
#include <vector>
#include <map>

// Enum for Class IDs
enum ClassID
{
    WARRIOR = 1,
    ROGUE,
    PRIEST,
    DRUID,
    PALADIN,
    HUNTER,
    MAGE,
    WARLOCK,
    SHAMAN,
    DEATHKNIGHT,
    PET
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

// Extern declaration for the BlessingTemplates map
extern std::map<int, BlessingTemplate> BlessingTemplates;

#endif
