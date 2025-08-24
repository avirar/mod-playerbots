#include "NewRpgAction.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "BroadcastHelper.h"
#include "ChatHelper.h"
#include "DBCStores.h"
#include "G3D/Vector2.h"
#include "GossipDef.h"
#include "IVMapMgr.h"
#include "NewRpgInfo.h"
#include "NewRpgStrategy.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Position.h"
#include "QuestDef.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"
#include "StatsWeightCalculator.h"
#include "Timer.h"
#include "TravelMgr.h"
#include "World.h"

bool TellRpgStatusAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;
    std::string out = botAI->rpgInfo.ToString();
    bot->Whisper(out.c_str(), LANG_UNIVERSAL, owner);
    return true;
}

bool StartRpgDoQuestAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    std::string const text = event.getParam();
    PlayerbotChatHandler ch(owner);
    uint32 questId = ch.extractQuestId(text);
    const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
    if (quest)
    {
        botAI->rpgInfo.ChangeToDoQuest(questId, quest);
        bot->Whisper("Start to do quest " + std::to_string(questId), LANG_UNIVERSAL, owner);
        return true;
    }
    bot->Whisper("Invalid quest " + text, LANG_UNIVERSAL, owner);
    return false;
}

bool NewRpgStatusUpdateAction::Execute(Event event)
{
    NewRpgInfo& info = botAI->rpgInfo;
    switch (info.status)
    {
        case RPG_IDLE:
        {
            // PRIORITY: Go near Vendor NPC if bags are almost full to prevent looting issues
            if (AI_VALUE(uint8, "bag space") > 80)
            {
                GuidVector possibleTargets = AI_VALUE(GuidVector, "possible new rpg targets");
                if (!possibleTargets.empty())
                {
                    for (ObjectGuid& guid : possibleTargets)
                    {
                        Creature* creature = ObjectAccessor::GetCreature(*bot, guid);

                        if (!creature || !creature->IsInWorld())
                            continue;

                        if (creature->IsVendor())
                        {
                            info.ChangeToWanderNpc();
                            return true;
                        }
                    }
                }
            }
            return RandomChangeStatus({RPG_GO_CAMP, RPG_GO_GRIND, RPG_WANDER_RANDOM, RPG_WANDER_NPC, RPG_DO_QUEST,
                                       RPG_TRAVEL_FLIGHT, RPG_REST});
        }
        case RPG_GO_GRIND:
        {
            WorldPosition& originalPos = info.go_grind.pos;
            assert(info.go_grind.pos != WorldPosition());
            // GO_GRIND -> WANDER_RANDOM
            if (bot->GetExactDist(originalPos) < 10.0f)
            {
                info.ChangeToWanderRandom();
                return true;
            }
            break;
        }
        case RPG_GO_CAMP:
        {
            WorldPosition& originalPos = info.go_camp.pos;
            assert(info.go_camp.pos != WorldPosition());
            // GO_CAMP -> WANDER_NPC
            if (bot->GetExactDist(originalPos) < 10.0f)
            {
                info.ChangeToWanderNpc();
                return true;
            }
            break;
        }
        case RPG_WANDER_RANDOM:
        {
            // WANDER_RANDOM -> IDLE
            if (info.HasStatusPersisted(statusWanderRandomDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_WANDER_NPC:
        {
            if (info.HasStatusPersisted(statusWanderNpcDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_DO_QUEST:
        {
            // DO_QUEST -> IDLE
            if (info.HasStatusPersisted(statusDoQuestDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_TRAVEL_FLIGHT:
        {
            if (info.flight.inFlight && !bot->IsInFlight())
            {
                // flight arrival
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_REST:
        {
            // REST -> IDLE
            if (info.HasStatusPersisted(statusRestDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        default:
            break;
    }
    return false;
}

bool NewRpgGoGrindAction::Execute(Event event)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    return MoveFarTo(botAI->rpgInfo.go_grind.pos);
}

bool NewRpgGoCampAction::Execute(Event event)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    return MoveFarTo(botAI->rpgInfo.go_camp.pos);
}

bool NewRpgWanderRandomAction::Execute(Event event)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    // While wandering, also look for any active quest objectives using non-LOS search
    std::map<uint32, Quest const*> activeQuests;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;
            
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (quest && bot->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
        {
            activeQuests[questId] = quest;
        }
    }
    
    if (!activeQuests.empty())
    {
        // Search for objectives of any active quest using reliable core values
        GuidVector possibleTargets = AI_VALUE(GuidVector, "all targets");  // All hostiles
        GuidVector allNpcs = AI_VALUE(GuidVector, "nearest npcs");         // All NPCs
        possibleTargets.insert(possibleTargets.end(), allNpcs.begin(), allNpcs.end());
        
        for (ObjectGuid& guid : possibleTargets)
        {
            Unit* unit = ObjectAccessor::GetUnit(*bot, guid);
            if (!unit || !unit->IsInWorld())
                continue;
                
            float distance = bot->GetDistance(unit);
            if (distance > 100.0f) // Wider search during wandering
                continue;
                
            if (unit->GetTypeId() == TYPEID_UNIT)
            {
                Creature* creature = unit->ToCreature();
                if (!creature)
                    continue;
                    
                uint32 creatureEntry = creature->GetEntry();
                
                // Check if this creature is needed for any active quest
                for (auto& [questId, quest] : activeQuests)
                {
                    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                    {
                        int32 requiredNpcOrGo = quest->RequiredNpcOrGo[i];
                        if (requiredNpcOrGo > 0 && requiredNpcOrGo == (int32)creatureEntry)
                        {
                            // Check if we still need this objective
                            const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
                            if (q_status.CreatureOrGOCount[i] < quest->RequiredNpcOrGoCount[i])
                            {
                                LOG_DEBUG("playerbots", "[New RPG] {} Found quest objective {} (entry {}) while wandering for quest {}", 
                                         bot->GetName(), creature->GetName(), creatureEntry, questId);
                                
                                return MoveWorldObjectTo(guid, 25.0f);
                            }
                        }
                    }
                }
            }
        }
    }

    return MoveRandomNear();
}

bool NewRpgWanderNpcAction::Execute(Event event)
{
    NewRpgInfo& info = botAI->rpgInfo;

    if (!info.wander_npc.npcOrGo)
    {
        // No npc can be found, switch to IDLE
        ObjectGuid npcOrGo = ChooseNpcOrGameObjectToInteract();
        if (npcOrGo.IsEmpty())
        {
            info.ChangeToIdle();
            return true;
        }
        info.wander_npc.npcOrGo = npcOrGo;
        info.wander_npc.lastReach = 0;
        return true;
    }

    WorldObject* object = ObjectAccessor::GetWorldObject(*bot, info.wander_npc.npcOrGo);

    // --- Step 1: Ensure bot is close enough to interact ---
    if (!object || bot->GetDistance(object) > INTERACTION_DISTANCE)
    {
        return MoveWorldObjectTo(info.wander_npc.npcOrGo);
    }

    bool interacted = false;  // Track if the bot has interacted with the NPC

    // --- Step 2: Handle Quest NPCs ---
    if (bot->CanInteractWithQuestGiver(object))
    {
        InteractWithNpcOrGameObjectForQuest(info.wander_npc.npcOrGo);
        interacted = true;
    }

    // --- Step 3: Detect NPC and Retrieve Details ---
    Creature* creature = bot->GetNPCIfCanInteractWith(info.wander_npc.npcOrGo, UNIT_NPC_FLAG_NONE);

    if (!creature)
    {
        return true;
    }

    std::string npcName = creature->GetName();
    uint32 npcFlags = creature->GetCreatureTemplate()->npcflag;

    // --- Step 4: Handle Trainers, limited to class trainers for the moment ---
    if (creature->IsValidTrainerForPlayer(bot) && creature->GetCreatureTemplate()->trainer_type == TRAINER_TYPE_CLASS)
    {
        bot->SetSelection(info.wander_npc.npcOrGo);
        botAI->DoSpecificAction("trainer", Event("trainer"));
        interacted = true;
    }

    // --- Step 5: Handle Vendors ---
    if (npcFlags & UNIT_NPC_FLAG_VENDOR_MASK)
    {
        botAI->DoSpecificAction("sell", Event("sell", "*"));
        botAI->DoSpecificAction("buy", Event("buy", "vendor"));
        interacted = true;
    }

    // --- Step 6: Handle Repair Vendors ---
    if (npcFlags & UNIT_NPC_FLAG_REPAIR)
    {
        bot->SetSelection(info.wander_npc.npcOrGo);
        botAI->DoSpecificAction("repair", Event("repair"));
        interacted = true;
    }

    // --- Step 7: Apply Waiting Logic (EVEN IF NO INTERACTION) ---
    if (!info.wander_npc.lastReach)
    {
        info.wander_npc.lastReach = getMSTime();  // Set once for ALL cases
        return false;
    }
    else if (GetMSTimeDiffToNow(info.wander_npc.lastReach) < npcStayTime)
    {
        return false;
    }

    // --- Step 8: Reset & Move to Next Target ---
    info.wander_npc.npcOrGo = ObjectGuid();
    info.wander_npc.lastReach = 0;
    info.recentNpcVisits[creature->GetGUID()] = getMSTime();

    return true;
}

bool NewRpgDoQuestAction::Execute(Event event)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    NewRpgInfo& info = botAI->rpgInfo;
    uint32 questId = RPG_INFO(quest, questId);
    const Quest* quest = RPG_INFO(quest, quest);
    uint8 questStatus = bot->GetQuestStatus(questId);
    switch (questStatus)
    {
        case QUEST_STATUS_INCOMPLETE:
            return DoIncompleteQuest();
        case QUEST_STATUS_COMPLETE:
            return DoCompletedQuest();
        default:
            break;
    }
    botAI->rpgInfo.ChangeToIdle();
    return true;
}

bool NewRpgDoQuestAction::DoIncompleteQuest()
{
    uint32 questId = RPG_INFO(do_quest, questId);
    LOG_DEBUG("playerbots", "[New RPG] DoIncompleteQuest started for quest {}", questId);

    if (botAI->rpgInfo.do_quest.pos != WorldPosition())
    {
        /// @TODO: extract to a new function
        int32 currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;
        botAI->TellMasterNoFacing("Checking objective completion for quest " + std::to_string(questId) + ", objective index: " + std::to_string(currentObjective));

        // check if the objective has completed
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
        bool completed = true;

        if (currentObjective < QUEST_OBJECTIVES_COUNT)
        {
            if (q_status.CreatureOrGOCount[currentObjective] < quest->RequiredNpcOrGoCount[currentObjective])
                completed = false;
        }
        else if (currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
        {
            if (q_status.ItemCount[currentObjective - QUEST_OBJECTIVES_COUNT] <
                quest->RequiredItemCount[currentObjective - QUEST_OBJECTIVES_COUNT])
                completed = false;
        }

        // the current objective is completed, clear and find a new objective later
        if (completed)
        {
            botAI->TellMasterNoFacing("Objective completed, clearing quest state for quest " + std::to_string(questId));
            botAI->rpgInfo.do_quest.lastReachPOI = 0;
            botAI->rpgInfo.do_quest.pos = WorldPosition();
            botAI->rpgInfo.do_quest.objectiveIdx = 0;
        }
    }

    if (botAI->rpgInfo.do_quest.pos == WorldPosition())
    {
        botAI->TellMasterNoFacing("No valid POI position found, searching for quest POIs for quest " + std::to_string(questId));

        std::vector<POIInfo> poiInfo;
        if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo))
        {
            // can't find a poi pos to go, stop doing quest for now
            botAI->TellMasterNoFacing("Failed to get POI positions for quest " + std::to_string(questId));
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }

        uint32 rndIdx = urand(0, poiInfo.size() - 1);
        G3D::Vector2 nearestPoi = poiInfo[rndIdx].pos;
        int32 objectiveIdx = poiInfo[rndIdx].objectiveIdx;

        float dx = nearestPoi.x, dy = nearestPoi.y;
        float dz = bot->GetMap()->GetGridHeight(dx, dy);
        
        // Look for nearby quest-related NPCs to get proper Z reference
        ObjectGuid nearbyNPC = FindNearbyQuestNPC(questId, dx, dy, 100.0f);
        float floorZ = GetProperFloorHeightNearNPC(bot, dx, dy, dz, nearbyNPC);

        if (floorZ != INVALID_HEIGHT && floorZ != VMAP_INVALID_HEIGHT_VALUE)
        {
            dz = floorZ;
        }

        // double check for GetQuestPOIPosAndObjectiveIdx
        if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
        {
            botAI->TellMasterNoFacing("Invalid height detected for quest " + std::to_string(questId));
            return false;
        }

        WorldPosition pos(bot->GetMapId(), dx, dy, dz);
        botAI->TellMasterNoFacing("Setting new POI position for quest " + std::to_string(questId) + " at (" + std::to_string(dx) + ", " + std::to_string(dy) + ", " + std::to_string(dz) + ")");
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
        botAI->rpgInfo.do_quest.pos = pos;
        botAI->rpgInfo.do_quest.objectiveIdx = objectiveIdx;
    }

    if (bot->GetDistance(botAI->rpgInfo.do_quest.pos) > 10.0f && !botAI->rpgInfo.do_quest.lastReachPOI)
    {
        botAI->TellMasterNoFacing("Moving far to quest POI for quest " + std::to_string(questId));
        return MoveFarTo(botAI->rpgInfo.do_quest.pos);
    }

    // Now we are near the quest objective
    // kill mobs and looting quest should be done automatically by grind strategy

    if (!botAI->rpgInfo.do_quest.lastReachPOI)
    {
        botAI->TellMasterNoFacing("Arrived at quest POI for quest " + std::to_string(questId));
        botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();
        return true;
    }

    // stayed at this POI for more than 5 minutes
    if (GetMSTimeDiffToNow(botAI->rpgInfo.do_quest.lastReachPOI) >= poiStayTime)
    {
        botAI->TellMasterNoFacing("Staying at POI for too long, checking for progress on quest " + std::to_string(questId));

        bool hasProgression = false;
        int32 currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;

        // check if the objective has progression
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);

        if (currentObjective < QUEST_OBJECTIVES_COUNT)
        {
            if (q_status.CreatureOrGOCount[currentObjective] != 0 && quest->RequiredNpcOrGoCount[currentObjective])
                hasProgression = true;
        }
        else if (currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
        {
            if (q_status.ItemCount[currentObjective - QUEST_OBJECTIVES_COUNT] != 0 &&
                quest->RequiredItemCount[currentObjective - QUEST_OBJECTIVES_COUNT])
                hasProgression = true;
        }

        if (!hasProgression)
        {
            // we has reach the poi for more than 5 mins but no progession
            // may not be able to complete this quest, marked as abandoned
            botAI->TellMasterNoFacing("No progression detected, marking quest " + std::to_string(questId) + " as abandoned");
            /// @TODO: It may be better to make lowPriorityQuest a global set shared by all bots (or saved in db)
            botAI->lowPriorityQuest.insert(questId);
            botAI->rpgStatistic.questAbandoned++;
            LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }

        // clear and select another poi later
        botAI->TellMasterNoFacing("Clearing POI state for quest " + std::to_string(questId));
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
        botAI->rpgInfo.do_quest.pos = WorldPosition();
        botAI->rpgInfo.do_quest.objectiveIdx = 0;
        return true;
    }

    // Before moving randomly, search for quest targets/objectives using non-LOS search
    LOG_DEBUG("playerbots", "[New RPG] {} DoIncompleteQuest: reached quest objective search phase for quest {}", 
             bot->GetName(), questId);
             
    if (SearchQuestGiverAndAcceptOrReward())
    {
        LOG_DEBUG("playerbots", "[New RPG] {} Found quest giver, returning", bot->GetName());
        return true;
    }
        
    // Get quest template to check specific required NPCs/GameObjects
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} No quest template found for quest {}", bot->GetName(), questId);
        return MoveRandomNear(50.0f);
    }
    
    LOG_DEBUG("playerbots", "[New RPG] {} Quest {} template found, proceeding with objective search", 
             bot->GetName(), questId);
        
    // Search for quest objectives using core values that work reliably (like LOS command)
    GuidVector possibleTargets = AI_VALUE(GuidVector, "all targets");  // All hostiles without LOS restrictions
    GuidVector allNpcs = AI_VALUE(GuidVector, "nearest npcs");         // All NPCs without LOS restrictions  
    GuidVector possibleGameObjects = AI_VALUE(GuidVector, "nearest game objects");
    
    // Combine hostile targets with all NPCs for comprehensive search
    possibleTargets.insert(possibleTargets.end(), allNpcs.begin(), allNpcs.end());
    
    LOG_DEBUG("playerbots", "[New RPG] {} Searching for quest {} objectives - found {} targets, {} gameobjects", 
             bot->GetName(), questId, possibleTargets.size(), possibleGameObjects.size());
    
    // Look for required NPCs first
    for (ObjectGuid& guid : possibleTargets)
    {
        Unit* unit = ObjectAccessor::GetUnit(*bot, guid);
        if (!unit || !unit->IsInWorld())
            continue;
            
        float distance = bot->GetDistance(unit);
        LOG_DEBUG("playerbots", "[New RPG] {} Checking unit {} (entry {}) at distance {}", 
                 bot->GetName(), unit->GetName(), unit->GetEntry(), distance);
                 
        if (distance > 80.0f) // Reasonable search radius at POI
        {
            LOG_DEBUG("playerbots", "[New RPG] {} Unit {} too far ({}), skipping", 
                     bot->GetName(), unit->GetName(), distance);
            continue;
        }
            
        if (unit->GetTypeId() == TYPEID_UNIT)
        {
            Creature* creature = unit->ToCreature();
            if (!creature)
                continue;
                
            uint32 creatureEntry = creature->GetEntry();
            
            // Check if this creature is required for the quest
            for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            {
                int32 requiredNpcOrGo = quest->RequiredNpcOrGo[i];
                if (requiredNpcOrGo > 0)
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Quest {} objective {} requires entry {}, checking against {}", 
                             bot->GetName(), questId, i, requiredNpcOrGo, creatureEntry);
                             
                    if (requiredNpcOrGo == (int32)creatureEntry)
                    {
                        // Check if we still need this objective
                        const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
                        uint32 currentCount = q_status.CreatureOrGOCount[i];
                        uint32 requiredCount = quest->RequiredNpcOrGoCount[i];
                        
                        LOG_DEBUG("playerbots", "[New RPG] {} Quest {} objective {} progress: {}/{}", 
                                 bot->GetName(), questId, i, currentCount, requiredCount);
                        
                        if (currentCount < requiredCount)
                        {
                            LOG_DEBUG("playerbots", "[New RPG] {} Found required quest NPC {} (entry {}) at distance {}", 
                                     bot->GetName(), creature->GetName(), creatureEntry, distance);
                            
                            // Move towards the required NPC - grind strategy will handle combat
                            return MoveWorldObjectTo(guid, 25.0f);
                        }
                        else
                        {
                            LOG_DEBUG("playerbots", "[New RPG] {} Quest objective {} already completed ({}/{})", 
                                     bot->GetName(), i, currentCount, requiredCount);
                        }
                    }
                }
            }
        }
    }
    
    // Look for required GameObjects
    for (ObjectGuid& guid : possibleGameObjects)
    {
        GameObject* go = ObjectAccessor::GetGameObject(*bot, guid);
        if (!go || !go->IsInWorld())
            continue;
            
        float distance = bot->GetDistance(go);
        if (distance > 80.0f)
            continue;
            
        uint32 goEntry = go->GetEntry();
        
        // Check if this GameObject is required for the quest
        for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 requiredNpcOrGo = quest->RequiredNpcOrGo[i];
            if (requiredNpcOrGo < 0 && (-requiredNpcOrGo) == (int32)goEntry)
            {
                // Check if we still need this objective  
                const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
                if (q_status.CreatureOrGOCount[i] < quest->RequiredNpcOrGoCount[i])
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Found required quest GameObject {} (entry {}) at distance {}", 
                             bot->GetName(), go->GetGOInfo()->name, goEntry, distance);
                    
                    // If close enough, interact with the GameObject
                    if (distance <= INTERACTION_DISTANCE)
                    {
                        LOG_DEBUG("playerbots", "[New RPG] {} Close enough to GameObject, attempting interaction", 
                                 bot->GetName());
                        InteractWithNpcOrGameObjectForQuest(guid);
                        return true;
                    }
                    
                    return MoveWorldObjectTo(guid, INTERACTION_DISTANCE);
                }
            }
        }
    }

    // Only move random if no quest targets found
    botAI->TellMasterNoFacing("Moving randomly near quest POI for quest " + std::to_string(questId));
    return MoveRandomNear(50.0f);
}

bool NewRpgDoQuestAction::DoCompletedQuest()
{
    uint32 questId = RPG_INFO(quest, questId);
    const Quest* quest = RPG_INFO(quest, quest);

    if (RPG_INFO(quest, objectiveIdx) != -1)
    {
        // if quest is completed, back to poi with -1 idx to reward
        BroadcastHelper::BroadcastQuestUpdateComplete(botAI, bot, quest);
        botAI->rpgStatistic.questCompleted++;
        std::vector<POIInfo> poiInfo;
        if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo, true))
        {
            // can't find a poi pos to reward, stop doing quest for now
            botAI->rpgInfo.ChangeToIdle();
            return false;
        }
        assert(poiInfo.size() > 0);
        // now we get the place to get rewarded
        float dx = poiInfo[0].pos.x, dy = poiInfo[0].pos.y;
        float dz = bot->GetMap()->GetGridHeight(dx, dy);
        
        // Look for nearby quest giver for reward to get proper Z reference
        ObjectGuid nearbyNPC = FindNearbyQuestNPC(questId, dx, dy, 100.0f);
        float floorZ = GetProperFloorHeightNearNPC(bot, dx, dy, dz, nearbyNPC);
        if (floorZ != INVALID_HEIGHT && floorZ != VMAP_INVALID_HEIGHT_VALUE)
        {
            dz = floorZ;
        }

        // double check for GetQuestPOIPosAndObjectiveIdx
        if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
            return false;

        WorldPosition pos(bot->GetMapId(), dx, dy, dz);
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
        botAI->rpgInfo.do_quest.pos = pos;
        botAI->rpgInfo.do_quest.objectiveIdx = -1;
    }

    if (botAI->rpgInfo.do_quest.pos == WorldPosition())
        return false;

    if (bot->GetDistance(botAI->rpgInfo.do_quest.pos) > 10.0f && !botAI->rpgInfo.do_quest.lastReachPOI)
        return MoveFarTo(botAI->rpgInfo.do_quest.pos);

    // Now we are near the qoi of reward
    // the quest should be rewarded by SearchQuestGiverAndAcceptOrReward
    if (!botAI->rpgInfo.do_quest.lastReachPOI)
    {
        botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();
        return true;
    }
    // stayed at this POI for more than 5 minutes
    if (GetMSTimeDiffToNow(botAI->rpgInfo.do_quest.lastReachPOI) >= poiStayTime)
    {
        // e.g. Can not reward quest to gameobjects
        /// @TODO: It may be better to make lowPriorityQuest a global set shared by all bots (or saved in db)
        botAI->lowPriorityQuest.insert(questId);
        botAI->rpgStatistic.questAbandoned++;
        LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }
    return false;
}

bool NewRpgTravelFlightAction::Execute(Event event)
{
    if (bot->IsInFlight())
    {
        botAI->rpgInfo.flight.inFlight = true;
        return false;
    }
    Creature* flightMaster = ObjectAccessor::GetCreature(*bot, botAI->rpgInfo.flight.fromFlightMaster);
    if (!flightMaster || !flightMaster->IsAlive())
    {
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }
    const TaxiNodesEntry* entry = sTaxiNodesStore.LookupEntry(botAI->rpgInfo.flight.toNode);
    if (bot->GetDistance(flightMaster) > INTERACTION_DISTANCE)
    {
        return MoveFarTo(flightMaster);
    }
    std::vector<uint32> nodes = {botAI->rpgInfo.flight.fromNode, botAI->rpgInfo.flight.toNode};

    botAI->RemoveShapeshift();
    if (bot->IsMounted())
    {
        bot->Dismount();
    }
    if (!bot->ActivateTaxiPathTo(nodes, flightMaster, 0))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} active taxi path {} (from {} to {}) failed", bot->GetName(),
                  flightMaster->GetEntry(), nodes[0], nodes[1]);
        botAI->rpgInfo.ChangeToIdle();
    }
    return true;
}
