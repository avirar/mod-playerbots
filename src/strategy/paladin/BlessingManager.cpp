// BlessingManager.cpp

#include "BlessingManager.h"
#include "Playerbots.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include <algorithm>
#include <string>

// Define the Greater Blessing spells
std::map<int, std::string> GSpells = {
    {0, ""},
    {1, "Greater Blessing of Wisdom"},       // GREATER_BLESSING_OF_WISDOM
    {2, "Greater Blessing of Might"},        // GREATER_BLESSING_OF_MIGHT
    {3, "Greater Blessing of Kings"},        // GREATER_BLESSING_OF_KINGS
    {4, "Greater Blessing of Sanctuary"}     // GREATER_BLESSING_OF_SANCTUARY
};

// Define class IDs mapping
std::map<int, std::string> ClassIDMap = {
    {1, "WARRIOR"},
    {2, "PALADIN"},
    {3, "HUNTER"},
    {4, "ROGUE"},
    {5, "PRIEST"},
    {6, "DEATH_KNIGHT"},
    {7, "SHAMAN"},
    {8, "MAGE"},
    {9, "WARLOCK"},
    {10, "UNK"},
    {11, "DRUID"}
    // {12, "PET"} // Uncomment if pets should receive blessings
};

// Define the Blessing Templates based on the provided PallyPower.Templates
std::map<int, BlessingTemplate> BlessingManager::BlessingTemplates = {
    {1, BlessingTemplate{
            {
                {WARRIOR, {GREATER_BLESSING_OF_KINGS}},
                {PALADIN, {GREATER_BLESSING_OF_KINGS}},
                {HUNTER, {GREATER_BLESSING_OF_KINGS}},
                {ROGUE, {GREATER_BLESSING_OF_KINGS}},
                {PRIEST, {GREATER_BLESSING_OF_KINGS}},
                {DEATH_KNIGHT, {GREATER_BLESSING_OF_KINGS}},
                {SHAMAN, {GREATER_BLESSING_OF_KINGS}},
                {MAGE, {GREATER_BLESSING_OF_KINGS}},
                {WARLOCK, {GREATER_BLESSING_OF_KINGS}},
                {DRUID, {GREATER_BLESSING_OF_KINGS}},
                // {UNK, {GREATER_BLESSING_OF_KINGS}} // Uncomment if needed
                // {PET, {GREATER_BLESSING_OF_KINGS}} // Excluded as per your note
            }
        }
    },
    {2, BlessingTemplate{
            {
                {WARRIOR, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {PALADIN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {HUNTER, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {ROGUE, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {PRIEST, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {DEATH_KNIGHT, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {SHAMAN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {MAGE, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {WARLOCK, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {DRUID, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                // {UNK, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}} // Uncomment if needed
                // {PET, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}} // Excluded
            }
        }
    },
    {3, BlessingTemplate{
            {
                {WARRIOR, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PALADIN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {HUNTER, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_KINGS}},
                {ROGUE, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PRIEST, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {DEATH_KNIGHT, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {SHAMAN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                {MAGE, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {WARLOCK, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {DRUID, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_KINGS}},
                // {UNK, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}} // Uncomment if needed
                // {PET, {GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}} // Excluded
            }
        }
    },
    {4, BlessingTemplate{
            {
                {WARRIOR, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PALADIN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {HUNTER, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {ROGUE, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {PRIEST, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {DEATH_KNIGHT, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {SHAMAN, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {MAGE, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {WARLOCK, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                {DRUID, {GREATER_BLESSING_OF_WISDOM, GREATER_BLESSING_OF_MIGHT, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}},
                // {UNK, {GREATER_BLESSING_OF_Wisdom, GREATER_BLESSING_OF_Might, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}} // Uncomment if needed
                // {PET, {GREATER_BLESSING_OF_Wisdom, GREATER_BLESSING_OF_Might, GREATER_BLESSING_OF_SANCTUARY, GREATER_BLESSING_OF_KINGS}} // Excluded
            }
        }
    }
};

// Constructor
BlessingManager::BlessingManager(PlayerbotAI* botAI, uint64 groupId) 
    : botAI(botAI), groupId(groupId)
{
    AssignBlessings();
}

// Destructor
BlessingManager::~BlessingManager()
{
    // Any necessary cleanup can be performed here
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
std::vector<Player*> BlessingManager::GetPaladinsInGroup() const
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
        if (member && member->IsInWorld() && member->getClass() == PALADIN)
        {
            paladins.push_back(member); // Add Paladin to the list
        }
        ref = ref->next();
    }

    return paladins;
}

// Assign blessings based on the number of Paladins
void BlessingManager::AssignBlessings()
{
    paladinBlessings.clear();
    classBlessingPaladinMap.clear();

    std::vector<Player*> paladins = GetPaladinsInGroup();
    int numPaladins = std::min(static_cast<int>(paladins.size()), 4); // Max 4 paladins
    if (numPaladins == 0)
    {
        LOG_WARN("playerbots", "No Paladins found in the raid. Blessings not assigned.");
        return; // No Paladins to assign blessings
    }

    // Ensure the template exists
    if (BlessingTemplates.find(numPaladins) == BlessingTemplates.end())
    {
        LOG_WARN("playerbots", "No BlessingTemplate found for {} Paladins.", numPaladins);
        return;
    }

    BlessingTemplate currentTemplate = BlessingTemplates.at(numPaladins);

    // Categorize paladins based on their talents
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

    // Tracking which classes each paladin has assigned a blessing to
    std::map<ObjectGuid, std::set<ClassID>> paladinAssignedClasses;

    // Distribute blessings
    for (const auto& [classId, blessings] : currentTemplate.classBlessings)
    {
        for (const GreaterBlessingType blessing : blessings)
        {
            Player* assignedPaladin = nullptr;

            switch (blessing)
            {
                case GREATER_BLESSING_OF_SANCTUARY:
                    // Assign to a paladin with Sanctuary talent who hasn't assigned a blessing to this class
                    for (Player* paladin : paladinsWithSanctuary)
                    {
                        ObjectGuid paladinGuid = paladin->GetGUID();

                        if (paladinAssignedClasses[paladinGuid].find(classId) == paladinAssignedClasses[paladinGuid].end())
                        {
                            assignedPaladin = paladin;
                            paladinAssignedClasses[paladinGuid].insert(classId);
                            break;
                        }
                    }
                    break;

                case GREATER_BLESSING_OF_MIGHT:
                    // Assign to a paladin with Improved Might talent who hasn't assigned a blessing to this class
                    for (Player* paladin : paladinsWithMight)
                    {
                        ObjectGuid paladinGuid = paladin->GetGUID();

                        if (paladinAssignedClasses[paladinGuid].find(classId) == paladinAssignedClasses[paladinGuid].end())
                        {
                            assignedPaladin = paladin;
                            paladinAssignedClasses[paladinGuid].insert(classId);
                            break;
                        }
                    }
                    if (!assignedPaladin)
                    {
                        // Assign to any paladin without a blessing for this class
                        for (Player* paladin : paladinsWithoutBoosts)
                        {
                            ObjectGuid paladinGuid = paladin->GetGUID();

                            if (paladinAssignedClasses[paladinGuid].find(classId) == paladinAssignedClasses[paladinGuid].end())
                            {
                                assignedPaladin = paladin;
                                paladinAssignedClasses[paladinGuid].insert(classId);
                                break;
                            }
                        }

                        // If still not assigned, assign to any paladin who hasn't assigned to this class
                        if (!assignedPaladin)
                        {
                            for (Player* paladin : paladins)
                            {
                                ObjectGuid paladinGuid = paladin->GetGUID();

                                if (paladinAssignedClasses[paladinGuid].find(classId) == paladinAssignedClasses[paladinGuid].end())
                                {
                                    assignedPaladin = paladin;
                                    paladinAssignedClasses[paladinGuid].insert(classId);
                                    break;
                                }
                            }
                        }
                    }
                    break;

                case GREATER_BLESSING_OF_WISDOM:
                    // Assign to a paladin with Improved Wisdom talent who hasn't assigned a blessing to this class
                    for (Player* paladin : paladinsWithWisdom)
                    {
                        ObjectGuid paladinGuid = paladin->GetGUID();

                        if (paladinAssignedClasses[paladinGuid].find(classId) == paladinAssignedClasses[paladinGuid].end())
                        {
                            assignedPaladin = paladin;
                            paladinAssignedClasses[paladinGuid].insert(classId);
                            break;
                        }
                    }
                    if (!assignedPaladin)
                    {
                        // Assign to any paladin without a blessing for this class
                        for (Player* paladin : paladinsWithoutBoosts)
                        {
                            ObjectGuid paladinGuid = paladin->GetGUID();

                            if (paladinAssignedClasses[paladinGuid].find(classId) == paladinAssignedClasses[paladinGuid].end())
                            {
                                assignedPaladin = paladin;
                                paladinAssignedClasses[paladinGuid].insert(classId);
                                break;
                            }
                        }

                        // If still not assigned, assign to any paladin who hasn't assigned to this class
                        if (!assignedPaladin)
                        {
                            for (Player* paladin : paladins)
                            {
                                ObjectGuid paladinGuid = paladin->GetGUID();

                                if (paladinAssignedClasses[paladinGuid].find(classId) == paladinAssignedClasses[paladinGuid].end())
                                {
                                    assignedPaladin = paladin;
                                    paladinAssignedClasses[paladinGuid].insert(classId);
                                    break;
                                }
                            }
                        }
                    }
                    break;

                case GREATER_BLESSING_OF_KINGS:
                    // Assign to any paladin who hasn't assigned a blessing to this class
                    for (Player* paladin : paladinsWithoutBoosts)
                    {
                        ObjectGuid paladinGuid = paladin->GetGUID();

                        if (paladinAssignedClasses[paladinGuid].find(classId) == paladinAssignedClasses[paladinGuid].end())
                        {
                            assignedPaladin = paladin;
                            paladinAssignedClasses[paladinGuid].insert(classId);
                            break;
                        }
                    }

                    // If no paladin without boosts is available, assign to any paladin who hasn't assigned to this class
                    if (!assignedPaladin)
                    {
                        for (Player* paladin : paladins)
                        {
                            ObjectGuid paladinGuid = paladin->GetGUID();

                            if (paladinAssignedClasses[paladinGuid].find(classId) == paladinAssignedClasses[paladinGuid].end())
                            {
                                assignedPaladin = paladin;
                                paladinAssignedClasses[paladinGuid].insert(classId);
                                break;
                            }
                        }
                    }
                    break;

                default:
                    LOG_WARN("playerbots", "Unknown Blessing Type {} for ClassID {}", blessing, classId);
                    break;
            }

            if (assignedPaladin)
            {
                ObjectGuid paladinGuid = assignedPaladin->GetGUID();

                // Assign the blessing
                classBlessingPaladinMap[classId][blessing] = paladinGuid;
                paladinBlessings[paladinGuid].push_back(blessing);

                LOG_INFO("playerbots", "Assigned {} to Paladin GUID {} <{}> for Class {}",
                         GreaterBlessingTypeToString(blessing), paladinGuid.ToString().c_str(),
                         assignedPaladin->GetName().c_str(), ClassIDToString(classId));
            }
            else
            {
                LOG_WARN("playerbots", "No eligible Paladin found to assign {} for Class {}", 
                         GreaterBlessingTypeToString(blessing), ClassIDToString(classId));
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
                         result += GreaterBlessingTypeToString(blessing) + ", ";
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
    std::vector<ClassID> targetClasses;

    ObjectGuid paladinGuid = botAI->GetBot()->GetGUID();

    // Retrieve the actual number of paladins in the group
    std::vector<Player*> paladins = GetPaladinsInGroup();
    int numPaladins = std::min(static_cast<int>(paladins.size()), 4); // Max 4 paladins

    if (numPaladins == 0)
    {
        LOG_WARN("playerbots", "No Paladins found in the group. Cannot retrieve classes for blessing.");
        return targetClasses;
    }

    // Find the appropriate template
    auto templateIt = BlessingTemplates.find(numPaladins);
    if (templateIt == BlessingTemplates.end())
    {
        LOG_WARN("playerbots", "No BlessingTemplate found for {} Paladins in GetClassesForBlessing.", numPaladins);
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

    LOG_INFO("playerbots", "Paladin GUID {} assigned classes for blessing {}: {}",
             paladinGuid.ToString().c_str(), GreaterBlessingTypeToString(blessingType),
             [&]() {
                 std::string result;
                 for (auto cls : targetClasses)
                     result += ClassIDToString(cls) + ", ";
                 return result;
             }());
    return targetClasses;
}

// Remove blessings assigned by a specific Paladin
void BlessingManager::RemoveBlessingsByPaladin(ObjectGuid paladinGuid)
{
    auto it = paladinBlessings.find(paladinGuid);
    if (it != paladinBlessings.end())
    {
        for (auto blessing : it->second)
        {
            // Iterate through classBlessingPaladinMap to remove the blessing
            for (auto& [classId, blessingMap] : classBlessingPaladinMap)
            {
                auto blessingIt = blessingMap.find(blessing);
                if (blessingIt != blessingMap.end() && blessingIt->second == paladinGuid)
                {
                    blessingMap.erase(blessingIt);
                    LOG_INFO("playerbots", "Removed {} from Paladin GUID {} for Class {}",
                             GreaterBlessingTypeToString(blessing), paladinGuid.ToString().c_str(),
                             ClassIDToString(classId));
                }
            }
        }
        paladinBlessings.erase(it);
    }
}

// Utility Functions

std::string ClassIDToString(ClassID classId)
{
    auto it = ClassIDMap.find(static_cast<int>(classId));
    if (it != ClassIDMap.end())
        return it->second;
    return "Unknown";
}

std::string GreaterBlessingTypeToString(GreaterBlessingType blessingType)
{
    auto it = GSpells.find(static_cast<int>(blessingType));
    if (it != GSpells.end())
        return it->second;
    return "Unknown Blessing";
}
