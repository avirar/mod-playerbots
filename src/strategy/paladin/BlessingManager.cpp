// BlessingManager.cpp

#include "BlessingManager.h"
#include "Playerbots.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include <algorithm>
#include <string>

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

// Helper function to check if a paladin has the required talent for a blessing
bool BlessingManager::CanCastBlessing(Player* paladin, GreaterBlessingType blessingType) const
{
    switch (blessingType)
    {
        case GREATER_BLESSING_OF_WISDOM:
            return paladin->HasTalent(20245, paladin->GetActiveSpec()); // Improved Blessing of Wisdom
        case GREATER_BLESSING_OF_MIGHT:
            return paladin->HasTalent(20045, paladin->GetActiveSpec()); // Improved Blessing of Might
        case GREATER_BLESSING_OF_SANCTUARY:
            return paladin->HasTalent(20911, paladin->GetActiveSpec()); // Greater Blessing of Sanctuary
        case GREATER_BLESSING_OF_KINGS:
            return true; // No talent required
        default:
            return false;
    }
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

    // Determine the number of paladins, capped at 4
    int numPaladins = 1;
    if (!BlessingTemplates.empty())
    {
        // Assuming the highest key corresponds to the current number of paladins
        numPaladins = BlessingTemplates.rbegin()->first;
        numPaladins = std::min(numPaladins, 4);
    }

    // Find the appropriate template
    auto templateIt = BlessingTemplates.find(numPaladins);
    if (templateIt == BlessingTemplates.end())
    {
        LOG_WARN("playerbots", "No BlessingTemplate found for {} Paladins in GetTargetClasses.", numPaladins);
        return targetClasses;
    }

    BlessingTemplate currentTemplate = templateIt->second;

    for (const auto& [classId, blessings] : currentTemplate.classBlessings)
    {
        // Check if the blessingType is part of the blessings for this class
        if (std::find(blessings.begin(), blessings.end(), blessingType) != blessings.end())
        {
            targetClasses.push_back(classId);
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

    // Ensure the template exists
    if (BlessingTemplates.find(numPaladins) == BlessingTemplates.end())
    {
        LOG_WARN("playerbots", "No BlessingTemplate found for {} Paladins.", numPaladins);
        return;
    }

    BlessingTemplate currentTemplate = BlessingTemplates.at(numPaladins);

    // Create categorized lists of paladins based on their talents
    std::vector<Player*> paladinsWithSanctuary;
    std::vector<Player*> paladinsWithMight;
    std::vector<Player*> paladinsWithWisdom;
    std::vector<Player*> paladinsWithoutBoosts;

    for (Player* paladin : paladins)
    {
        bool hasSanctuary = CanCastBlessing(paladin, GREATER_BLESSING_OF_SANCTUARY);
        bool hasMight = CanCastBlessing(paladin, GREATER_BLESSING_OF_MIGHT);
        bool hasWisdom = CanCastBlessing(paladin, GREATER_BLESSING_OF_WISDOM);

        if (hasSanctuary)
            paladinsWithSanctuary.push_back(paladin);
        if (hasMight)
            paladinsWithMight.push_back(paladin);
        if (hasWisdom)
            paladinsWithWisdom.push_back(paladin);
        if (!hasSanctuary && !hasMight && !hasWisdom)
            paladinsWithoutBoosts.push_back(paladin);
    }

    // Distribute blessings
    for (const auto& [classId, blessings] : currentTemplate.classBlessings)
    {
        for (const GreaterBlessingType blessing : blessings)
        {
            Player* assignedPaladin = nullptr;

            switch (blessing)
            {
                case GREATER_BLESSING_OF_SANCTUARY:
                    // Assign to a paladin with Sanctuary talent
                    for (Player* paladin : paladinsWithSanctuary)
                    {
                        // Check if the paladin hasn't assigned a blessing to this class
                        if (std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                     paladinBlessings[paladin->GetGUID()].end(),
                                     blessing) == paladinBlessings[paladin->GetGUID()].end())
                        {
                            assignedPaladin = paladin;
                            break;
                        }
                    }
                    break;

                case GREATER_BLESSING_OF_MIGHT:
                    // Assign to a paladin with Improved Might talent
                    for (Player* paladin : paladinsWithMight)
                    {
                        if (std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                     paladinBlessings[paladin->GetGUID()].end(),
                                     blessing) == paladinBlessings[paladin->GetGUID()].end())
                        {
                            assignedPaladin = paladin;
                            break;
                        }
                    }
                    if (!assignedPaladin)
                    {
                        // Assign to any paladin who hasn't assigned a blessing to this class
                        for (Player* paladin : paladins)
                        {
                            if (std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                         paladinBlessings[paladin->GetGUID()].end(),
                                         GREATER_BLESSING_OF_SANCTUARY) == paladinBlessings[paladin->GetGUID()].end() &&
                                std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                         paladinBlessings[paladin->GetGUID()].end(),
                                         GREATER_BLESSING_OF_MIGHT) == paladinBlessings[paladin->GetGUID()].end() &&
                                std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                         paladinBlessings[paladin->GetGUID()].end(),
                                         GREATER_BLESSING_OF_WISDOM) == paladinBlessings[paladin->GetGUID()].end() &&
                                std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                         paladinBlessings[paladin->GetGUID()].end(),
                                         GREATER_BLESSING_OF_KINGS) == paladinBlessings[paladin->GetGUID()].end())
                            {
                                assignedPaladin = paladin;
                                break;
                            }
                        }
                    }
                    break;

                case GREATER_BLESSING_OF_WISDOM:
                    // Assign to a paladin with Improved Wisdom talent
                    for (Player* paladin : paladinsWithWisdom)
                    {
                        if (std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                     paladinBlessings[paladin->GetGUID()].end(),
                                     blessing) == paladinBlessings[paladin->GetGUID()].end())
                        {
                            assignedPaladin = paladin;
                            break;
                        }
                    }
                    if (!assignedPaladin)
                    {
                        // Assign to any paladin who hasn't assigned a blessing to this class
                        for (Player* paladin : paladins)
                        {
                            if (std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                         paladinBlessings[paladin->GetGUID()].end(),
                                         GREATER_BLESSING_OF_SANCTUARY) == paladinBlessings[paladin->GetGUID()].end() &&
                                std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                         paladinBlessings[paladin->GetGUID()].end(),
                                         GREATER_BLESSING_OF_MIGHT) == paladinBlessings[paladin->GetGUID()].end() &&
                                std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                         paladinBlessings[paladin->GetGUID()].end(),
                                         GREATER_BLESSING_OF_WISDOM) == paladinBlessings[paladin->GetGUID()].end() &&
                                std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                         paladinBlessings[paladin->GetGUID()].end(),
                                         GREATER_BLESSING_OF_KINGS) == paladinBlessings[paladin->GetGUID()].end())
                            {
                                assignedPaladin = paladin;
                                break;
                            }
                        }
                    }
                    break;

                case GREATER_BLESSING_OF_KINGS:
                    // Assign to any paladin who hasn't assigned a blessing to this class
                    for (Player* paladin : paladins)
                    {
                        if (std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                     paladinBlessings[paladin->GetGUID()].end(),
                                     GREATER_BLESSING_OF_SANCTUARY) == paladinBlessings[paladin->GetGUID()].end() &&
                            std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                     paladinBlessings[paladin->GetGUID()].end(),
                                     GREATER_BLESSING_OF_MIGHT) == paladinBlessings[paladin->GetGUID()].end() &&
                            std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                     paladinBlessings[paladin->GetGUID()].end(),
                                     GREATER_BLESSING_OF_WISDOM) == paladinBlessings[paladin->GetGUID()].end() &&
                            std::find(paladinBlessings[paladin->GetGUID()].begin(),
                                     paladinBlessings[paladin->GetGUID()].end(),
                                     GREATER_BLESSING_OF_KINGS) == paladinBlessings[paladin->GetGUID()].end())
                        {
                            assignedPaladin = paladin;
                            break;
                        }
                    }
                    break;

                default:
                    break;
            }

            if (assignedPaladin)
            {
                ObjectGuid paladinGuid = assignedPaladin->GetGUID();

                // Assign the blessing
                classBlessingPaladinMap[classId][blessing] = paladinGuid;
                paladinBlessings[paladinGuid].push_back(blessing);

                LOG_INFO("playerbots", "Assigned {} to Paladin GUID {} for ClassID {}",
                         blessing, paladinGuid.ToString().c_str(), classId);
            }
            else
            {
                LOG_WARN("playerbots", "No eligible Paladin found to assign {} for ClassID {}",
                         blessing, classId);
            }
        }
    }
}


