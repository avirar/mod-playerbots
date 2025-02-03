#include "PlayerbotAI.h"
#include "Group.h"
#include "Player.h"
#include "Playerbots.h"
#include <map>
#include <vector>
#include <set>
#include <algorithm>

/*
// Minimal Blessing Type enum
enum GreaterBlessingType
{
    GREATER_BLESSING_OF_WISDOM,
    GREATER_BLESSING_OF_MIGHT,
    GREATER_BLESSING_OF_KINGS,
    GREATER_BLESSING_OF_SANCTUARY
};
*/

static std::string GetGreaterBlessingSpellName(GreaterBlessingType type)
{
    switch (type)
    {
        case GREATER_BLESSING_OF_MIGHT:
            return "greater blessing of might";
        case GREATER_BLESSING_OF_WISDOM:
            return "greater blessing of wisdom";
        case GREATER_BLESSING_OF_KINGS:
            return "greater blessing of kings";
        case GREATER_BLESSING_OF_SANCTUARY:
            return "greater blessing of sanctuary";
    }
    return ""; // Fallback
}

/*

// A simple structure to hold which blessings each class should get,
// depending on how many Paladins are in the group.
static std::map<int, std::map<uint8, std::vector<GreaterBlessingType>>> BlessingTemplates =
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
    // 2 Paladins: physical classes prefer Might, casters prefer Wisdom, all get Kings
    {
        2,
        {
            { CLASS_WARRIOR,       { GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS } },
            { CLASS_PALADIN,       { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS } },
            { CLASS_HUNTER,        { GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS } },
            { CLASS_ROGUE,         { GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS } },
            { CLASS_PRIEST,        { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS } },
            { CLASS_DEATH_KNIGHT,  { GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS } },
            { CLASS_SHAMAN,        { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS } },
            { CLASS_MAGE,          { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS } },
            { CLASS_WARLOCK,       { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS } },
            { CLASS_DRUID,         { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS } },
        }
    },
    // 3 Paladins: might see some Sanctuary usage as well
    {
        3,
        {
            { CLASS_WARRIOR,       { GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_PALADIN,       { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT,     GREATER_BLESSING_OF_KINGS } },
            { CLASS_HUNTER,        { GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_WISDOM,     GREATER_BLESSING_OF_KINGS } },
            { CLASS_ROGUE,         { GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_PRIEST,        { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_DEATH_KNIGHT,  { GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_SHAMAN,        { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT,     GREATER_BLESSING_OF_KINGS } },
            { CLASS_MAGE,          { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_WARLOCK,       { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_DRUID,         { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT,     GREATER_BLESSING_OF_KINGS } },
        }
    },
    // 4 Paladins: basically everything is on the table
    {
        4,
        {
            { CLASS_WARRIOR,       { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_PALADIN,       { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_HUNTER,        { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_ROGUE,         { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_PRIEST,        { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_DEATH_KNIGHT,  { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_SHAMAN,        { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_MAGE,          { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_WARLOCK,       { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } },
            { CLASS_DRUID,         { GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS } }
        }
    }
};

*/




// -------------------------------------------------------------------------
// Simple helper to check if a Paladin has the required talent for the blessing
// (In your environment, replace HasTalent(SpellID, Spec) with the actual logic.)
// -------------------------------------------------------------------------
static bool PaladinHasTalentForBlessing(Player* paladin, GreaterBlessingType blessing)
{
    switch (blessing)
    {
        case GREATER_BLESSING_OF_WISDOM:
            // Improved Blessing of Wisdom (e.g., talent ID 20245)
            return paladin->HasTalent(20245, paladin->GetActiveSpec());
        case GREATER_BLESSING_OF_MIGHT:
            // Improved Blessing of Might (e.g., talent ID 20045)
            return paladin->HasTalent(20045, paladin->GetActiveSpec());
        case GREATER_BLESSING_OF_SANCTUARY:
            // Must have the Sanctuary talent (e.g., talent ID 20911)
            return paladin->HasTalent(20911, paladin->GetActiveSpec());
        case GREATER_BLESSING_OF_KINGS:
            // No talent required
            return true;
        default:
            return false;
    }
}

