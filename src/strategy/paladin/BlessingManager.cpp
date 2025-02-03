#include "BlessingManager.h"

#include "PlayerbotAI.h"
#include "Group.h"
#include "Player.h"
#include "Playerbots.h"
#include <map>
#include <vector>
#include <set>
#include <algorithm>

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
        if (!member || !member->IsInWorld() || member->getClass() != CLASS_PALADIN)
            continue;

        // Ignore dead paladins
        if (!member->IsAlive())
            continue;

        // Ignore paladins that are out of range (>30 yards)
        if (!bot->IsWithinDistInMap(member, 30.0f))
            continue;

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

