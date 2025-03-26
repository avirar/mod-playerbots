/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "CheckMountStateAction.h"
#include "BattlegroundWS.h"
#include "Event.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "SpellAuraEffects.h"

// Define the static map / init bool for caching bot preferred mount data globally
std::unordered_map<uint32, PreferredMountCache> CheckMountStateAction::mountCache;
bool CheckMountStateAction::preferredMountTableChecked = false;

MountData CollectMountData(const Player* bot)
{
    MountData data;
    for (auto& entry : bot->GetSpellMap())
    {
        uint32 spellId = entry.first;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo || spellInfo->Effects[0].ApplyAuraName != SPELL_AURA_MOUNTED)
            continue;

        if (entry.second->State == PLAYERSPELL_REMOVED || !entry.second->Active || spellInfo->IsPassive())
            continue;

        int32 effect1 = spellInfo->Effects[1].BasePoints;
        int32 effect2 = spellInfo->Effects[2].BasePoints;

        int32 speed = std::max(effect1, effect2);

        // Update max speed if appropriate.
        if (speed > data.maxSpeed)
            data.maxSpeed = speed;  // In BG, clamp max speed to 99 later; here we just store the maximum found.

        // Determine index: flight if either effect has flight aura or specific mount ID.
        uint32 index = (spellInfo->Effects[1].ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED ||
                        spellInfo->Effects[2].ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED ||
                        // Winged Steed of the Ebon Blade
                        // This mount is meant to autoscale from a 150% flyer
                        // up to a 280% as you train your flying skill up.
                        // This incorrectly gets categorised as a ground mount, force this to flyer only.
                        // TODO: Add other scaling mounts here if they have the same issue, or adjust above
                        // checks so that they are all correctly detected.
                        spellInfo->Id == 54729) ? 1 : 0;
        data.allSpells[index][speed].push_back(spellId);
    }
    return data;
}

bool CheckMountStateAction::isUseful()
{
    // Not useful when:
    if (botAI->IsInVehicle() || bot->isDead() || bot->HasUnitState(UNIT_STATE_IN_FLIGHT) ||
        !bot->IsOutdoors() || bot->InArena())
        return false;

    master = GetMaster();
    if (master == bot)
        master = nullptr;

    // Always update the bot's shapeshift form
    botInShapeshiftForm = bot->GetShapeshiftForm();

    // Update master shapeshift form only if there's a real master
    if (master)
        masterInShapeshiftForm = master->GetShapeshiftForm();
    else
        masterInShapeshiftForm = FORM_NONE;

    // Not useful when in combat and not currently mounted / travel formed
    if ((bot->IsInCombat() || botAI->GetState() == BOT_STATE_COMBAT) &&
        !bot->IsMounted() &&
        botInShapeshiftForm != FORM_TRAVEL &&
        botInShapeshiftForm != FORM_FLIGHT &&
        botInShapeshiftForm != FORM_FLIGHT_EPIC)
        return false;

    // Check if bot is slightly below ground level (false outdoor detection)
    float posZ = bot->GetPositionZ();
    float groundLevel = bot->GetMapWaterOrGroundLevel(bot->GetPositionX(), bot->GetPositionY(), posZ);

    const float groundTolerance = 0.1f; // Allow up to 0.1 units difference
    if (!bot->IsMounted() && posZ < groundLevel - groundTolerance)
        return false;

    // Not useful when bot does not have mount strategy and is not currently mounted
    if (!GET_PLAYERBOT_AI(bot)->HasStrategy("mount", BOT_STATE_NON_COMBAT) && !bot->IsMounted())
        return false;

    // Not useful when level is too low
    if (bot->GetLevel() < sPlayerbotAIConfig->useGroundMountAtMinLevel)
        return false;

    // Allow mounting while transformed only if the form allows it
    if (bot->HasAuraType(SPELL_AURA_TRANSFORM) && bot->IsInDisallowedMountForm())
        return false;

    // BG Logic
    if (bot->InBattleground())
    {
        // Do not mount if carrying a BG Flag
        if (bot->HasAura(23333) || bot->HasAura(23335) || bot->HasAura(34976))
            return false;

        // Only mount if BG starts in less than 30 sec
        if (Battleground* bg = bot->GetBattleground())
        {
            if (bg->GetStatus() == STATUS_WAIT_JOIN && bg->GetStartDelayTime() > BG_START_DELAY_30S)
                return false;
        }
    }

    // Don't mount while looting
    if (AI_VALUE(bool, "has available loot"))
    {
        Unit* lootTarget = AI_VALUE2(Unit*, "loot target", "available loot");
        if (lootTarget && bot && lootTarget->IsInWorld() && bot->IsInWorld())
        {
            if (bot->GetDistance(lootTarget) <= 30.0f)
                return false;
        }
    }

    return true;
}