// -------------------------------------------------------------------------
// Gather all Paladins in the group/raid
// -------------------------------------------------------------------------
static std::vector<Player*> GetPaladinsInGroup(PlayerbotAI* botAI)
{
    std::vector<Player*> paladins;
    Player* bot = botAI->GetBot();

    if (!bot)
        return paladins;

    Group* group = bot->GetGroup();
    if (!group)
        return paladins;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member && member->IsInWorld() && member->getClass() == CLASS_PALADIN)
            paladins.push_back(member);
    }

    return paladins;
}

// -------------------------------------------------------------------------
// Core function: AssignBlessingsForGroup
// Returns a map from (Paladin Player*) -> (set of (classId -> assigned blessing)).
// The logic is “one greater blessing per class per paladin,” assigned in order
// of talent preference.
// -------------------------------------------------------------------------
std::map<Player*, std::map<uint8, GreaterBlessingType>> AssignBlessingsForGroup(PlayerbotAI* botAI)
{
    std::map<Player*, std::map<uint8, GreaterBlessingType>> results;

    // Get relevant Paladins
    std::vector<Player*> paladins = GetPaladinsInGroup(botAI);
    int numPaladins = std::min(static_cast<int>(paladins.size()), 4);
    if (numPaladins == 0)
    {
        // No paladins -> empty map
        return results;
    }

    // Find which template we’ll use
    auto templIt = BlessingTemplates.find(numPaladins);
    if (templIt == BlessingTemplates.end())
    {
        // If no valid template, return empty
        return results;
    }

    auto& classToBlessings = templIt->second;

    // Categorize Paladins by which improved blessings they can cast
    std::vector<Player*> paladinsWithSanctuary;
    std::vector<Player*> paladinsWithMight;
    std::vector<Player*> paladinsWithWisdom;
    std::vector<Player*> paladinsWithoutImprovements; // can only cast Kings or unimproved Might/Wisdom

    for (Player* pal : paladins)
    {
        bool canSanctuary = PaladinHasTalentForBlessing(pal, GREATER_BLESSING_OF_SANCTUARY);
        bool canMight     = PaladinHasTalentForBlessing(pal, GREATER_BLESSING_OF_MIGHT);
        bool canWisdom    = PaladinHasTalentForBlessing(pal, GREATER_BLESSING_OF_WISDOM);

        if (canSanctuary) paladinsWithSanctuary.push_back(pal);
        if (canMight)     paladinsWithMight.push_back(pal);
        if (canWisdom)    paladinsWithWisdom.push_back(pal);

        if (!canSanctuary && !canMight && !canWisdom)
            paladinsWithoutImprovements.push_back(pal);
    }

    std::map<Player*, std::set<uint8>> paladinAssignedClasses;
    // Keep track of which class each Paladin has already assigned
    // so we don't assign multiple blessings from the same Paladin to the same class.
    for (auto& [classId, blessingsVec] : classToBlessings)
    {
        for (GreaterBlessingType b : blessingsVec)
        {
            Player* assignedPaladin = nullptr;
    
            // We'll need the spell name for a quick check
            std::string blessingSpell = GetGreaterBlessingSpellName(b);
    
            switch (b)
            {
                case GREATER_BLESSING_OF_SANCTUARY:
                {
                    // Try paladins with Sanctuary talent
                    for (Player* pal : paladinsWithSanctuary)
                    {
                        if (!paladinAssignedClasses[pal].count(classId))
                        {
                            // Check if this Paladin can cast e.g. "greater blessing of sanctuary"
                            if (GET_PLAYERBOT_AI(pal) && GET_PLAYERBOT_AI(pal)->CanCastSpell(blessingSpell, pal))
                            {
                                assignedPaladin = pal;
                                paladinAssignedClasses[pal].insert(classId);
                                break;
                            }
                            // else skip this pal; continue searching
                        }
                    }
                    break;
                }
                case GREATER_BLESSING_OF_MIGHT:
                {
                    // First try improved Might paladins
                    for (Player* pal : paladinsWithMight)
                    {
                        if (!paladinAssignedClasses[pal].count(classId))
                        {
                            if (GET_PLAYERBOT_AI(pal) && GET_PLAYERBOT_AI(pal)->CanCastSpell(blessingSpell, pal))
                            {
                                assignedPaladin = pal;
                                paladinAssignedClasses[pal].insert(classId);
                                break;
                            }
                        }
                    }
                    // Otherwise try "no improvements"
                    if (!assignedPaladin)
                    {
                        for (Player* pal : paladinsWithoutImprovements)
                        {
                            if (!paladinAssignedClasses[pal].count(classId))
                            {
                                if (GET_PLAYERBOT_AI(pal) && GET_PLAYERBOT_AI(pal)->CanCastSpell(blessingSpell, pal))
                                {
                                    assignedPaladin = pal;
                                    paladinAssignedClasses[pal].insert(classId);
                                    break;
                                }
                            }
                        }
                    }
                    // Lastly pick ANY paladin if still unassigned
                    if (!assignedPaladin)
                    {
                        for (Player* pal : paladins)
                        {
                            if (!paladinAssignedClasses[pal].count(classId))
                            {
                                if (GET_PLAYERBOT_AI(pal) && GET_PLAYERBOT_AI(pal)->CanCastSpell(blessingSpell, pal))
                                {
                                    assignedPaladin = pal;
                                    paladinAssignedClasses[pal].insert(classId);
                                    break;
                                }
                            }
                        }
                    }
                    break;
                }
                case GREATER_BLESSING_OF_WISDOM:
                {
                    // Same pattern as Might, but for Wisdom
                    for (Player* pal : paladinsWithWisdom)
                    {
                        if (!paladinAssignedClasses[pal].count(classId))
                        {
                            if (GET_PLAYERBOT_AI(pal) && GET_PLAYERBOT_AI(pal)->CanCastSpell(blessingSpell, pal))
                            {
                                assignedPaladin = pal;
                                paladinAssignedClasses[pal].insert(classId);
                                break;
                            }
                        }
                    }
                    // Then paladinsWithoutImprovements, etc.
                    if (!assignedPaladin)
                    {
                        for (Player* pal : paladinsWithoutImprovements)
                        {
                            if (!paladinAssignedClasses[pal].count(classId))
                            {
                                if (GET_PLAYERBOT_AI(pal) && GET_PLAYERBOT_AI(pal)->CanCastSpell(blessingSpell, pal))
                                {
                                    assignedPaladin = pal;
                                    paladinAssignedClasses[pal].insert(classId);
                                    break;
                                }
                            }
                        }
                    }
                    if (!assignedPaladin)
                    {
                        for (Player* pal : paladins)
                        {
                            if (!paladinAssignedClasses[pal].count(classId))
                            {
                                if (GET_PLAYERBOT_AI(pal) && GET_PLAYERBOT_AI(pal)->CanCastSpell(blessingSpell, pal))
                                {
                                    assignedPaladin = pal;
                                    paladinAssignedClasses[pal].insert(classId);
                                    break;
                                }
                            }
                        }
                    }
                    break;
                }
                case GREATER_BLESSING_OF_KINGS:
                {
                    // Try no-improvement paladins first if you want them to cast kings
                    for (Player* pal : paladinsWithoutImprovements)
                    {
                        if (!paladinAssignedClasses[pal].count(classId))
                        {
                            if (GET_PLAYERBOT_AI(pal) && GET_PLAYERBOT_AI(pal)->CanCastSpell(blessingSpell, pal))
                            {
                                assignedPaladin = pal;
                                paladinAssignedClasses[pal].insert(classId);
                                break;
                            }
                        }
                    }
                    // Then any paladin
                    if (!assignedPaladin)
                    {
                        for (Player* pal : paladins)
                        {
                            if (!paladinAssignedClasses[pal].count(classId))
                            {
                                if (GET_PLAYERBOT_AI(pal) && GET_PLAYERBOT_AI(pal)->CanCastSpell(blessingSpell, pal))
                                {
                                    assignedPaladin = pal;
                                    paladinAssignedClasses[pal].insert(classId);
                                    break;
                                }
                            }
                        }
                    }
                    break;
                }
            }
    
            // If we found a Paladin who can cast it (and assigned them), 
            // record it in 'results'
            if (assignedPaladin)
            {
                results[assignedPaladin][classId] = b;
            
                std::string blessingSpell = GetGreaterBlessingSpellName(b);
            
                LOG_INFO("playerbots",
                         "AssignBlessingsForGroup: Paladin '{}' <{}> is assigned '{}' for classId {}",
                         assignedPaladin->GetName().c_str(),
                         assignedPaladin->GetGUID().ToString().c_str(),
                         blessingSpell.c_str(),
                         classId);
                break;
            }
        }
    }


    return results; // (Paladin -> (classId -> Blessing)) for the entire group
}
