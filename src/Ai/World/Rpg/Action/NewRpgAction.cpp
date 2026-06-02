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
#include "TravelNode.h"
#include "World.h"
#include "PossibleRpgTargetsValue.h"
#include "Trainer.h"

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

    bool debug = botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT) || sPlayerbotAIConfig.rpgDebugDistrictTracking;

    // --- 1. District change detection ---
    uint32 newDistrictId = GetCurrentDistrictId(bot);
    if (newDistrictId != info.currentDistrictId)
    {
        // Record old district visit when leaving
        if (info.currentDistrictId != 0)
        {
            DistrictVisit* oldVisit = info.GetCurrentDistrictVisit();
            if (oldVisit)
            {
                info.RecordDistrictVisit(info.currentDistrictId, oldVisit->npcsVisited, oldVisit->npcsTotal);
                if (debug)
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Left district {} (visited {} NPCs)",
                              bot->GetName(), info.currentDistrictId, oldVisit->npcsVisited);
                }
            }
        }

        // Reset for new district
        info.currentDistrictId = newDistrictId;
        info.currentDistrictCenter = GetDistrictCenter(bot, newDistrictId);
        info.cachedDistrictId = 0; // Force cache rebuild
        info.lastCacheUpdate = 0;

        if (debug)
        {
            const AreaTableEntry* areaEntry = sAreaTableStore.LookupEntry(newDistrictId);
            std::string districtName = areaEntry ? areaEntry->area_name[0] : "Unknown";
            LOG_DEBUG("playerbots", "[New RPG] {} Entered district {} ({})",
                      bot->GetName(), newDistrictId, districtName);
        }
    }

    // --- 2. Cache management ---
    if (GetMSTimeDiffToNow(info.lastCacheUpdate) > sPlayerbotAIConfig.rpgNpcCacheTTL ||
        info.cachedDistrictId != info.currentDistrictId || info.cachedNpcs.empty())
    {
        UpdateNpcCache();
    }

    // Prune old visits
    info.PruneOldVisits(30 * 60 * 1000); // 30 min
    info.PruneDistrictVisits(sPlayerbotAIConfig.rpgDistrictCooldown, sPlayerbotAIConfig.rpgDistrictMaxVisits);

    // --- 3. Select best NPC ---
    if (info.wander_npc.npcOrGo.IsEmpty())
    {
        ObjectGuid targetGuid = SelectBestNpcFromCache();
        if (targetGuid.IsEmpty())
        {
            // No useful NPCs in current district, try moving to another district
            uint32 nextDistrict = GetNextUnvisitedDistrict();
            if (nextDistrict != 0)
            {
                WorldPosition districtCenter = GetDistrictCenter(bot, nextDistrict);
                if (debug)
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Moving to district {} (center {:.1f},{:.1f})",
                              bot->GetName(), nextDistrict, districtCenter.GetPositionX(), districtCenter.GetPositionY());
                }
                MoveFarTo(districtCenter);
                return true;
            }

            // No more districts to explore, transition to IDLE
            if (debug)
            {
                LOG_DEBUG("playerbots", "[New RPG] {} No useful NPCs remaining, transitioning to IDLE",
                          bot->GetName());
            }
            info.ChangeToIdle();
            return true;
        }

        info.wander_npc.npcOrGo = targetGuid;
        info.wander_npc.lastReach = 0;

        // Find corresponding cache entry for debug
        for (CachedNpc const& npc : info.cachedNpcs)
        {
            if (npc.guid == targetGuid)
            {
                if (debug)
                {
                    float dist = npc.fromTravelMgr ?
                        bot->GetDistance(npc.pos) : bot->GetDistance2d(npc.pos.GetPositionX(), npc.pos.GetPositionY());
                    LOG_DEBUG("playerbots", "[New RPG] {} Selected NPC (utility {:.2f}, dist {:.0f}yd)",
                              bot->GetName(), npc.utility, dist);
                }
                break;
            }
        }
        return true;
    }

    // --- 4. Move to NPC ---
    WorldObject* object = ObjectAccessor::GetWorldObject(*bot, info.wander_npc.npcOrGo);

    // Check if this is a TravelMgr entry (not in world)
    bool isTravelMgrEntry = false;
    for (CachedNpc const& npc : info.cachedNpcs)
    {
        if (npc.guid == info.wander_npc.npcOrGo && npc.fromTravelMgr)
        {
            isTravelMgrEntry = true;
            if (!object || !object->IsInWorld())
            {
                // Move to cached position
                if (bot->GetDistance(npc.pos) > INTERACTION_DISTANCE)
                {
                    return MoveFarTo(npc.pos);
                }
                // Close enough, try to find actual creature
                GuidVector nearby = AI_VALUE(GuidVector, "possible new rpg targets");
                for (ObjectGuid const& guid : nearby)
                {
                    Creature* c = ObjectAccessor::GetCreature(*bot, guid);
                    if (c && c->IsInWorld() && c->GetDistance2d(npc.pos.GetPositionX(), npc.pos.GetPositionY()) < 15.0f)
                    {
                        object = c;
                        break;
                    }
                }
            }
            break;
        }
    }

    if (!object || !object->IsInWorld())
    {
        if (!isTravelMgrEntry)
        {
            info.wander_npc.npcOrGo = ObjectGuid();
            info.wander_npc.lastReach = 0;
            return true;
        }
        // TravelMgr entry not found nearby, skip
        info.ignoredRpgNpcs[info.wander_npc.npcOrGo] = getMSTime();
        info.wander_npc.npcOrGo = ObjectGuid();
        info.wander_npc.lastReach = 0;
        return true;
    }

    if (bot->GetDistance(object) > INTERACTION_DISTANCE)
    {
        return MoveWorldObjectTo(info.wander_npc.npcOrGo);
    }

    // --- 5. Wait gate (NEW — fixes vendor spam) ---
    if (info.wander_npc.lastReach && GetMSTimeDiffToNow(info.wander_npc.lastReach) < npcStayTime)
    {
        if (debug)
        {
            uint32 elapsed = GetMSTimeDiffToNow(info.wander_npc.lastReach) / 1000;
            uint32 total = npcStayTime / 1000;
            LOG_DEBUG("playerbots", "[New RPG] {} Vendor wait: skipping interactions ({}s/{}s)",
                      bot->GetName(), elapsed, total);
        }
        return false;
    }

    bool interacted = false;
    Creature* creature = object->ToCreature();

    // --- 6. Interact ---

    // 6a. Quest NPCs
    if (bot->CanInteractWithQuestGiver(object))
    {
        InteractWithNpcOrGameObjectForQuest(info.wander_npc.npcOrGo);
        interacted = true;
    }

    // 6b. Flight master with unknown node
    if (creature && creature->HasNpcFlag(UNIT_NPC_FLAG_FLIGHTMASTER))
    {
        uint32 nodeId = GetTaxiNodeForCreature(creature);
        if (nodeId && !bot->m_taxi.IsTaximaskNodeKnown(nodeId))
        {
            DiscoverFlightPath(creature);
            interacted = true;
        }
    }

    // 6c. Get creature for NPC interaction
    creature = bot->GetNPCIfCanInteractWith(info.wander_npc.npcOrGo, UNIT_NPC_FLAG_NONE);
    if (!creature)
    {
        info.ignoredRpgNpcs[info.wander_npc.npcOrGo] = getMSTime();
        info.wander_npc.npcOrGo = ObjectGuid();
        info.wander_npc.lastReach = 0;
        return true;
    }

    uint32 npcFlags = creature->GetCreatureTemplate()->npcflag;

    // 6d. Trainers
    {
        Trainer::Trainer const* trainerData = sObjectMgr->GetTrainer(creature->GetEntry());
        if (trainerData && trainerData->IsTrainerValidForPlayer(bot))
        {
            Trainer::Type tType = trainerData->GetTrainerType();

            // Pre-validate tradeskill trainers
            if (tType == Trainer::Type::Tradeskill)
            {
                static TrainerClassifier classifier;
                if (!classifier.IsValidSecondaryTrainer(bot, creature))
                {
                    info.ignoredRpgNpcs[creature->GetGUID()] = getMSTime();
                    info.wander_npc.npcOrGo = ObjectGuid();
                    info.wander_npc.lastReach = 0;
                    return true;
                }
            }

            // Check for green spells
            bool hasGreenSpells = false;
            for (Trainer::Spell const& tSpell : trainerData->GetSpells())
            {
                if (trainerData->CanTeachSpell(bot, &tSpell))
                {
                    hasGreenSpells = true;
                    break;
                }
            }

            if (!hasGreenSpells)
            {
                info.ignoredRpgNpcs[creature->GetGUID()] = getMSTime();
                info.wander_npc.npcOrGo = ObjectGuid();
                info.wander_npc.lastReach = 0;
                return true;
            }

            bot->SetSelection(info.wander_npc.npcOrGo);
            botAI->DoSpecificAction("trainer", Event("trainer"));
            interacted = true;

            if (debug)
            {
                std::string tName = (tType == Trainer::Type::Class) ? "class" :
                                   (tType == Trainer::Type::Mount) ? "riding" :
                                   (tType == Trainer::Type::Pet) ? "pet" : "profession";
                LOG_DEBUG("playerbots", "[New RPG] {} Interacting with {} trainer: {}",
                          bot->GetName(), tName, creature->GetName());
            }
        }
    }

    // 6e. Vendors
    if (npcFlags & UNIT_NPC_FLAG_VENDOR_MASK)
    {
        uint8 bagBefore = AI_VALUE(uint8, "bag space");
        botAI->DoSpecificAction("sell", Event("sell", "vendor"));
        botAI->DoSpecificAction("buy", Event("buy", "vendor"));
        uint8 bagAfter = AI_VALUE(uint8, "bag space");
        interacted = true;
        if (debug)
        {
            LOG_DEBUG("playerbots", "[New RPG] {} Vendor sell+buy: {} bagSpace {}% -> {}%",
                      bot->GetName(), creature->GetName(), bagBefore, bagAfter);
        }
    }

    // 6f. Repair
    if (npcFlags & UNIT_NPC_FLAG_REPAIR)
    {
        bot->SetSelection(info.wander_npc.npcOrGo);
        botAI->DoSpecificAction("repair", Event("repair"));
        interacted = true;
    }

    // --- 7. Post-interaction bookkeeping ---
    if (!info.wander_npc.lastReach)
    {
        // Always set lastReach on first reach, even if nothing to interact with.
        // This prevents infinite looping on NPCs the bot can't use (stable masters, etc.)
        info.wander_npc.lastReach = getMSTime();

        // Mark as visited immediately to prevent cache rebuild from re-selecting this NPC
        // before the wait period completes
        info.ignoredRpgNpcs[info.wander_npc.npcOrGo] = getMSTime();

        if (debug && !interacted)
        {
            LOG_DEBUG("playerbots", "[New RPG] {} No applicable interaction for {}, will skip after wait",
                      bot->GetName(), creature->GetName());
        }
        return false;
    }
    else if (GetMSTimeDiffToNow(info.wander_npc.lastReach) < npcStayTime)
    {
        return false;
    }

    // Mark NPC as visited
    info.ignoredRpgNpcs[creature->GetGUID()] = getMSTime();

    // Increment district visit counter
    DistrictVisit* districtVisit = info.GetCurrentDistrictVisit();
    if (districtVisit)
        ++districtVisit->npcsVisited;

    // For flight masters: update cache
    if (creature->HasNpcFlag(UNIT_NPC_FLAG_FLIGHTMASTER))
    {
        for (CachedNpc& npc : info.cachedNpcs)
        {
            if (npc.guid == creature->GetGUID())
            {
                npc.taxiNodeKnown = true;
                npc.utility = 0.35f;
                break;
            }
        }
    }

    // --- 8. District exhaustion check ---
    if (IsDistrictExhausted(info.currentDistrictId))
    {
        if (debug)
        {
            LOG_DEBUG("playerbots", "[New RPG] {} District {} exhausted after {} NPC visits",
                      bot->GetName(), info.currentDistrictId, districtVisit->npcsVisited);
        }

        info.RecordDistrictVisit(info.currentDistrictId, districtVisit->npcsVisited, districtVisit->npcsTotal);

        uint32 nextDistrict = GetNextUnvisitedDistrict();
        if (nextDistrict != 0)
        {
            WorldPosition districtCenter = GetDistrictCenter(bot, nextDistrict);
            if (debug)
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Moving to next district {} (center {:.1f},{:.1f})",
                          bot->GetName(), nextDistrict, districtCenter.GetPositionX(), districtCenter.GetPositionY());
            }
            info.wander_npc.npcOrGo = ObjectGuid();
            info.wander_npc.lastReach = 0;
            MoveFarTo(districtCenter);
            return true;
        }
    }

    // --- 9. Reset & pick new target ---
    info.wander_npc.npcOrGo = ObjectGuid();
    info.wander_npc.lastReach = 0;
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
            return DoIncompleteQuest(info.quest);
        case QUEST_STATUS_COMPLETE:
            return DoCompletedQuest(info.quest);
        default:
            break;
    }
    botAI->rpgInfo.ChangeToIdle();
    return true;
}