bool CheckMountStateAction::Execute(Event /*event*/)
{
    // Determine if there are no attackers
    bool noAttackers = !AI_VALUE2(bool, "combat", "self target") || !AI_VALUE(uint8, "attacker count");
    bool enemy = AI_VALUE(Unit*, "enemy player target");
    bool dps = AI_VALUE(Unit*, "dps target");
    bool shouldDismount = false;
    bool shouldMount = false;

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (currentTarget)
    {
        float dismountDistance = CalculateDismountDistance();
        float mountDistance = CalculateMountDistance();
        float combatReach = bot->GetCombatReach() + currentTarget->GetCombatReach();
        float distanceToTarget = bot->GetExactDist(currentTarget);

        shouldDismount = (distanceToTarget <= dismountDistance + combatReach);
        shouldMount = (distanceToTarget > mountDistance + combatReach);
    }
    else
    {
        shouldMount = true;
    }

    // Shapeshift removal if needed
    if (shouldDismount ||
        (masterInShapeshiftForm != FORM_TRAVEL && botInShapeshiftForm == FORM_TRAVEL) ||
        (masterInShapeshiftForm != FORM_FLIGHT && botInShapeshiftForm == FORM_FLIGHT && master && master != bot && !master->IsMounted()) ||
        (masterInShapeshiftForm != FORM_FLIGHT_EPIC && botInShapeshiftForm == FORM_FLIGHT_EPIC && master && master != bot && !master->IsMounted()))
    {
        botAI->RemoveShapeshift();
    }

    // Immediate dismount
    if (shouldDismount && bot->IsMounted())
    {
        Dismount();
        return true;
    }

    bool inBattleground = bot->InBattleground();

    // Master-follow logic
    if (master && master != bot && !inBattleground)
    {
        Group* group = bot->GetGroup();
        if (!group || group->GetLeaderGUID() != master->GetGUID())
            return false;

        if (ShouldFollowMasterMountState(master, noAttackers, shouldMount))
            return Mount();

        if (ShouldDismountForMaster(master) && bot->IsMounted())
        {
            Dismount();
            return true;
        }

        return false;
    }

    // Solo or BG logic
    if ((!master || inBattleground) && !bot->IsMounted() &&
        noAttackers && shouldMount && !bot->IsInCombat())
        return Mount();

    // Dismount in combat
    if (!bot->IsFlying() && shouldDismount && bot->IsMounted() &&
        (enemy || dps || (!noAttackers && bot->IsInCombat())))
    {
        Dismount();
        return true;
    }

    return false;
}

bool CheckMountStateAction::Mount()
{
    // Remove current Shapeshift if needed
    if (botInShapeshiftForm != FORM_TRAVEL &&
        botInShapeshiftForm != FORM_FLIGHT &&
        botInShapeshiftForm != FORM_FLIGHT_EPIC)
    {
        botAI->RemoveShapeshift();
        botAI->RemoveAura("tree of life");
    }

    // Try preferred mount
    if (TryPreferredMount(master))
        return true;

    // Get mount data
    MountData mountData = CollectMountData(bot);
    int32 masterMountType = GetMountType(master);
    int32 masterSpeed = (master == bot) ? mountData.maxSpeed : CalculateMasterMountSpeed(master, mountData);

    // Try shapeshift
    if (TryForms(master, masterMountType, masterSpeed))
        return true;

    // Try random mount spell (filtered by speed and type)
    auto spellsIt = mountData.allSpells.find(masterMountType);
    if (spellsIt != mountData.allSpells.end())
    {
        auto& spells = spellsIt->second;
        if (TryRandomMountFiltered(spells, masterSpeed))
            return true;
    }

    // Try mount item
    std::vector<Item*> items = AI_VALUE2(std::vector<Item*>, "inventory items", "mount");
    if (!items.empty())
    {
        Item* item = *items.begin();
        return UseItemAuto(item);
    }

    return false;
}

