/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NEWRPGBASEACTION_H
#define PLAYERBOTS_NEWRPGBASEACTION_H

#include "LastMovementValue.h"
#include "MovementActions.h"
#include "NewRpgInfo.h"
#include "NewRpgStrategy.h"
#include "Object.h"
#include "ObjectDefines.h"
#include "ObjectGuid.h"
#include "PlayerbotAI.h"
#include "QuestDef.h"
#include "TravelMgr.h"

struct POIInfo
{
    G3D::Vector2 pos;
    int32 objectiveIdx;
    uint32 poiId{0}; // quest_poi.Id, the key for the baked Z store
};

// A resolved 3D objective/turn-in position from spawn data (quest guidp map).
struct QuestObjectivePosition
{
    int32 objectiveIdx; // 0..3 NpcOrGo, QUEST_OBJECTIVES_COUNT+ for item slots, -1 turn-in
    WorldPosition pos;
};

/// A base (composition) class for all new rpg actions
/// All functions that may be shared by multiple actions should be declared here
/// And we should make all actions composable instead of inheritable
class NewRpgBaseAction : public MovementAction
{
public:
    NewRpgBaseAction(PlayerbotAI* botAI, std::string name) : MovementAction(botAI, name) {}

    // Standalone so values and multipliers can reuse the same interaction search the actions use.
    static ObjectGuid FindQuestGiverToInteract(PlayerbotAI* botAI, Player* bot, bool questgiverOnly = false,
                                               float distanceLimit = 0.0f);

    // QuestGiverSearchDistance capped at the candidate caches' current radius (they only see loaded grids).
    static float GetQuestGiverSearchDistance(Player* bot);

protected:
    /* MOVEMENT RELATED */
    bool IsRpgTravelBlocked();
    bool MoveFarTo(WorldPosition dest);
    bool MoveWorldObjectTo(ObjectGuid guid, float distance = INTERACTION_DISTANCE);
    bool MoveRandomNear(float moveStep = 50.0f, MovementPriority priority = MovementPriority::MOVEMENT_NORMAL, WorldObject* center = nullptr);

    /* QUEST RELATED CHECK */
    ObjectGuid ChooseNpcOrGameObjectToInteract(bool questgiverOnly = false, float distanceLimit = 0.0f);
    static bool HasQuestToAcceptOrReward(Player* bot, WorldObject* object);
    bool InteractWithNpcOrGameObjectForQuest(ObjectGuid guid);
    static bool CanInteractWithQuestGiver(Player* bot, Object* questGiver);
    bool IsWithinInteractionDist(Object* object);
    uint32 BestRewardIndex(Quest const* quest);
    static bool IsQuestWorthDoing(Player* bot, Quest const* quest);
    static bool IsQuestCapableDoing(Player* bot, Quest const* quest);

    /* QUEST RELATED ACTION */
    bool SearchQuestGiverAndAcceptOrReward();
    static constexpr uint32 questGiverStayTime = 5 * 1000;
    bool AcceptQuest(Quest const* quest, ObjectGuid guid);
    bool TurnInQuest(Quest const* quest, ObjectGuid guid);
    bool OrganizeQuestLog();

    // Correct 3D positions for a quest's incomplete objectives (toComplete = turn-in) from the
    // shared quest guidp map (spawn Z): covers cave floors and tower platforms alike. Empty when
    // the map has no entry - callers fall back to POI + baked/terrain height.
    static bool GetQuestObjectivePositions(Player* bot, uint32 questId,
                                           std::vector<QuestObjectivePosition>& outPositions, bool toComplete);

    /* STUCK / UNUSABLE AREA RECOVERY */
    WorldPosition FindRecoveryAnchor();
    bool RecoverToRpgArea();

protected:
    bool GetQuestPOIPosAndObjectiveIdx(uint32 questId, std::vector<POIInfo>& poiInfo, bool toComplete = false,
                                       bool includeDistant = false);
    static WorldPosition SelectRandomGrindPos(Player* bot);
    static WorldPosition SelectRandomCampPos(Player* bot);
    bool SelectRandomFlightTaxiNode(uint32& flightMasterEntry, WorldPosition& flightMasterPos,
                                    std::vector<uint32>& path, bool questAware = true);
    bool RandomChangeStatus(std::vector<NewRpgStatus> candidateStatus);
    bool CheckRpgStatusAvailable(NewRpgStatus status);

protected:
    /* FOR MOVE FAR */
    const float pathFinderDis = 70.0f;
    // Time without real progress toward dest before MoveFarTo
    // falls back to teleport recovery. Kept short enough that a
    // bot truly oscillating around an unreachable destination
    // (mmap returning non-progressing partial paths, or NOPATH +
    // cone fallback wandering) doesn't spin for 5 minutes before
    // the teleport fires, but long enough that a genuine long
    // walk that is slowly making progress never triggers it.
    const uint32 stuckTime = 90 * IN_MILLISECONDS;

    /* FOR RECOVERY */
    // A same-zone anchor within this distance means a serviced area (empty level cache) where
    // recovering would only bounce the bot to its feet.
    const float recoverySameZoneDistance = 500.0f;
};

#endif