bool NewRpgDoQuestAction::DoIncompleteQuest(NewRpgInfo::DoQuest& data)
{
    uint32 questId = RPG_INFO(do_quest, questId);
    
    // Keep upstream objective completion checking logic
    if (botAI->rpgInfo.do_quest.pos != WorldPosition())
    {
        int32 currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;

        // For area triggers (encoded as negative), skip objective tracking - server handles completion automatically
        // The main Execute() function will detect quest completion via bot->GetQuestStatus() and route to DoCompletedQuest()
        if (currentObjective < -100)
        {
            // Area trigger quest - don't try to check individual objectives
            // Just let the server's area trigger completion update the quest status
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Area trigger quest {} - waiting for server completion",
                          bot->GetName(), questId);
            }
        }
        else
        {
            // Regular quest objective checking
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
    }

    if (botAI->rpgInfo.do_quest.pos == WorldPosition())
    {
        // STEP 1: Use clean upstream POI system first
        std::vector<POIInfo> poiInfo;
        if (GetQuestPOIPosAndObjectiveIdx(questId, poiInfo))
        {
            uint32 rndIdx = urand(0, poiInfo.size() - 1);
            POIInfo& selectedPOI = poiInfo[rndIdx];
            G3D::Vector2 nearestPoi = selectedPOI.pos;
            int32 objectiveIdx = selectedPOI.objectiveIdx;

            float dx = nearestPoi.x, dy = nearestPoi.y;
            float dz;

            // Check if this POI has a specific Z coordinate (e.g., area triggers)
            if (selectedPOI.useExactZ)
            {
                // Use the exact Z coordinate from the POI (for area triggers in mines, caves, etc.)
                dz = selectedPOI.z;

                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Using exact Z coordinate {} from area trigger POI",
                             bot->GetName(), dz);
                }
            }
            else
            {
                // Calculate Z from ground/water level for regular polygon POIs
                dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT),
                             bot->GetMap()->GetWaterLevel(dx, dy));
            }

            if (dz != INVALID_HEIGHT && dz != VMAP_INVALID_HEIGHT_VALUE)
            {
                WorldPosition pos(bot->GetMapId(), dx, dy, dz);
                botAI->rpgInfo.do_quest.lastReachPOI = 0;
                botAI->rpgInfo.do_quest.pos = pos;

                // For area triggers, use the radius as objectiveIdx's sign to indicate special handling
                // Store as negative to signal "move to exact coordinates" vs "move within 10 yards"
                if (selectedPOI.radius > 0.0f)
                {
                    // Area trigger: store radius as-(objectiveIdx + 100) to encode both values
                    // We'll decode this later to know it's an area trigger with specific radius
                    botAI->rpgInfo.do_quest.objectiveIdx = -(int32)(selectedPOI.radius * 10.0f + 1000); // Encode radius * 10 + offset

                    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                    {
                        LOG_DEBUG("playerbots", "[New RPG] {} Set area trigger POI position for quest {} at ({}, {}, {}) - must enter radius {} (encoded as {})",
                                 bot->GetName(), questId, dx, dy, dz, selectedPOI.radius, botAI->rpgInfo.do_quest.objectiveIdx);
                    }
                }
                else
                {
                    // Regular POI
                    botAI->rpgInfo.do_quest.objectiveIdx = objectiveIdx;

                    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                    {
                        LOG_DEBUG("playerbots", "[New RPG] {} Set POI position for quest {} at ({}, {}, {})",
                                  bot->GetName(), questId, dx, dy, dz);
                    }
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
        
        // STEP 3: Still no position - check if vendor has needed items before giving up
        if (botAI->rpgInfo.do_quest.pos == WorldPosition())
        {
            if (HasNeededQuestItemForSale())
            {
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Quest {} has no POI but vendor has needed items - wandering to find vendor",
                              bot->GetName(), questId);
                }
                botAI->rpgInfo.ChangeToWanderNpc();
                return true;
            }

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
    // Check if this is an area trigger (encoded as negative objectiveIdx)
    float movementTolerance = 10.0f;
    if (botAI->rpgInfo.do_quest.objectiveIdx < -100)
    {
        // Decode area trigger radius from negative objectiveIdx
        float radius = (float)(-(botAI->rpgInfo.do_quest.objectiveIdx + 1000)) / 10.0f;
        // Move to half the radius to ensure we enter the trigger zone
        movementTolerance = std::max(2.0f, radius * 0.5f);

        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} Area trigger detected, using movement tolerance {} (radius {})",
                     bot->GetName(), movementTolerance, radius);
        }
    }

    if (bot->GetDistance(botAI->rpgInfo.do_quest.pos) > movementTolerance && !botAI->rpgInfo.do_quest.lastReachPOI)
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