void CheckMountStateAction::Dismount()
{
    if (bot->isMoving())
        bot->StopMoving();

    WorldPacket emptyPacket;
    bot->GetSession()->HandleCancelMountAuraOpcode(emptyPacket);
}

bool CheckMountStateAction::TryForms(Player* master, int32 masterMountType, int32 masterSpeed) const
{
    if (!master)
        return false;

    // If both master and bot are in matching forms or master is mounted with corresponding speed, nothing to do
    else if
        ((masterInShapeshiftForm == FORM_TRAVEL && botInShapeshiftForm == FORM_TRAVEL) ||
        ((masterInShapeshiftForm == FORM_FLIGHT || (masterMountType == 1 && masterSpeed == 149)) && botInShapeshiftForm == FORM_FLIGHT) ||
        ((masterInShapeshiftForm == FORM_FLIGHT_EPIC || (masterMountType == 1 && masterSpeed == 279)) && botInShapeshiftForm == FORM_FLIGHT_EPIC))
        return true;

    // Check if master is in Travel Form and bot can do the same
    if (botAI->CanCastSpell(SPELL_TRAVEL_FORM, bot, true) &&
        masterInShapeshiftForm == FORM_TRAVEL && botInShapeshiftForm != FORM_TRAVEL)
    {
        botAI->CastSpell(SPELL_TRAVEL_FORM, bot);
        return true;
    }

    // Check if master is in Flight Form or has a flying mount and bot can flight form
    if (botAI->CanCastSpell(SPELL_FLIGHT_FORM, bot, true) &&
        ((masterInShapeshiftForm == FORM_FLIGHT && botInShapeshiftForm != FORM_FLIGHT) ||
        (masterMountType == 1 && masterSpeed == 149)))
    {
        botAI->CastSpell(SPELL_FLIGHT_FORM, bot);

        // Compensate speedbuff
        bot->SetSpeed(MOVE_RUN, 2.5, true);
        return true;
    }

    // Check if master is in Swift Flight Form or has an epic flying mount and bot can swift flight form
    if (botAI->CanCastSpell(SPELL_SWIFT_FLIGHT_FORM, bot, true) &&
        ((masterInShapeshiftForm == FORM_FLIGHT_EPIC && botInShapeshiftForm != FORM_FLIGHT_EPIC) ||
        (masterMountType == 1 && masterSpeed == 279)))
    {
        botAI->CastSpell(SPELL_SWIFT_FLIGHT_FORM, bot);

        // Compensate speedbuff
        bot->SetSpeed(MOVE_RUN, 3.8, true);
        return true;
    }

    return false;
}

