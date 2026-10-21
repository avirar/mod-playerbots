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
    float z{0.0f};
    uint32 poiId{0}; // quest_poi.Id, the key for the baked Z store
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

    // Incomplete timed quest from the log, lowest slot first; 0 when none waits. Low-priority
    // entries are skipped, like the accept menu does.
    static uint32 FindIncompleteTimedQuest(PlayerbotAI* botAI);
    // The escort NPC for a quest: the SmartAI escort entry, the quest's source-spell summon
    // entry, quest starters and enders, accepting only NPCs that are actually walking (a
    // stationary giver/ender is not an escort).
    static Creature* FindEscorteeForQuest(Player* bot, uint32 questId, float range);
    // A scripted/event quest with no NPC/GO or item objectives: its C++ npc_escortAI escort is
    // not discoverable from the SmartAI index, so it is found by motion type near the bot.
    static bool HasScriptedEscortObjective(Quest const* quest);
    // Escort creature summoned at accept time: the core casts the quest's source spell on the
    // accepting player and a SPELL_EFFECT_SUMMON effect names the entry (e.g. Khadgar's Servant
    // for City of Light). 0 when the quest summons no known creature.
    static uint32 GetQuestSummonedEscortEntry(uint32 questId);

protected:
    // Throttle for quest-flow diagnostics (accept/pick decisions): one line per interval per action.
    bool QuestFlowDiagDue(uint32 interval = 10 * IN_MILLISECONDS);
    uint32 lastQuestFlowDiag{0};

    /* MOVEMENT RELATED */
    bool MoveFarTo(WorldPosition dest);
    bool MoveWorldObjectTo(ObjectGuid guid, float distance = INTERACTION_DISTANCE);
    bool MoveRandomNear(float moveStep = 50.0f, MovementPriority priority = MovementPriority::MOVEMENT_NORMAL, WorldObject* center = nullptr);
    bool IsMovementPending();
    bool ForceToWait(uint32 duration, MovementPriority priority = MovementPriority::MOVEMENT_NORMAL);

    /* QUEST RELATED CHECK */
    ObjectGuid ChooseNpcOrGameObjectToInteract(bool questgiverOnly = false, float distanceLimit = 0.0f);
    static bool HasQuestToAcceptOrReward(Player* bot, WorldObject* object);
    bool InteractWithNpcOrGameObjectForQuest(ObjectGuid guid);
    // Acceptance must match what the progress system can navigate on this map; otherwise the
    // accept becomes instant log_pressure fodder or a permanently stuck log entry.
    bool IsQuestAcceptableHere(uint32 questId);
    static bool CanInteractWithQuestGiver(Player* bot, Object* questGiver);
    bool IsWithinInteractionDist(Object* object);
    uint32 BestRewardIndex(Quest const* quest);
    static bool IsQuestWorthDoing(Player* bot, Quest const* quest);
    static bool IsQuestCapableDoing(Player* bot, Quest const* quest);

    /* QUEST RELATED ACTION */
    bool SearchQuestGiverAndAcceptOrReward();
    bool AcceptQuest(Quest const* quest, ObjectGuid guid);
    bool TurnInQuest(Quest const* quest, ObjectGuid guid);
    bool OrganizeQuestLog();

    /* STUCK / UNUSABLE AREA RECOVERY */
    WorldPosition FindRecoveryAnchor();
    bool RecoverToRpgArea();

protected:
    bool GetQuestPOIPosAndObjectiveIdx(uint32 questId, std::vector<POIInfo>& poiInfo, bool toComplete = false);
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
    // A same-zone anchor closer than this means the bot is already in a serviced area
    // (city, quest hub) whose level cache has no entries: recovering would only bounce it
    // to a point at its feet.
    const float recoverySameZoneDistance = 500.0f;
};

#endif
