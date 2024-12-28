#include "BlessingManager.h"
#include "Playerbots.h"
#include "Player.h"
#include "PlayerbotAI.h"

// Define the Blessing Templates based on the provided PallyPower.Templates
std::map<int, BlessingTemplate> BlessingTemplates = {
    {1, BlessingTemplate{
            {
                {WARRIOR, {GREATER_BLESSING_OF_KINGS}},
                {ROGUE, {GREATER_BLESSING_OF_KINGS}},
                {PRIEST, {GREATER_BLESSING_OF_KINGS}},
                {DRUID, {GREATER_BLESSING_OF_KINGS}},
                {PALADIN, {GREATER_BLESSING_OF_KINGS}},
                {HUNTER, {GREATER_BLESSING_OF_KINGS}},
                {MAGE, {GREATER_BLESSING_OF_KINGS}},
                {WARLOCK, {GREATER_BLESSING_OF_KINGS}},
                {SHAMAN, {GREATER_BLESSING_OF_KINGS}},
                {DEATHKNIGHT, {GREATER_BLESSING_OF_KINGS}},
                {PET, {GREATER_BLESSING_OF_KINGS}}
            }
        }
    },
    {2, BlessingTemplate{
            {
                {WARRIOR, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {ROGUE, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {PRIEST, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {DRUID, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {PALADIN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {HUNTER, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {MAGE, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {WARLOCK, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {SHAMAN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {DEATHKNIGHT, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {PET, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}}
            }
        }
    },
    {3, BlessingTemplate{
            {
                {WARRIOR, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {ROGUE, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PRIEST, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {DRUID, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {PALADIN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {HUNTER, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {MAGE, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {WARLOCK, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {SHAMAN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {DEATHKNIGHT, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PET, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}}
            }
        }
    },
    {4, BlessingTemplate{
            {
                {WARRIOR, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {ROGUE, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PRIEST, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {DRUID, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PALADIN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {HUNTER, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {MAGE, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {WARLOCK, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {SHAMAN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {DEATHKNIGHT, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PET, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}}
            }
        }
    }
};

// Constructor
BlessingManager::BlessingManager(PlayerbotAI* botAI) : botAI(botAI)
{
    AssignBlessings();
}

// Get all Paladins in the raid
std::vector<Player*> BlessingManager::GetPaladinsInRaid() const
{
    std::vector<Player*> paladins;

    // Get the bot's group
    Group* group = botAI->GetBot()->GetGroup();
    if (!group || !group->isRaidGroup())
        return paladins; // Return empty if not in a raid

    // Iterate through the group members
    GroupReference* ref = group->GetFirstMember();
    while (ref)
    {
        Player* member = ref->GetSource();
        if (member && member->IsInWorld() && member->getClass() == CLASS_PALADIN)
        {
            paladins.push_back(member); // Add Paladin to the list
        }
        ref = ref->next();
    }

    return paladins;
}

// Get target classes for a specific blessing
std::vector<ClassID> BlessingManager::GetTargetClasses(GreaterBlessingType blessingType) const
{
    std::vector<ClassID> targetClasses;
    
    // This function should reference the current template based on the number of Paladins
    // However, since this is a const function, we'll need to pass the number of Paladins or the current template
    // For simplicity, let's assume the maximum number of Paladins is 4
    
    // Find the template with the highest number of Paladins not exceeding 4
    int applicablePaladins = 1;
    if (BlessingTemplates.find(4) != BlessingTemplates.end())
        applicablePaladins = 4;
    else if (BlessingTemplates.find(3) != BlessingTemplates.end())
        applicablePaladins = 3;
    else if (BlessingTemplates.find(2) != BlessingTemplates.end())
        applicablePaladins = 2;
    else
        applicablePaladins = 1;
    
    BlessingTemplate currentTemplate = BlessingTemplates.at(applicablePaladins);
    
    for (auto const& [classId, blessings] : currentTemplate.classBlessings)
    {
        for (GreaterBlessingType blessing : blessings)
        {
            if (blessing == blessingType)
            {
                targetClasses.push_back(classId);
                break;
            }
        }
    }
    
    return targetClasses;
}

// Assign blessings based on the number of Paladins
void BlessingManager::AssignBlessings()
{
    paladinBlessings.clear();
    classBlessingPaladinMap.clear();

    std::vector<Player*> paladins = GetPaladinsInRaid();
    int numPaladins = std::min(static_cast<int>(paladins.size()), 4); // Max 4 paladins
    if (numPaladins == 0)
        return; // No Paladins to assign blessings

    BlessingTemplate currentTemplate = BlessingTemplates.at(numPaladins);

    // Create a priority map for paladins based on their talents
    std::map<Player*, int> paladinPriority;
    for (Player* paladin : paladins)
    {
        int priority = 0;

        if (paladin->HasTalent(20045, paladin->GetActiveSpec())) // Improved Blessing of Might
            priority += 10;

        if (paladin->HasTalent(20245, paladin->GetActiveSpec())) // Improved Blessing of Wisdom
            priority += 10;

        if (paladin->HasTalent(20911, paladin->GetActiveSpec())) // Greater Blessing of Sanctuary
            priority += 20;

        paladinPriority[paladin] = priority;
    }

    // Sort paladins by priority
    std::vector<Player*> sortedPaladins(paladins.begin(), paladins.end());
    std::sort(sortedPaladins.begin(), sortedPaladins.end(),
              [&paladinPriority](Player* a, Player* b) {
                  return paladinPriority[a] > paladinPriority[b];
              });

    // Distribute blessings
    for (auto const& [classId, blessings] : currentTemplate.classBlessings)
    {
        for (GreaterBlessingType blessing : blessings)
        {
            bool blessingAssigned = false;

            for (Player* paladin : sortedPaladins)
            {
                ObjectGuid paladinGuid = paladin->GetGUID();

                // Skip Sanctuary if the paladin lacks the talent
                if (blessing == GREATER_BLESSING_OF_SANCTUARY &&
                    !paladin->HasTalent(20911, paladin->GetActiveSpec()))
                {
                    continue;
                }

                // Assign this blessing to the first eligible paladin
                if (paladinBlessings[paladinGuid].size() < blessings.size())
                {
                    classBlessingPaladinMap[classId] = paladinGuid;
                    paladinBlessings[paladinGuid].push_back(blessing);
                    blessingAssigned = true;
                    LOG_INFO("playerbots", "Assigned {} to Paladin {} <{}> for ClassID {}",
                             blessing, paladinGuid.ToString().c_str(), paladin->GetName().c_str(), classId);
                    break;
                }
            }

            // If no paladin could be assigned this blessing, log a warning
            if (!blessingAssigned)
            {
                LOG_WARN("playerbots", "No eligible Paladin found to assign {} for ClassID {}",
                         blessing, classId);
            }
        }
    }

    // Final log of all assignments
    for (Player* paladin : paladins)
    {
        ObjectGuid paladinGuid = paladin->GetGUID();
        auto assignedBlessings = paladinBlessings[paladinGuid];
        LOG_INFO("playerbots", "Final blessings for Paladin {} <{}>: {}", 
                 paladinGuid.ToString().c_str(), paladin->GetName().c_str(),
                 [&]() {
                     std::string result;
                     for (auto blessing : assignedBlessings)
                         result += std::to_string(blessing) + ", ";
                     return result;
                 }());
    }
}




// Get assigned blessings for a specific Paladin
std::vector<GreaterBlessingType> BlessingManager::GetAssignedBlessings(PlayerbotAI* botAI) const
{
    ObjectGuid paladinGuid = botAI->GetBot()->GetGUID();
    auto it = paladinBlessings.find(paladinGuid);
    if (it != paladinBlessings.end())
    {
        LOG_INFO("playerbots", "Retrieved assigned blessings for Paladin {}: {}",
                 paladinGuid.ToString().c_str(),
                 [&]() {
                     std::string result;
                     for (auto blessing : it->second)
                         result += std::to_string(blessing) + ", ";
                     return result;
                 }());
        return it->second;
    }

    LOG_INFO("playerbots", "No blessings assigned to Paladin {}",
             paladinGuid.ToString().c_str());
    return {};
}


// Get classes assigned to a specific blessing for a Paladin
std::vector<ClassID> BlessingManager::GetClassesForBlessing(PlayerbotAI* botAI, GreaterBlessingType blessingType) const
{
    ObjectGuid paladinGuid = botAI->GetBot()->GetGUID();
    std::vector<ClassID> targetClasses;

    auto it = paladinBlessings.find(paladinGuid);
    if (it == paladinBlessings.end())
    {
        LOG_INFO("playerbots", "Paladin {} has no assigned blessings for GetClassesForBlessing",
                 paladinGuid.ToString().c_str());
        return targetClasses;
    }

    for (auto const& [classId, assignedPaladinGuid] : classBlessingPaladinMap)
    {
        if (assignedPaladinGuid == paladinGuid)
        {
            auto classIt = BlessingTemplates.at(4).classBlessings.find(classId); // Use the template for 4 paladins
            if (classIt != BlessingTemplates.at(4).classBlessings.end())
            {
                if (std::find(classIt->second.begin(), classIt->second.end(), blessingType) != classIt->second.end())
                    targetClasses.push_back(classId);
            }
        }
    }

    LOG_INFO("playerbots", "Paladin {} assigned classes for blessing {}: {}",
             paladinGuid.ToString().c_str(), blessingType,
             [&]() {
                 std::string result;
                 for (auto cls : targetClasses)
                     result += std::to_string(cls) + ", ";
                 return result;
             }());
    return targetClasses;
}