bool CheckMountStateAction::TryPreferredMount(Player* master) const
{
    uint32 botGUID = bot->GetGUID().GetRawValue();

    // Build cache (only once)
    if (!preferredMountTableChecked)
    {
        // Verify preferred mounts table existance in the database
        QueryResult checkTable = PlayerbotsDatabase.Query(
            "SELECT EXISTS(SELECT * FROM information_schema.tables WHERE table_schema = 'acore_playerbots' AND table_name = 'playerbots_preferred_mounts')");

        if (checkTable && checkTable->Fetch()[0].Get<uint32>() == 1)
        {
            preferredMountTableChecked = true;

            // Cache all mounts of both types globally, for all entries
            QueryResult result = PlayerbotsDatabase.Query("SELECT guid, spellid, type FROM playerbots_preferred_mounts");

            if (result)
            {
                uint32 totalResults = 0;
                while (auto row = result->Fetch())
                {
                    uint32 guid = row[0].Get<uint32>();
                    uint32 spellId = row[1].Get<uint32>();
                    uint32 mountType = row[2].Get<uint32>();

                    if (mountType == 0)
                        mountCache[guid].groundMounts.push_back(spellId);

                    else if (mountType == 1)
                        mountCache[guid].flightMounts.push_back(spellId);

                    totalResults++;

                    result->NextRow();
                }
                LOG_INFO("playerbots", "Preferred mounts initialized | Total records: {}", totalResults);
            }
        }
        else // If the SQL table is missing, log an error and return false
        {
            preferredMountTableChecked = true;

            LOG_DEBUG("playerbots", "Preferred mounts SQL table playerbots_preferred_mounts does not exist!");

            return false;
        }
    }

    // Pick a random preferred mount from the selection, if available
    uint32 chosenMountId = 0;

    if (GetMountType(master) == 0 && !mountCache[botGUID].groundMounts.empty())
    {
        uint32 index = urand(0, mountCache[botGUID].groundMounts.size() - 1);
        chosenMountId = mountCache[botGUID].groundMounts[index];
    }

    else if (GetMountType(master) == 1 && !mountCache[botGUID].flightMounts.empty())
    {
        uint32 index = urand(0, mountCache[botGUID].flightMounts.size() - 1);
        chosenMountId = mountCache[botGUID].flightMounts[index];
    }

    // No suitable preferred mount found
    if (chosenMountId == 0)
        return false;

    // Check if spell exists
    if (!sSpellMgr->GetSpellInfo(chosenMountId))
    {
        LOG_ERROR("playerbots", "Preferred mount failed: Invalid spell {} | Bot Guid: {}", chosenMountId, botGUID);
        return false;
    }

    // Required here as otherwise bots won't mount in BG's due to them constant moving
    if (bot->isMoving())
        bot->StopMoving();

    // Check if spell can be cast - for now allow all, even if the bot does not have the actual mount
    //if (botAI->CanCastSpell(mountId, botAI->GetBot()))
    //{
    botAI->CastSpell(chosenMountId, botAI->GetBot());
    return true;
    //}

    LOG_DEBUG("playerbots", "Preferred mount failed! | Bot Guid: {}", botGUID);
    return false;
}

bool CheckMountStateAction::TryRandomMountFiltered(const std::map<int32, std::vector<uint32>>& spells, int32 masterSpeed) const
{
    // Determine bot's maximum available mount speed
    int32 botMaxSpeed = 0;
    for (const auto& pair : spells)
        botMaxSpeed = std::max(botMaxSpeed, pair.first);

    // Sort speed tiers from highest to lowest
    std::vector<int32> speeds;
    for (const auto& pair : spells)
        speeds.push_back(pair.first);
    std::sort(speeds.begin(), speeds.end(), std::greater<int32>());

    for (int32 speed : speeds)
    {
        // Prevent solo bots from using flying mounts
        if (!master && speed > 100)
            continue;

        const auto& spellIds = spells.at(speed);
        size_t size = spellIds.size();
        if (size == 0)
            continue;

        // Random starting point in the speed tier
        uint32 startIndex = (size > 1) ? urand(0, size - 1) : 0;

        for (size_t i = 0; i < size; ++i)
        {
            uint32 index = (startIndex + i) % size;
            uint32 spellId = spellIds[index];

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!spellInfo)
                continue;

            if (bot->isMoving())
                bot->StopMoving();

            if (botAI->CanCastSpell(spellId, bot))
            {
                botAI->CastSpell(spellId, bot);
                return true;
            }
        }
    }

    return false;
}

