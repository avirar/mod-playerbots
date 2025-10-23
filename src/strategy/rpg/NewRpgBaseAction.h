#ifndef _PLAYERBOT_NEWRPGBASEACTION_H
#define _PLAYERBOT_NEWRPGBASEACTION_H

#include "Duration.h"
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
    G3D::Vector2 pos;         // x, y coordinates
    int32 objectiveIdx;       // Objective index (16 for exploration)
    float z;                  // Optional Z coordinate (0.0f = not set, use ground level)
    bool useExactZ;           // If true, use the specified Z coordinate instead of recalculating from ground height
    float radius;             // For area triggers, the radius to enter (0.0f = not an area trigger)

    POIInfo() : objectiveIdx(0), z(0.0f), useExactZ(false), radius(0.0f) {}
};

/// A base (composition) class for all new rpg actions
/// All functions that may be shared by multiple actions should be declared here
/// And we should make all actions composable instead of inheritable
class NewRpgBaseAction : public MovementAction
{
public:
    NewRpgBaseAction(PlayerbotAI* botAI, std::string name) : MovementAction(botAI, name) {}

protected:
    /* MOVEMENT RELATED */
    bool MoveFarTo(WorldPosition dest);
    bool MoveWorldObjectTo(ObjectGuid guid, float distance = INTERACTION_DISTANCE);
    bool MoveRandomNear(float moveStep = 50.0f, MovementPriority priority = MovementPriority::MOVEMENT_NORMAL);
    bool ForceToWait(uint32 duration, MovementPriority priority = MovementPriority::MOVEMENT_NORMAL);

    /* QUEST RELATED CHECK */
    ObjectGuid ChooseNpcOrGameObjectToInteract(bool questgiverOnly = false, float distanceLimit = 0.0f);
    bool HasQuestToAcceptOrReward(WorldObject* object);
    bool InteractWithNpcOrGameObjectForQuest(ObjectGuid guid);
    bool CanInteractWithQuestGiver(Object* questGiver);
    bool IsWithinInteractionDist(Object* object);
    uint32 BestRewardIndex(Quest const* quest);
    bool IsQuestWorthDoing(Quest const* quest);
    bool IsQuestCapableDoing(Quest const* quest);
    bool IsRequiredQuestObjectiveNPC(Creature* creature);
    bool TryInteractWithQuestObjective(uint32 questId, int32 objectiveIdx);

    /* LOCK SYSTEM INTEGRATION */
    bool CheckGameObjectLockRequirements(GameObject* go, uint32& reqItem, uint32& skillId, uint32& reqSkillValue);
    bool CanAccessLockedGameObject(GameObject* go);
    bool HasRequiredKeyItem(uint32 itemId);
    bool HasQuestItemInDropTable(uint32 questId, uint32 itemId);

    /* QUEST RELATED ACTION */
    bool SearchQuestGiverAndAcceptOrReward();
    bool AcceptQuest(Quest const* quest, ObjectGuid guid);
    bool TurnInQuest(Quest const* quest, ObjectGuid guid);
    bool OrganizeQuestLog();

protected:
    bool GetQuestPOIPosAndObjectiveIdx(uint32 questId, std::vector<POIInfo>& poiInfo, bool toComplete = false);
    static WorldPosition SelectRandomGrindPos(Player* bot);
    static WorldPosition SelectRandomCampPos(Player* bot);
    bool SelectRandomFlightTaxiNode(ObjectGuid& flightMaster, uint32& fromNode, uint32& toNode);
    bool RandomChangeStatus(std::vector<NewRpgStatus> candidateStatus);
    bool CheckRpgStatusAvailable(NewRpgStatus status);
    bool SearchForActualQuestTargets(uint32 questId);
    bool GetRandomPointInPolygon(const std::vector<QuestPOIPoint>& points, float& outX, float& outY);
    bool IsWithinPOIBoundary(float x, float y, float tolerance = 40.0f);

protected:
    /* FOR MOVE FAR */
    const float pathFinderDis = 70.0f;
    const uint32 stuckTime = 5 * 60 * 1000;
};

#endif