bool NewRpgDoQuestAction::DoCompletedQuest(NewRpgInfo::DoQuest& data)
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

    ObjectGuid flightMasterGuid = botAI->rpgInfo.flight.fromFlightMaster;
    uint32 flightMasterEntry = flightMasterGuid.GetEntry();
    WorldPosition const& cachedPos = botAI->rpgInfo.flight.fromFlightMasterPos;

    Creature* flightMaster = ObjectAccessor::GetCreature(*bot, flightMasterGuid);

    if (!flightMaster && cachedPos != WorldPosition())
    {
        float distToCached = bot->GetDistance(cachedPos);
        LOG_DEBUG("playerbots", "[New RPG] {} NewRpgTravelFlightAction: flight master not in grid, moving to cached position ({:.1f}yd)", bot->GetName(), distToCached);

        if (distToCached > INTERACTION_DISTANCE)
            return MoveFarTo(cachedPos);

        // Close to cached position — try to find creature by entry nearby
        GuidVector nearby = AI_VALUE(GuidVector, "possible new rpg targets");
        for (ObjectGuid const& guid : nearby)
        {
            Creature* c = ObjectAccessor::GetCreature(*bot, guid);
            if (c && c->IsInWorld() && c->GetEntry() == flightMasterEntry &&
                c->GetDistance2d(cachedPos.GetPositionX(), cachedPos.GetPositionY()) < 15.0f)
            {
                flightMaster = c;
                break;
            }
        }

        if (!flightMaster)
        {
            LOG_DEBUG("playerbots", "[New RPG] {} NewRpgTravelFlightAction: flight master entry {} not found near cached position", bot->GetName(), flightMasterEntry);
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
    }
    else if (!flightMaster)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} NewRpgTravelFlightAction: flight master creature not found (guid counter {}, no cached position)", bot->GetName(), flightMasterGuid.GetCounter());
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }

    if (!flightMaster->IsAlive())
    {
        LOG_DEBUG("playerbots", "[New RPG] {} NewRpgTravelFlightAction: flight master creature {} is dead", bot->GetName(), flightMaster->GetEntry());
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }
    const TaxiNodesEntry* entry = sTaxiNodesStore.LookupEntry(botAI->rpgInfo.flight.toNode);
    float dist = bot->GetDistance(flightMaster);
    if (dist > INTERACTION_DISTANCE)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} NewRpgTravelFlightAction: moving to flight master {} from {:.1f}yd", bot->GetName(), flightMaster->GetEntry(), dist);
        return MoveFarTo(flightMaster);
    }
    uint32 fromNode = botAI->rpgInfo.flight.fromNode;
    uint32 toNode = botAI->rpgInfo.flight.toNode;
    LOG_DEBUG("playerbots", "[New RPG] {} NewRpgTravelFlightAction: at flight master entry {}, activating taxi from node {} to {}", bot->GetName(), flightMaster->GetEntry(), fromNode, toNode);
    std::vector<uint32> nodes = sTravelNodeMap.FindTaxiPath(fromNode, toNode);
    if (nodes.empty())
    {
        LOG_DEBUG("playerbots", "[New RPG] {} NewRpgTravelFlightAction: FindTaxiPath returned empty, falling back to direct {}-{}", bot->GetName(), fromNode, toNode);
        nodes = {fromNode, toNode};
    }
    else
    {
        std::string pathStr;
        for (size_t i = 0; i < nodes.size(); i++)
            pathStr += std::to_string(nodes[i]) + (i + 1 < nodes.size() ? " -> " : "");
        LOG_DEBUG("playerbots", "[New RPG] {} NewRpgTravelFlightAction: FindTaxiPath returned {} nodes: {}", bot->GetName(), nodes.size(), pathStr);
    }

    botAI->RemoveShapeshift();
    if (bot->IsMounted())
    {
        bot->Dismount();
    }
    if (!bot->ActivateTaxiPathTo(nodes, flightMaster, 0))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} NewRpgTravelFlightAction: ActivateTaxiPathTo from node {} to node {} FAILED ({} nodes in path)", bot->GetName(), nodes[0], nodes.size() > 1 ? nodes[1] : 0, nodes.size());
        botAI->rpgInfo.ChangeToIdle();
    }
    else
    {
        LOG_DEBUG("playerbots", "[New RPG] {} NewRpgTravelFlightAction: ActivateTaxiPathTo SUCCESS, entering flight", bot->GetName());
    }
    return true;
}