float CheckMountStateAction::CalculateDismountDistance() const
{
    // Warrior bots should dismount far enough to charge (because it's important for generating some initial rage),
    // a real player would be riding toward enemy mashing the charge key but the bots won't cast charge while mounted.
    bool isMelee = PlayerbotAI::IsMelee(bot);
    float dismountDistance = isMelee ? sPlayerbotAIConfig->meleeDistance + 2.0f : sPlayerbotAIConfig->spellDistance + 2.0f;
    return bot->getClass() == CLASS_WARRIOR ? std::max(18.0f, dismountDistance) : dismountDistance;
}

float CheckMountStateAction::CalculateMountDistance() const
{
    // Mount distance should be >= 21 regardless of class, because when travelling a distance < 21 it takes longer
    // to cast mount-spell than the time saved from the speed increase. At a distance of 21 both approaches take 3
    // seconds:
    // 21 / 7  =  21 / 14 + 1.5  =  3   (7 = dismounted speed  14 = epic-mount speed  1.5 = mount-spell cast time)
    bool isMelee = PlayerbotAI::IsMelee(bot);
    float baseDistance = isMelee ? sPlayerbotAIConfig->meleeDistance + 10.0f : sPlayerbotAIConfig->spellDistance + 10.0f;
    return std::max(21.0f, baseDistance);
}

bool CheckMountStateAction::ShouldFollowMasterMountState(Player* master, bool noAttackers, bool shouldMount) const
{
    bool isMasterMounted = master->IsMounted() || (masterInShapeshiftForm == FORM_FLIGHT ||
                                                    masterInShapeshiftForm == FORM_FLIGHT_EPIC ||
                                                    masterInShapeshiftForm == FORM_TRAVEL);
    return isMasterMounted && !bot->IsMounted() && noAttackers &&
        shouldMount && !bot->IsInCombat() && botAI->GetState() != BOT_STATE_COMBAT;
}

bool CheckMountStateAction::ShouldDismountForMaster(Player* master) const
{
    bool isMasterMounted = master->IsMounted() || (masterInShapeshiftForm == FORM_FLIGHT ||
                                                    masterInShapeshiftForm == FORM_FLIGHT_EPIC ||
                                                    masterInShapeshiftForm == FORM_TRAVEL);
    return !isMasterMounted && bot->IsMounted();
}

int32 CheckMountStateAction::CalculateMasterMountSpeed(Player* master, const MountData& mountData) const
{
    // Check riding skill and level requirements
    int32 ridingSkill = bot->GetPureSkillValue(SKILL_RIDING);
    int32 botLevel = bot->GetLevel();

    if (ridingSkill <= 75 && botLevel < static_cast<int32>(sPlayerbotAIConfig->useFastGroundMountAtMinLevel))
        return 59;

    // If there is a master and bot not in BG, use master's aura effects.
    if (master && master != bot && !bot->InBattleground())
    {
        auto auraEffects = master->GetAuraEffectsByType(SPELL_AURA_MOUNTED);
        if (!auraEffects.empty())
        {
            SpellInfo const* masterSpell = auraEffects.front()->GetSpellInfo();
            int32 effect1 = masterSpell->Effects[1].BasePoints;
            int32 effect2 = masterSpell->Effects[2].BasePoints;
            return std::max(effect1, effect2);
        }
        else if (masterInShapeshiftForm == FORM_FLIGHT_EPIC)
            return 279;
        else if (masterInShapeshiftForm == FORM_FLIGHT)
            return 149;
    }
    else
    {
        // Bots on their own.
        int32 speed = mountData.maxSpeed;
        if (bot->InBattleground() && speed > 99)
            return 99;

        return speed;
    }

    return 59;
}

uint32 CheckMountStateAction::GetMountType(Player* master) const
{
    if (!master)
        return 0;

    auto auraEffects = master->GetAuraEffectsByType(SPELL_AURA_MOUNTED);

    if (!auraEffects.empty())
    {
        SpellInfo const* masterSpell = auraEffects.front()->GetSpellInfo();
        return (masterSpell->Effects[1].ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED ||
                masterSpell->Effects[2].ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED) ? 1 : 0;
    }
    else if (masterInShapeshiftForm == FORM_FLIGHT || masterInShapeshiftForm == FORM_FLIGHT_EPIC)
        return 1;

    return 0;
}
