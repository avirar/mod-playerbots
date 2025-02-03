#include "BlessingManager.h"

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

    // Get all paladins in the group.
    std::vector<Player*> paladins = GetPaladinsInGroup(botAI);
    if (paladins.empty())
        return results;

    // Sort paladins alphabetically for a stable order.
    std::sort(paladins.begin(), paladins.end(), [](Player* a, Player* b) {
        return a->GetName() < b->GetName();
    });

    // Use the number of paladins (capped at 4) to choose the correct template.
    int numPaladins = std::min(static_cast<int>(paladins.size()), 4);
    auto templIt = BlessingTemplates.find(numPaladins);
    if (templIt == BlessingTemplates.end())
        return results;
    auto& classToBlessings = templIt->second;

    // For each target class (for example, CLASS_WARRIOR, CLASS_HUNTER, etc.)
    for (auto& [classId, blessingVector] : classToBlessings)
    {
        // This set will track which paladins have already been assigned a blessing for this class.
        std::set<Player*> assignedPaladins;

        // For each blessing option in the order given by the template:
        for (GreaterBlessingType blessing : blessingVector)
        {
            // Build a list of candidate paladins not yet assigned a blessing for this class.
            std::vector<Player*> candidates;
            for (Player* pal : paladins)
            {
                if (assignedPaladins.find(pal) != assignedPaladins.end())
                    continue; // already assigned for this class

                // For Sanctuary, the paladin must have the talent.
                if (blessing == GREATER_BLESSING_OF_SANCTUARY && !PaladinHasTalentForBlessing(pal, blessing))
                    continue;

                // For Might and Wisdom, include every candidate first.
                candidates.push_back(pal);
            }

            // For improved blessings (Might or Wisdom), prioritize those with the improved talent.
            if (blessing == GREATER_BLESSING_OF_MIGHT || blessing == GREATER_BLESSING_OF_WISDOM)
            {
                bool anyHasImproved = false;
                for (Player* candidate : candidates)
                {
                    if (PaladinHasTalentForBlessing(candidate, blessing))
                    {
                        anyHasImproved = true;
                        break;
                    }
                }
                if (anyHasImproved)
                {
                    std::vector<Player*> filtered;
                    for (Player* candidate : candidates)
                    {
                        if (PaladinHasTalentForBlessing(candidate, blessing))
                            filtered.push_back(candidate);
                    }
                    candidates = filtered;
                }
            }

            // If we have any candidates, assign the blessing to the first one.
            if (!candidates.empty())
            {
                // The candidates list remains sorted (since paladins was sorted).
                Player* chosenPal = candidates.front();
                results[chosenPal][classId] = blessing;
                assignedPaladins.insert(chosenPal);

                std::string blessingSpell = GetGreaterBlessingSpellName(blessing);
                LOG_INFO("playerbots",
                         "AssignBlessingsForGroup: Paladin '{}' <{}> is assigned '{}' for classId {}",
                         chosenPal->GetName(),
                         chosenPal->GetGUID().ToString(),
                         blessingSpell,
                         classId);
            }
        }
    }

    return results;
}

