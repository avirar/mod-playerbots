/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NEWRPGINFO_H
#define PLAYERBOTS_NEWRPGINFO_H

#include <unordered_set>

#include "Define.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Strategy.h"
#include "Timer.h"
#include "TravelMgr.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

using NewRpgStatusTransitionProb = std::vector<std::vector<int>>;

struct NewRpgInfo
{
    NewRpgInfo() : data(Idle{}) {}
    ~NewRpgInfo() = default;

    // RPG_GO_GRIND
    struct GoGrind
    {
        WorldPosition pos{};
    };
    // RPG_GO_CAMP
    struct GoCamp
    {
        WorldPosition pos{};
    };
    // RPG_WANDER_NPC
    struct WanderNpc
    {
        ObjectGuid npcOrGo{};
        uint32 lastReach{0};
    };
    // RPG_WANDER_RANDOM
    struct WanderRandom
    {
        WanderRandom() = default;
    };
    // RPG_DO_QUEST
    struct DoQuest
    {
        Quest const* quest{nullptr};
        uint32 questId{0};
        int32 objectiveIdx{0};
        WorldPosition pos{};
        uint32 lastReachPOI{0};
    };
    // RPG_TRAVEL_FLIGHT
    struct TravelFlight
    {
        uint32 flightMasterEntry{0};
        WorldPosition flightMasterPos{};
        std::vector<uint32> path;
        bool inFlight{false};
    };
    // RPG_REST
    struct Rest
    {
        Rest() = default;
    };
    // RPG_OUTDOOR_PVP
    struct OutdoorPvP
    {
        ObjectGuid::LowType capturePointSpawnId{0};
    };
    // RPG_DO_GATHER
    struct DoGather
    {
        ObjectGuid::LowType nodeSpawnId{0};
        WorldPosition nodePos{};
        std::unordered_set<ObjectGuid::LowType> visited;
        uint32 lastReach{0};
        uint32 lastPassiveCheck{0};
        ObjectGuid::LowType lastSwitchedFrom{0};
    };
    // RPG_DO_PROFESSIONS
    struct DoProfessions
    {
        // A dependency or conversion cast; the final recipe is selected again once these are done.
        struct CraftStep
        {
            uint32 spellId{0};
            uint32 spellFocus{0};
            uint32 inputItemId{0};  // 0 for a normal create-item recipe
            uint32 inputCount{0};
            uint32 expectedItemId{0};  // conversion output or bought item
            ObjectGuid vendorGuid{};   // buy steps only
        };

        uint32 skillLine{0};
        std::vector<uint32> pendingSkillLines;
        bool started{false};
        uint16 startSkill{0};
        uint32 spellId{0};
        uint32 lastCast{0};
        uint32 lastCampfire{0};
        uint32 lastTrainer{0};
        uint16 lastTrainerSkill{0};
        ObjectGuid trainerGuid{};
        uint32 casts{0};
        std::vector<CraftStep> pendingCrafts;
        // Conversion loot is asynchronous; wait for the window to close before the next step.
        uint32 lootWaitStart{0};
        uint32 lootWaitSpell{0};
        uint32 lootWaitInput{0};
        uint32 lootWaitItem{0};
        uint32 lootWaitCount{0};
    };
    struct Idle
    {
    };

    uint32 startT{0};  // start timestamp of the current status

    // Failure memory kept outside the status variant so it survives an idle transition.
    uint32 failedSpell{0};
    uint32 failedInput{0};
    uint32 failedItem{0};
    uint32 failedAt{0};
    uint32 failedVendorItem{0};
    uint32 failedVendorAt{0};
    std::unordered_map<uint32, uint32> failedRecipes;  // spellId -> failure timestamp

    // MOVE_FAR
    float nearestMoveFarDis{FLT_MAX};
    uint32 stuckTs{0};
    uint32 stuckAttempts{0};
    WorldPosition moveFarPos;
    // END MOVE_FAR

    // QUEST GIVER
    ObjectGuid questGiver;  // giver the bot is walking to or talking with
    uint32 questGiverSince{0};
    uint32 questGiverLastTry{0};
    GuidSet ignoredQuestGivers;  // skipped until the next IDLE
    // END QUEST GIVER

    using RpgData = std::variant<
        Idle,
        GoGrind,
        GoCamp,
        WanderNpc,
        WanderRandom,
        DoQuest,
        Rest,
        TravelFlight,
        OutdoorPvP,
        DoGather,
        DoProfessions
    >;
    RpgData data;

    NewRpgStatus GetStatus();
    static NewRpgStatus StatusFromString(std::string const& name);
    bool HasStatusPersisted(uint32 maxDuration) { return GetMSTimeDiffToNow(startT) > maxDuration; }
    void ChangeToGoGrind(WorldPosition pos);
    void ChangeToGoCamp(WorldPosition pos);
    void ChangeToWanderNpc();
    void ChangeToWanderRandom();
    void ChangeToDoQuest(uint32 questId, Quest const* quest);
    void ChangeToTravelFlight(uint32 flightMasterEntry, WorldPosition flightMasterPos, std::vector<uint32> path);
    void ChangeToOutdoorPvp(ObjectGuid::LowType capturePointSpawnId = 0);
    void ChangeToDoGather();
    void ChangeToDoProfessions(uint32 skillLine);
    void ChangeToDoProfessions(uint32 skillLine, std::vector<uint32> pendingSkillLines);
    void ChangeToRest();
    void ChangeToIdle();
    bool CanChangeTo(NewRpgStatus status);
    void Reset();
    void SetMoveFarTo(WorldPosition pos);
    std::string ToString();
};

