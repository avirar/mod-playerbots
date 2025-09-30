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
#include "PossibleRpgTargetsValue.h"

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
            // PRIORITY: Find vendor when bags are almost full to prevent looting issues
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
                // Fallback: Go to camp if no nearby vendor found
                WorldPosition campPos = SelectRandomCampPos(bot);
                if (campPos != WorldPosition())
                {
                    info.ChangeToGoCamp(campPos);
                    return true;
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
                                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                                {
                                    LOG_DEBUG("playerbots", "[New RPG] {} Found quest objective {} (entry {}) while wandering for quest {}", 
                                             bot->GetName(), creature->GetName(), creatureEntry, questId);
                                }
                                
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

    // --- Step 1: Validate the target before moving ---
    if (!object || !object->IsInWorld())
    {
        // Target no longer exists, find a new one
        info.wander_npc.npcOrGo = ObjectGuid();
        info.wander_npc.lastReach = 0;
        return true;
    }

    // --- Step 2: Pre-validate trainers to avoid wasting time moving to unusable ones ---
    Creature* creature = object->ToCreature();
    if (creature && creature->IsValidTrainerForPlayer(bot))
    {
        uint32 trainerType = creature->GetCreatureTemplate()->trainer_type;
        std::string trainerTypeName = "UNKNOWN";
        switch (trainerType)
        {
            case TRAINER_TYPE_CLASS: trainerTypeName = "CLASS"; break;
            case TRAINER_TYPE_MOUNTS: trainerTypeName = "MOUNTS/RIDING"; break;
            case TRAINER_TYPE_PETS: trainerTypeName = "PETS"; break;
            case TRAINER_TYPE_TRADESKILLS: trainerTypeName = "TRADESKILLS"; break;
        }
        
        // For profession trainers, check if we should skip them entirely
        if (trainerType == TRAINER_TYPE_TRADESKILLS)
        {
            static TrainerClassifier classifier;
            if (!classifier.IsValidSecondaryTrainer(bot, creature))
            {
                // Mark this NPC as recently visited to avoid re-selecting it immediately
                info.recentNpcVisits[creature->GetGUID()] = getMSTime();
                
                // Reset and find a new target
                info.wander_npc.npcOrGo = ObjectGuid();
                info.wander_npc.lastReach = 0;
                return true;
            }
        }
        
        // Check if this trainer has any GREEN spells available
        const TrainerSpellData* trainerSpells = creature->GetTrainerSpells();
        if (trainerSpells)
        {
            bool hasGreenSpells = false;
            for (const auto& [_, tSpell] : trainerSpells->spellList)
            {
                if (bot->GetTrainerSpellState(&tSpell) == TRAINER_SPELL_GREEN)
                {
                    hasGreenSpells = true;
                    break;
                }
            }
            
            if (!hasGreenSpells)
            {
                
                // Mark this NPC as recently visited to avoid re-selecting it
                info.recentNpcVisits[creature->GetGUID()] = getMSTime();
                
                // Reset and find a new target
                info.wander_npc.npcOrGo = ObjectGuid();
                info.wander_npc.lastReach = 0;
                return true;
            }
        }
    }

    // --- Step 3: Ensure bot is close enough to interact ---
    if (bot->GetDistance(object) > INTERACTION_DISTANCE)
    {
        return MoveWorldObjectTo(info.wander_npc.npcOrGo);
    }

    bool interacted = false;  // Track if the bot has interacted with the NPC

    // --- Step 4: Handle Quest NPCs ---
    if (bot->CanInteractWithQuestGiver(object))
    {
        InteractWithNpcOrGameObjectForQuest(info.wander_npc.npcOrGo);
        interacted = true;
    }

    // --- Step 5: Handle NPCs (re-get creature since we validated earlier) ---
    creature = bot->GetNPCIfCanInteractWith(info.wander_npc.npcOrGo, UNIT_NPC_FLAG_NONE);

    if (!creature)
    {
        // Not a valid NPC for interaction, move to next target
        info.wander_npc.npcOrGo = ObjectGuid();
        info.wander_npc.lastReach = 0;
        return true;
    }

    std::string npcName = creature->GetName();
    uint32 npcFlags = creature->GetCreatureTemplate()->npcflag;

    // --- Step 6: Handle Trainers (we already pre-validated above) ---
    if (creature->IsValidTrainerForPlayer(bot))
    {
        bool shouldInteract = false;
        
        // Always interact with class trainers
        if (creature->GetCreatureTemplate()->trainer_type == TRAINER_TYPE_CLASS)
        {
            shouldInteract = true;
            if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} - Interacting with class trainer: {}", 
                          bot->GetName(), creature->GetName());
            }
        }
        // Always interact with mount trainers (riding trainers)
        else if (creature->GetCreatureTemplate()->trainer_type == TRAINER_TYPE_MOUNTS)
        {
            shouldInteract = true;
            if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} - Interacting with riding trainer: {}", 
                          bot->GetName(), creature->GetName());
            }
        }
        // Always interact with pet trainers (for hunters)
        else if (creature->GetCreatureTemplate()->trainer_type == TRAINER_TYPE_PETS)
        {
            shouldInteract = true;
            if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} - Interacting with pet trainer: {}", 
                          bot->GetName(), creature->GetName());
            }
        }
        // For profession trainers, we already validated them in Step 2
        else if (creature->GetCreatureTemplate()->trainer_type == TRAINER_TYPE_TRADESKILLS)
        {
            shouldInteract = true; // We already validated this is a secondary trainer
            if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} - Interacting with pre-validated secondary trainer: {}", 
                          bot->GetName(), creature->GetName());
            }
        }
        
        if (shouldInteract)
        {
            bot->SetSelection(info.wander_npc.npcOrGo);
            botAI->DoSpecificAction("trainer", Event("trainer"));
            interacted = true;
        }
    }

    // --- Step 7: Handle Vendors ---
    if (npcFlags & UNIT_NPC_FLAG_VENDOR_MASK)
    {
        botAI->DoSpecificAction("sell", Event("sell", "vendor"));
        botAI->DoSpecificAction("buy", Event("buy", "vendor"));
        interacted = true;
    }

    // --- Step 8: Handle Repair Vendors ---
    if (npcFlags & UNIT_NPC_FLAG_REPAIR)
    {
        bot->SetSelection(info.wander_npc.npcOrGo);
        botAI->DoSpecificAction("repair", Event("repair"));
        interacted = true;
    }

    // --- Step 9: Apply Waiting Logic ---
    // If we haven't interacted yet, record the time and stay
    if (!info.wander_npc.lastReach)
    {
        if (interacted)
        {
            // We just interacted, start the waiting timer
            info.wander_npc.lastReach = getMSTime();
        }
        return false; // Stay regardless, either to interact or to wait after interaction
    }
    // If we're in waiting period after interaction
    else if (GetMSTimeDiffToNow(info.wander_npc.lastReach) < npcStayTime)
    {
        return false; // Continue waiting
    }

    // --- Step 10: Reset & Move to Next Target ---
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
    
    // Keep upstream objective completion checking logic
    if (botAI->rpgInfo.do_quest.pos != WorldPosition())
    {
        int32 currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;

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
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Objective completed, clearing quest state for quest {}", 
                          bot->GetName(), questId);
            }
            botAI->rpgInfo.do_quest.lastReachPOI = 0;
            botAI->rpgInfo.do_quest.pos = WorldPosition();
            botAI->rpgInfo.do_quest.objectiveIdx = 0;
        }
    }

    if (botAI->rpgInfo.do_quest.pos == WorldPosition())
    {
        // STEP 1: Use clean upstream POI system first
        std::vector<POIInfo> poiInfo;
        if (GetQuestPOIPosAndObjectiveIdx(questId, poiInfo))
        {
            uint32 rndIdx = urand(0, poiInfo.size() - 1);
            G3D::Vector2 nearestPoi = poiInfo[rndIdx].pos;
            int32 objectiveIdx = poiInfo[rndIdx].objectiveIdx;

            float dx = nearestPoi.x, dy = nearestPoi.y;
            
            // Use upstream's clean approach - no fancy Z calculations
            float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), 
                               bot->GetMap()->GetWaterLevel(dx, dy));

            if (dz != INVALID_HEIGHT && dz != VMAP_INVALID_HEIGHT_VALUE)
            {
                WorldPosition pos(bot->GetMapId(), dx, dy, dz);
                botAI->rpgInfo.do_quest.lastReachPOI = 0;
                botAI->rpgInfo.do_quest.pos = pos;
                botAI->rpgInfo.do_quest.objectiveIdx = objectiveIdx;
                
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Set POI position for quest {} at ({}, {}, {})", 
                              bot->GetName(), questId, dx, dy, dz);
                }
            }
        }
        
        // STEP 2: POI system failed - try smart fallback using server's quest system
        if (botAI->rpgInfo.do_quest.pos == WorldPosition() && SearchForActualQuestTargets(questId))
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} POI failed, found actual quest target for quest {}", 
                          bot->GetName(), questId);
            }
        }
        
        // STEP 3: Still no position - give up on this quest
        if (botAI->rpgInfo.do_quest.pos == WorldPosition())
        {
            if (botAI->HasStrategy("debug", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Failed to find any position for quest {}, abandoning", 
                          bot->GetName(), questId);
            }
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
    }

    // Use upstream movement logic - already uses MoveFarTo for proper pathing
    if (bot->GetDistance(botAI->rpgInfo.do_quest.pos) > 10.0f && !botAI->rpgInfo.do_quest.lastReachPOI)
    {
        return MoveFarTo(botAI->rpgInfo.do_quest.pos);
    }

    // Now we are near the quest objective - check for locked GameObject requirements first
    if (!botAI->rpgInfo.do_quest.lastReachPOI)
    {
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} Arrived at quest POI for quest {}", bot->GetName(), questId);
        }
        
        // CHECK IF THIS QUEST OBJECTIVE INVOLVES A LOCKED GAMEOBJECT
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        int32 objectiveIdx = botAI->rpgInfo.do_quest.objectiveIdx;
        
        if (quest && objectiveIdx >= 0 && objectiveIdx < QUEST_OBJECTIVES_COUNT)
        {
            int32 requiredNpcOrGo = quest->RequiredNpcOrGo[objectiveIdx];
            if (requiredNpcOrGo < 0) // GameObject objective
            {
                uint32 goEntry = (uint32)(-requiredNpcOrGo);
                GuidVector nearbyGOs = AI_VALUE(GuidVector, "nearest game objects");
                
                for (const ObjectGuid& guid : nearbyGOs)
                {
                    GameObject* go = ObjectAccessor::GetGameObject(*bot, guid);
                    if (go && go->GetEntry() == goEntry && go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
                    {
                        uint32 reqItem, skillId, reqSkillValue;
                        if (!CheckGameObjectLockRequirements(go, reqItem, skillId, reqSkillValue) && reqItem > 0)
                        {
                            // Need to get the key item first - check if we can get it from quest drops
                            if (HasQuestItemInDropTable(questId, reqItem))
                            {
                                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                                {
                                    ItemTemplate const* keyProto = sObjectMgr->GetItemTemplate(reqItem);
                                    LOG_DEBUG("playerbots", "[New RPG] {} Quest {} requires key item {} ({}) - switching to kill objectives first", 
                                             bot->GetName(), questId, reqItem, 
                                             keyProto ? keyProto->Name1 : "Unknown");
                                }
                                
                                // Switch to hunting for the drop item instead
                                return SearchForActualQuestTargets(questId);
                            }
                        }
                        break;
                    }
                }
            }
        }
        
        botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();
        
        // Try immediate interaction with quest objectives (unified approach for NPCs and GOs)
        if (TryInteractWithQuestObjective(questId, botAI->rpgInfo.do_quest.objectiveIdx))
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Successfully interacting with quest objective on arrival", 
                         bot->GetName());
            }
            return true;
        }
        
        return true;
    }

    // Enhanced timeout logic - try smart search before abandoning
    if (GetMSTimeDiffToNow(botAI->rpgInfo.do_quest.lastReachPOI) >= poiStayTime)
    {
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} Timeout at POI for quest {}, trying direct interaction before abandoning", 
                      bot->GetName(), questId);
        }

        // First try direct interaction with quest objectives (unified approach for NPCs and GOs)
        if (TryInteractWithQuestObjective(questId, botAI->rpgInfo.do_quest.objectiveIdx))
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Found and interacting with quest objective, resetting timer", 
                          bot->GetName());
            }
            botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();
            return true;
        }

        // Fallback: try smart search for quest targets
        if (SearchForActualQuestTargets(questId))
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Found actual quest target during timeout, resetting timer", 
                          bot->GetName());
            }
            botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();
            return true;
        }
        
        // Keep upstream progression checking and abandonment logic
        bool hasProgression = false;
        int32 currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;
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
            if (botAI->HasStrategy("debug", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} No progression detected, abandoning quest {}", 
                          bot->GetName(), questId);
            }
            botAI->lowPriorityQuest.insert(questId);
            botAI->rpgStatistic.questAbandoned++;
            botAI->rpgStatistic.questAbandonedByID[questId]++;
            botAI->rpgStatistic.questAbandonReasons["no_progression"]++;
            botAI->rpgStatistic.questAbandonReasonsByID[questId]["no_progression"]++;
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }

        // Clear and select another poi later
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} Clearing POI state for quest {} to try new location", 
                      bot->GetName(), questId);
        }
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
        botAI->rpgInfo.do_quest.pos = WorldPosition();
        botAI->rpgInfo.do_quest.objectiveIdx = 0;
        return true;
    }

    // Allow natural completion through wandering - just like upstream
    return MoveRandomNear(20.0f);
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
        botAI->rpgStatistic.questCompletedByID[questId]++;
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
        
        // Use upstream's clean approach - no fancy Z calculations
        float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), 
                           bot->GetMap()->GetWaterLevel(dx, dy));

        // double check for upstream POI logic
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
        botAI->rpgStatistic.questAbandonedByID[questId]++;
        botAI->rpgStatistic.questAbandonReasons["reward_issue"]++;
        botAI->rpgStatistic.questAbandonReasonsByID[questId]["reward_issue"]++;
        if (botAI->HasStrategy("debug", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
        }
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
        if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} active taxi path {} (from {} to {}) failed", bot->GetName(),
                      flightMaster->GetEntry(), nodes[0], nodes[1]);
        }
        botAI->rpgInfo.ChangeToIdle();
    }
    return true;
}
