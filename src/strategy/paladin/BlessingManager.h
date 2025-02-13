#ifndef _BLESSING_ASSIGNMENT_H_
#define _BLESSING_ASSIGNMENT_H_

#include "PlayerbotAI.h"
#include "Group.h"
#include "Player.h"

#include <map>
#include <vector>
#include <set>
#include <algorithm>


// Minimal Blessing Type enum
enum GreaterBlessingType
{
    GREATER_BLESSING_OF_WISDOM,
    GREATER_BLESSING_OF_MIGHT,
    GREATER_BLESSING_OF_KINGS,
    GREATER_BLESSING_OF_SANCTUARY
};

// -----------------------------------------------------------------------------
// A simple structure to hold which blessings each class should get depending on how many Paladins are in the group.
// This does dictate the priority that blessings are applied to classes, assuming paladins have the necessary talents
// -----------------------------------------------------------------------------
inline static std::map<int, std::map<uint8 /*classId*/, std::vector<GreaterBlessingType>>> BlessingTemplates =
{
    // 1 Paladin: everyone just gets Kings
    {
        1,
        {
            { CLASS_WARRIOR,       { GREATER_BLESSING_OF_KINGS } },
            { CLASS_PALADIN,       { GREATER_BLESSING_OF_KINGS } },
            { CLASS_HUNTER,        { GREATER_BLESSING_OF_KINGS } },
            { CLASS_ROGUE,         { GREATER_BLESSING_OF_KINGS } },
            { CLASS_PRIEST,        { GREATER_BLESSING_OF_KINGS } },
            { CLASS_DEATH_KNIGHT,  { GREATER_BLESSING_OF_KINGS } },
            { CLASS_SHAMAN,        { GREATER_BLESSING_OF_KINGS } },
            { CLASS_MAGE,          { GREATER_BLESSING_OF_KINGS } },
            { CLASS_WARLOCK,       { GREATER_BLESSING_OF_KINGS } },
            { CLASS_DRUID,         { GREATER_BLESSING_OF_KINGS } }
        }
    },
    // 2 Paladins: physical classes prefer Might, casters prefer Wisdom, tanks might get Kings
    {
        2,
        {
            { CLASS_WARRIOR,        { GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_MIGHT } },  
            { CLASS_PALADIN,        { GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_HUNTER,         { GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_MIGHT } },  
            { CLASS_ROGUE,          { GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_MIGHT } },  
            { CLASS_ PRIEST,        { GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_DEATH_KNIGHT,   { GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_MIGHT } },  
            { CLASS_SHAMAN,         { GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_MAGE,           { GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_WARLOCK,        { GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_DRUID,          { GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } } 
        }
    },
    // 3 Paladins: Sanctuary first so tank paladins cast it reliably, then Might/Wis, and Kings always last
    {
        3,
        {
            { CLASS_WARRIOR,       { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_MIGHT } },  
            { CLASS_PALADIN,       { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_HUNTER,        { GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_ROGUE,         { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_MIGHT } },  
            { CLASS_PRIEST,        { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_DEATH_KNIGHT,  { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_MIGHT } },  
            { CLASS_SHAMAN,        { GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT } },  
            { CLASS_MAGE,          { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_WARLOCK,       { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_DRUID,         { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } }   
        }
    },
    // 4 Paladins: Sanctuary always first, Kings always last
    {
        4,
        {
            { CLASS_WARRIOR,       { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_MIGHT } },  
            { CLASS_PALADIN,       { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT } },  
            { CLASS_HUNTER,        { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_ROGUE,         { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_MIGHT } },  
            { CLASS_PRIEST,        { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_DEATH_KNIGHT,  { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_MIGHT } },  
            { CLASS_SHAMAN,        { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_MAGE,          { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM } },  
            { CLASS_WARLOCK,       { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT } },  
            { CLASS_DRUID,         { GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS, GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT } }   
        }
    }
};

// Checks if a Paladin has a talent to cast (e.g Sanctuary), or to Improve the given blessing.
static bool PaladinHasTalentForBlessing(Player* paladin, GreaterBlessingType blessing);

// Returns all Paladins (Player*) in the same group/raid as the bot and within 30yd range.
static std::vector<Player*> GetPaladinsInGroup(PlayerbotAI* botAI);

// Main function to assign blessings for the current group/raid.
// Returns a map of (Paladin pointer) -> (classId -> assigned blessing).
std::map<Player*, std::map<uint8, GreaterBlessingType>> AssignBlessingsForGroup(PlayerbotAI* botAI);

#endif // _BLESSING_ASSIGNMENT_H_