enum class QuestDropReason : uint8
{
    Trivial,
    NotWorth,
    NotCapable,
    Failed,
    OtherZone,
    ClassOrHoliday,
    LogFull,
    Count
};

enum class QuestAbandonReason : uint8
{
    NoProgress,
    CannotTurnIn,
    Count
};

enum class ProfessionEndReason : uint8
{
    NoRecipe,
    NoMaterials,
    BagFull,
    NoFocus,
    NoTarget,
    CraftFailed,
    SkillLost,
    Count
};

inline char const* ProfessionEndReasonName(ProfessionEndReason reason)
{
    switch (reason)
    {
        case ProfessionEndReason::NoRecipe:
            return "no_recipe";
        case ProfessionEndReason::NoMaterials:
            return "no_materials";
        case ProfessionEndReason::BagFull:
            return "bag_full";
        case ProfessionEndReason::NoFocus:
            return "no_focus";
        case ProfessionEndReason::NoTarget:
            return "no_target";
        case ProfessionEndReason::CraftFailed:
            return "craft_failed";
        case ProfessionEndReason::SkillLost:
            return "skill_lost";
        default:
            return "unknown";
    }
}

inline char const* QuestDropReasonName(QuestDropReason reason)
{
    switch (reason)
    {
        case QuestDropReason::Trivial:
            return "trivial";
        case QuestDropReason::NotWorth:
            return "not_worth";
        case QuestDropReason::NotCapable:
            return "not_capable";
        case QuestDropReason::Failed:
            return "failed";
        case QuestDropReason::OtherZone:
            return "other_zone";
        case QuestDropReason::ClassOrHoliday:
            return "class_or_holiday";
        case QuestDropReason::LogFull:
            return "log_full";
        default:
            return "unknown";
    }
}

inline char const* QuestAbandonReasonName(QuestAbandonReason reason)
{
    switch (reason)
    {
        case QuestAbandonReason::NoProgress:
            return "no_progress";
        case QuestAbandonReason::CannotTurnIn:
            return "cannot_turn_in";
        default:
            return "unknown";
    }
}

struct QuestCounters
{
    uint32 accepted{0};
    uint32 completed{0};
    uint32 rewarded{0};
    std::array<uint32, static_cast<size_t>(QuestDropReason::Count)> dropped{};
    std::array<uint32, static_cast<size_t>(QuestAbandonReason::Count)> abandoned{};
};

struct NewRpgStatistic
{
    uint32 questAccepted{0};
    uint32 questCompleted{0};
    uint32 questAbandoned{0};
    uint32 questRewarded{0};
    uint32 questDropped{0};
    // Filled only while CollectDetailedQuestStats is enabled.
    std::unordered_map<uint32, QuestCounters> byQuest;

    struct ProfessionCounters
    {
        uint32 sessions{0};
        uint32 casts{0};
        uint32 castFailures{0};
        uint32 skillGain{0};
        uint32 trainerVisits{0};
        uint32 trainerSpellsLearned{0};
        uint32 vendorPurchases{0};
        std::array<uint32, static_cast<size_t>(ProfessionEndReason::Count)> endReasons{};

        ProfessionCounters& operator+=(ProfessionCounters const& other)
        {
            sessions += other.sessions;
            casts += other.casts;
            castFailures += other.castFailures;
            skillGain += other.skillGain;
            trainerVisits += other.trainerVisits;
            trainerSpellsLearned += other.trainerSpellsLearned;
            vendorPurchases += other.vendorPurchases;
            for (size_t i = 0; i < endReasons.size(); ++i)
                endReasons[i] += other.endReasons[i];
            return *this;
        }
    };

    struct ProfessionRecipeCounters
    {
        uint32 casts{0};
        uint32 failures{0};
    };

    ProfessionCounters profession;
    // Filled only while CollectDetailedProfessionStats is enabled.
    std::unordered_map<uint32, ProfessionCounters> byProfession;
    std::unordered_map<uint32, ProfessionRecipeCounters> byRecipe;

    NewRpgStatistic& operator+=(NewRpgStatistic const& other)
    {
        questAccepted += other.questAccepted;
        questCompleted += other.questCompleted;
        questAbandoned += other.questAbandoned;
        questRewarded += other.questRewarded;
        questDropped += other.questDropped;

        for (auto const& [questId, counters] : other.byQuest)
        {
            QuestCounters& target = byQuest[questId];
            target.accepted += counters.accepted;
            target.completed += counters.completed;
            target.rewarded += counters.rewarded;
            for (size_t i = 0; i < target.dropped.size(); ++i)
                target.dropped[i] += counters.dropped[i];
            for (size_t i = 0; i < target.abandoned.size(); ++i)
                target.abandoned[i] += counters.abandoned[i];
        }

        profession += other.profession;
        for (auto const& [skillLine, counters] : other.byProfession)
            byProfession[skillLine] += counters;
        for (auto const& [spellId, counters] : other.byRecipe)
        {
            ProfessionRecipeCounters& target = byRecipe[spellId];
            target.casts += counters.casts;
            target.failures += counters.failures;
        }
        return *this;
    }
};

#endif
