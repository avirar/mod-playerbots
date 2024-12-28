#ifndef _PALADINCONSTANTS_H
#define _PALADINCONSTANTS_H

#include <string>
#include <vector>
#include <map>

// Enum for Class IDs
enum ClassID
{
    CLASS_WARRIOR       = 1, // TITLE Warrior
    CLASS_PALADIN       = 2, // TITLE Paladin
    CLASS_HUNTER        = 3, // TITLE Hunter
    CLASS_ROGUE         = 4, // TITLE Rogue
    CLASS_PRIEST        = 5, // TITLE Priest
    CLASS_DEATH_KNIGHT  = 6, // TITLE Death Knight
    CLASS_SHAMAN        = 7, // TITLE Shaman
    CLASS_MAGE          = 8, // TITLE Mage
    CLASS_WARLOCK       = 9, // TITLE Warlock
    //CLASS_UNK           = 10,
    CLASS_DRUID         = 11 // TITLE Druid
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