// Get assigned blessings for a specific Paladin
std::vector<GreaterBlessingType> BlessingManager::GetAssignedBlessings(PlayerbotAI* botAI) const
{
    ObjectGuid paladinGuid = botAI->GetBot()->GetGUID();
    auto it = paladinBlessings.find(paladinGuid);
    if (it != paladinBlessings.end())
    {
        LOG_INFO("playerbots", "Retrieved assigned blessings for Paladin GUID {}: {}",
                 paladinGuid.ToString().c_str(),
                 [&]() {
                     std::string result;
                     for (auto blessing : it->second)
                         result += std::to_string(blessing) + ", ";
                     return result;
                 }());
        return it->second;
    }

    LOG_INFO("playerbots", "No blessings assigned to Paladin GUID {}",
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
        LOG_INFO("playerbots", "Paladin GUID {} has no assigned blessings for GetClassesForBlessing",
                 paladinGuid.ToString().c_str());
        return targetClasses;
    }

    for (const auto& [classId, blessingsMap] : classBlessingPaladinMap)
    {
        auto blessingIt = blessingsMap.find(blessingType);
        if (blessingIt != blessingsMap.end() && blessingIt->second == paladinGuid)
        {
            targetClasses.push_back(classId);
        }
    }

    LOG_INFO("playerbots", "Paladin GUID {} assigned classes for blessing {}: {}",
             paladinGuid.ToString().c_str(), blessingType,
             [&]() {
                 std::string result;
                 for (auto cls : targetClasses)
                     result += std::to_string(cls) + ", ";
                 return result;
             }());
    return targetClasses;
}
