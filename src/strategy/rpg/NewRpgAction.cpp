#include "NewRpgAction.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "ChatHelper.h"
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
#include "BroadcastHelper.h"
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
    /// @TODO: Refactor by transition probability
    switch (info.status)
    {
        case RPG_IDLE:
        {
            uint32 roll = urand(1, 100);
            // IDLE -> NEAR_NPC
            if (roll <= 30)
            {
                GuidVector possibleTargets = AI_VALUE(GuidVector, "possible new rpg targets");
                if (possibleTargets.size() >= 3)
                {
                    info.ChangeToNearNpc();
                    return true;
                }
            }
            // IDLE -> GO_INNKEEPER
            else if (roll <= 45)
            {
                WorldPosition pos = SelectRandomInnKeeperPos(bot);
                if (pos != WorldPosition() && bot->GetExactDist(pos) > 50.0f)
                {
                    info.ChangeToGoInnkeeper(pos);
                    return true;
                }
            }
            // IDLE -> GO_GRIND
            else if (roll <= 100)
            {
                if (roll >= 60)
                {
                    std::vector<uint32> availableQuests;
                    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
                    {
                        uint32 questId = bot->GetQuestSlotQuestId(slot);
                        if (botAI->lowPriorityQuest.find(questId) != botAI->lowPriorityQuest.end())
                            continue;

                        std::vector<POIInfo> poiInfo;
                        if (GetQuestPOIPosAndObjectiveIdx(questId, poiInfo, true))
                        {
                            availableQuests.push_back(questId);
                        }
                    }
                    if (availableQuests.size())
                    {
                        uint32 questId = availableQuests[urand(0, availableQuests.size() - 1)];
                        const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
                        if (quest)
                        {
                            // IDLE -> DO_QUEST
                            info.ChangeToDoQuest(questId, quest);
                            return true;
                        }
                    }
                }
                WorldPosition pos = SelectRandomGrindPos(bot);
                if (pos != WorldPosition())
                {
                    info.ChangeToGoGrind(pos);
                    return true;
                }
            }
            // IDLE -> REST
            info.ChangeToRest();
            bot->SetStandState(UNIT_STAND_STATE_SIT);
            return true;
        }
        case RPG_GO_GRIND:
        {
            WorldPosition& originalPos = info.go_grind.pos;
            assert(info.go_grind.pos != WorldPosition());
            // GO_GRIND -> NEAR_RANDOM
            if (bot->GetExactDist(originalPos) < 10.0f)
            {
                info.ChangeToNearRandom();
                return true;
            }
            break;
        }
        case RPG_GO_INNKEEPER:
        {
            WorldPosition& originalPos = info.go_innkeeper.pos;
            assert(info.go_innkeeper.pos != WorldPosition());
            // GO_INNKEEPER -> NEAR_NPC
            if (bot->GetExactDist(originalPos) < 10.0f)
            {
                info.ChangeToNearNpc();
                return true;
            }
            break;
        }
        case RPG_NEAR_RANDOM:
        {
            // NEAR_RANDOM -> IDLE
            if (info.HasStatusPersisted(statusNearRandomDuration))
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
        case RPG_NEAR_NPC:
        {
            if (info.HasStatusPersisted(statusNearNpcDuration))
            {
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

bool NewRpgGoInnKeeperAction::Execute(Event event)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    return MoveFarTo(botAI->rpgInfo.go_innkeeper.pos);
}

bool NewRpgMoveRandomAction::Execute(Event event)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;
    
    return MoveRandomNear();
}

bool NewRpgMoveNpcAction::Execute(Event event)
{
    NewRpgInfo& info = botAI->rpgInfo;

    if (!info.near_npc.npcOrGo)
    {
        // No NPC can be found, switch to IDLE
        ObjectGuid npcOrGo = ChooseNpcOrGameObjectToInteract();
        if (npcOrGo.IsEmpty())
        {
            info.ChangeToIdle();
            return true;
        }
        info.near_npc.npcOrGo = npcOrGo;
        info.near_npc.lastReach = 0;
        return true;
    }

    WorldObject* object = ObjectAccessor::GetWorldObject(*bot, info.near_npc.npcOrGo);

    bool interacted = false;  // Track if the bot has interacted with the NPC

    // --- Step 1: Ensure bot is close enough to interact ---
    if (!object || bot->GetDistance(object) > INTERACTION_DISTANCE)
    {
        botAI->TellMaster("Moving to interact with target.");
        return MoveWorldObjectTo(info.near_npc.npcOrGo);
    }

    // --- Step 2: Handle Quest NPCs ---
    if (bot->CanInteractWithQuestGiver(object))
    {
        botAI->TellMaster("Interacting with quest NPC.");
        InteractWithNpcOrGameObjectForQuest(info.near_npc.npcOrGo);
        interacted = true;
    }

    // --- Step 3: Detect NPC and Retrieve Details ---
    botAI->TellMaster("Checking NPC GUID: " + info.near_npc.npcOrGo.ToString());
    Creature* creature = bot->GetNPCIfCanInteractWith(info.near_npc.npcOrGo, UNIT_NPC_FLAG_NONE);

    if (!creature)
    {
        botAI->TellMaster("No valid NPC found for interaction.");
        return true;
    }

    std::string npcName = creature->GetName();
    uint32 npcFlags = creature->GetCreatureTemplate()->npcflag;
    botAI->TellMaster("Found NPC: " + npcName + " (Flags: " + std::to_string(npcFlags) + ")");

    // --- Step 4: Handle Trainers ---
    if (creature->IsValidTrainerForPlayer(bot))
    {
        botAI->TellMaster("NPC: " + npcName + " is a valid trainer for me.");
        bot->SetSelection(info.near_npc.npcOrGo);
        botAI->TellMaster("Training with " + npcName + ".");
        botAI->DoSpecificAction("trainer", Event("trainer"));
        interacted = true;
    }

    // --- Step 5: Handle Vendors ---
    if (npcFlags & UNIT_NPC_FLAG_VENDOR_MASK)
    {
        botAI->TellMaster("NPC: " + npcName + " is a vendor.");
        botAI->TellMaster("Buying and selling at " + npcName + ".");
        botAI->DoSpecificAction("buy", Event("buy", "vendor"));
        botAI->DoSpecificAction("sell", Event("sell", "vendor"));
        interacted = true;
    }

    // --- Step 6: Handle Repair Vendors ---
    if (npcFlags & UNIT_NPC_FLAG_REPAIR)
    {
        botAI->TellMaster("NPC: " + npcName + " offers repairs.");
        bot->SetSelection(info.near_npc.npcOrGo);
        botAI->TellMaster("Repairing items at " + npcName + ".");
        botAI->DoSpecificAction("repair", Event("repair"));
        interacted = true;
    }

    // --- Step 7: Apply Waiting Logic (EVEN IF NO INTERACTION) ---
    if (!info.near_npc.lastReach)
    {
        info.near_npc.lastReach = getMSTime();  // ✅ Set once for ALL cases
        botAI->TellMaster("Pausing near " + npcName + " for " + std::to_string(npcStayTime) + "ms.");
        return false;
    }
    else if (GetMSTimeDiffToNow(info.near_npc.lastReach) < npcStayTime)
    {
        botAI->TellMaster("Waiting at " + npcName + " for " + std::to_string(npcStayTime - GetMSTimeDiffToNow(info.near_npc.lastReach)) + "ms.");
        return false;
    }

    // --- Step 8: Reset & Move to Next Target ---
    botAI->TellMaster("Finished interacting with " + npcName + ". Moving to next target.");
    info.near_npc.npcOrGo = ObjectGuid();
    info.near_npc.lastReach = 0;

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
    if (botAI->rpgInfo.do_quest.pos != WorldPosition())
    {
        /// @TODO: extract to a new function
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
            botAI->rpgInfo.do_quest.lastReachPOI = 0;
            botAI->rpgInfo.do_quest.pos = WorldPosition();
            botAI->rpgInfo.do_quest.objectiveIdx = 0;
        }
    }
    if (botAI->rpgInfo.do_quest.pos == WorldPosition())
    {
        std::vector<POIInfo> poiInfo;
        if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo))
        {
            // can't find a poi pos to go, stop doing quest for now
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        uint32 rndIdx = urand(0, poiInfo.size() - 1);
        G3D::Vector2 nearestPoi = poiInfo[rndIdx].pos;
        int32 objectiveIdx = poiInfo[rndIdx].objectiveIdx;

        float dx = nearestPoi.x, dy = nearestPoi.y;

        // z = MAX_HEIGHT as we do not know accurate z
        float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(dx, dy));

        // double check for GetQuestPOIPosAndObjectiveIdx
        if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
            return false;

        WorldPosition pos(bot->GetMapId(), dx, dy, dz);
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
        botAI->rpgInfo.do_quest.pos = pos;
        botAI->rpgInfo.do_quest.objectiveIdx = objectiveIdx;
    }

    if (bot->GetDistance(botAI->rpgInfo.do_quest.pos) > INTERACTION_DISTANCE - 2.0f && !botAI->rpgInfo.do_quest.lastReachPOI)
    {
        return MoveFarTo(botAI->rpgInfo.do_quest.pos);
    }
    // Now we are near the quest objective
    // kill mobs and looting quest should be done automatically by grind strategy
    // We are close enough to the POI XY, now try to refine the Z by scanning nearby units or GOs matching quest objectives
    const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        int32 npcOrGo = quest->RequiredNpcOrGo[i];
        if (!npcOrGo)
            continue;
    
        if (npcOrGo > 0) // NPC
        {
            GuidVector npcs = AI_VALUE(GuidVector, "nearest quest npcs");
            for (ObjectGuid const& guid : npcs)
            {
                Unit* unit = botAI->GetUnit(guid);
                if (!unit || unit->GetEntry() != uint32(npcOrGo))
                    continue;
    
                // Repath to NPC's true position
                Position const& realPos = unit->GetPosition();
                WorldPosition newPos(bot->GetMapId(), realPos.GetPositionX(), realPos.GetPositionY(), realPos.GetPositionZ());
    
                if (bot->GetExactDist(newPos) > INTERACTION_DISTANCE - 2.0f)
                {
                    botAI->TellMaster("Refining objective position to actual NPC " + unit->GetName() + " at Z=" + std::to_string(realPos.GetPositionZ()));
                    botAI->rpgInfo.do_quest.pos = newPos;
                    return MoveFarTo(newPos);
                }
            }
        }
        else if (npcOrGo < 0) // GameObject
        {
            GuidVector gos = AI_VALUE(GuidVector, "nearest game objects no los");
            for (ObjectGuid const& guid : gos)
            {
                GameObject* go = botAI->GetGameObject(guid);
                if (!go || go->GetEntry() != uint32(-npcOrGo))
                    continue;
    
                Position const& realPos = go->GetPosition();
                WorldPosition newPos(bot->GetMapId(), realPos.GetPositionX(), realPos.GetPositionY(), realPos.GetPositionZ());
    
                if (bot->GetExactDist(newPos) > INTERACTION_DISTANCE - 2.0f)
                {
                    botAI->TellMaster("Refining objective position to actual GameObject " + go->GetNameForLocaleIdx(sWorld->GetDefaultDbcLocale()) + " at Z=" + std::to_string(realPos.GetPositionZ()));
                    botAI->rpgInfo.do_quest.pos = newPos;
                    return MoveFarTo(newPos);
                }
            }
        }
    }

    // 1) First, see if we can produce any missing quest items from inventory "playercast" items while close to the objective
    //    (like "Empty Tainted Ooze Jar" or anything else that creates quest objectives).
    {
        // Gather how many quest items we still need
        std::map<uint32,int32> missingItems = GetMissingQuestItems(bot, quest);

        // Gather all items in bot's bags flagged as "playercast"
        // (the user said: "std::vector<Item*> playercastItems= AI_VALUE2(...)")
        std::vector<Item*> playercastItems = AI_VALUE2(std::vector<Item*>, "inventory items", "playercast");

        // Attempt to use one of them on a valid corpse/object to produce the quest item
        bool usedSomething = TryUseQuestProductionItems(bot, botAI, quest, missingItems, playercastItems);
        if (usedSomething)
            return true; // We used an item; next bot update tick can see if we got the item.
    }

    uint32 startItemId = quest->GetSrcItemId();
    
    // No StartItem for this quest, just try interacting with the unit/object
    if (!startItemId)
    {
        botAI->TellMaster("Quest [" + std::to_string(questId) + "] has no StartItem — checking for direct GameObject interaction objectives.");
    
        for (int32 objectiveIdx = 0; objectiveIdx < QUEST_OBJECTIVES_COUNT; ++objectiveIdx)
        {
            int32 npcOrGo = quest->RequiredNpcOrGo[objectiveIdx];
            if (!npcOrGo || npcOrGo > 0) // Only care about GameObjects here
                continue;
    
            uint32 goEntry = uint32(-npcOrGo);
            GuidVector gos = AI_VALUE(GuidVector, "nearest game objects no los");
    
            for (ObjectGuid const& guid : gos)
            {
                GameObject* go = botAI->GetGameObject(guid);
                if (!go || go->GetEntry() != goEntry)
                    continue;
    
                float distance = bot->GetDistance(go);
                if (distance > INTERACTION_DISTANCE - 2.0f)
                {
                    std::ostringstream msg;
                    msg << "Quest [" << questId << "] objective #" << objectiveIdx
                        << ": found GameObject [" << go->GetNameForLocaleIdx(sWorld->GetDefaultDbcLocale()) << "]"
                        << " (Entry: " << goEntry << ")"
                        << " but it's too far to interact directly: " << round(distance) << " yards";
                    botAI->TellMaster(msg.str());
                    continue;
                }
    
                bot->SetSelection(go->GetGUID());
    
                std::string goLink = chat->FormatGameobject(go);
    
                std::ostringstream msg;
                msg << "Quest [" << questId << "] objective #" << objectiveIdx
                    << ": directly interacting with GameObject " << goLink
                    << " (Entry: " << goEntry << ")"
                    << " at distance: " << round(distance) << " yards";
    
                botAI->TellMaster(msg.str());
    
                WorldPacket emptyPacket;
                bot->GetSession()->HandleCancelMountAuraOpcode(emptyPacket);
                SetNextMovementDelay(500);
    
                Event useEvent("use", goLink);
                botAI->DoSpecificAction("use", useEvent);
    
                return true;
            }
    
            botAI->TellMaster("Quest [" + std::to_string(questId) + "] objective #" + std::to_string(objectiveIdx) +
                              ": could not find target GameObject (Entry: " + std::to_string(goEntry) + ") nearby to interact directly.");
        }
    
        botAI->TellMaster("Also checking for direct interaction with friendly NPCs for Quest [" + std::to_string(questId) + "].");
        
        for (int32 objectiveIdx = 0; objectiveIdx < QUEST_OBJECTIVES_COUNT; ++objectiveIdx)
        {
            int32 npcOrGo = quest->RequiredNpcOrGo[objectiveIdx];
            if (!npcOrGo || npcOrGo < 0) // Only NPCs here
                continue;
        
            uint32 creatureEntry = uint32(npcOrGo);
            GuidVector npcs = AI_VALUE(GuidVector, "nearest npcs");
        
            for (ObjectGuid const& guid : npcs)
            {
                Unit* unit = botAI->GetUnit(guid);
                if (!unit || unit->GetEntry() != creatureEntry || !unit->IsAlive())
                    continue;
        
                float distance = bot->GetDistance(unit);
                if (distance > INTERACTION_DISTANCE - 2.0f)
                {
                    std::ostringstream msg;
                    msg << "Quest [" << questId << "] objective #" << objectiveIdx
                        << ": found NPC [" << unit->GetName() << "]"
                        << " (Entry: " << creatureEntry << ")"
                        << " but it's too far to interact directly: " << round(distance) << " yards";
                    botAI->TellMaster(msg.str());
                    continue;
                }
        
                bot->SetSelection(unit->GetGUID());
        
                std::ostringstream msg;
                msg << "Quest [" << questId << "] objective #" << objectiveIdx
                    << ": directly interacting with friendly NPC [" << unit->GetName() << "]"
                    << " (Entry: " << creatureEntry << ")"
                    << " at distance: " << round(distance) << " yards";
                
                WorldPacket emptyPacket;
                bot->GetSession()->HandleCancelMountAuraOpcode(emptyPacket);
                SetNextMovementDelay(500);
                
                botAI->TellMaster(msg.str());
                if (unit->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP))
                {
                    botAI->DoSpecificAction("talk");
                }
                else
                {
                    botAI->DoSpecificAction("use");
                }
                return true;
            }
        
            botAI->TellMaster("Quest [" + std::to_string(questId) + "] objective #" + std::to_string(objectiveIdx) +
                              ": could not find friendly NPC (Entry: " + std::to_string(creatureEntry) +
                              ") nearby to interact directly.");
        }
    }
    else
    {
        Item* item = bot->GetItemByEntry(startItemId);
        if (!item)
        {
            botAI->TellMaster("Quest [" + std::to_string(questId) + "] requires StartItem [" + std::to_string(startItemId) +
                              "], but it is not in my inventory. Continuing without it.");
        }
        else
        {
            ItemTemplate const* proto = item->GetTemplate();
            std::string itemLink = chat->FormatItem(proto);
    
            // === Check if item spell requires a SpellFocus and validate it's nearby ===
            for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                if (!proto->Spells[i].SpellId || proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
                    continue;
        
                SpellInfo const* spell = sSpellMgr->GetSpellInfo(proto->Spells[i].SpellId);
                if (!spell)
                    continue;
        
                uint32 focusId = spell->RequiresSpellFocus;
                if (!focusId)
                    continue;
        
                // Now we need to be near a GameObject with type 8 and Data0 == focusId
                GuidVector nearbyGOs = AI_VALUE(GuidVector, "nearest game objects no los");
                bool nearFocus = false;
        
                for (ObjectGuid const& guid : nearbyGOs)
                {
                    GameObject* go = botAI->GetGameObject(guid);
                    if (!go)
                        continue;
        
                    GameObjectTemplate const* goInfo = go->GetGOInfo();
                    if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_SPELL_FOCUS)
                        continue;
        
                    if (goInfo->spellFocus.focusId != focusId)
                        continue;
        
                    float distance = bot->GetDistance(go);
                    if (distance > INTERACTION_DISTANCE - 2.0f)
                        continue;
        
                    nearFocus = true;
        
                    std::ostringstream msg;
                    msg << "Using " << itemLink << " near required Spell Focus [" << go->GetNameForLocaleIdx(sWorld->GetDefaultDbcLocale()) << "]"
                        << " (Entry: " << go->GetEntry() << ", FocusId: " << focusId << ") at " << round(distance) << " yards.";
                    botAI->TellMaster(msg.str());

                    WorldPacket emptyPacket;
                    bot->GetSession()->HandleCancelMountAuraOpcode(emptyPacket);
                    SetNextMovementDelay(500);

                    Event useEvent("use", itemLink);
                    botAI->DoSpecificAction("use", useEvent);
                    return true;
                }
        
                if (!nearFocus)
                {
                    botAI->TellMaster("Item " + itemLink + " requires Spell Focus ID [" + std::to_string(focusId) + "], but none found nearby.");
                }
            }

            // Loop through all possible objectives (up to 4)
            for (int32 objectiveIdx = 0; objectiveIdx < QUEST_OBJECTIVES_COUNT; ++objectiveIdx)
            {
                int32 npcOrGo = quest->RequiredNpcOrGo[objectiveIdx];
                if (!npcOrGo)
                    continue;
    
                // === Use on required GameObject ===
                if (npcOrGo < 0)
                {
                    uint32 goEntry = uint32(-npcOrGo);
                    GuidVector gos = AI_VALUE(GuidVector, "nearest game objects no los");
    
                    for (ObjectGuid const& guid : gos)
                    {
                        GameObject* go = botAI->GetGameObject(guid);
                        if (!go || go->GetEntry() != goEntry)
                            continue;
    
                        bot->SetSelection(go->GetGUID());
    
                        std::ostringstream msg;
                        msg << "Quest [" << questId << "] objective #" << objectiveIdx
                            << ": using " << itemLink
                            << " on GameObject [" << go->GetNameForLocaleIdx(sWorld->GetDefaultDbcLocale()) << "]"
                            << " (Entry: " << goEntry << ")"
                            << " at distance: " << round(bot->GetDistance(go)) << " yards";
    
                        botAI->TellMaster(msg.str());

                        WorldPacket emptyPacket;
                        bot->GetSession()->HandleCancelMountAuraOpcode(emptyPacket);
                        SetNextMovementDelay(500);
    
                        Event useEvent("use", itemLink);
                        botAI->DoSpecificAction("use", useEvent);
                        return true;
                    }
    
                    botAI->TellMaster("Quest [" + std::to_string(questId) + "] objective #" + std::to_string(objectiveIdx) +
                                      ": could not find target GameObject (Entry: " + std::to_string(goEntry) +
                                      ") nearby to use " + itemLink);
                }
    
                // === Use on required NPC ===
                else if (npcOrGo > 0)
                {
                    uint32 creatureEntry = uint32(npcOrGo);
                    GuidVector units;
                
                    if (botAI && botAI->GetAiObjectContext())
                    {
                        GuidVector friendly = AI_VALUE(GuidVector, "nearest npcs");
                        if (!friendly.empty())
                            units.insert(units.end(), friendly.begin(), friendly.end());

                        GuidVector hostile = AI_VALUE(GuidVector, "nearest hostile npcs");
                        if (!hostile.empty())
                            units.insert(units.end(), hostile.begin(), hostile.end());
                    }
                
                    botAI->TellMaster("Scanning nearby friendly and hostile NPCs for Entry: " + std::to_string(creatureEntry));
                
                    for (ObjectGuid const& guid : units)
                    {
                        Unit* unit = botAI->GetUnit(guid);
                        if (unit)
                        {
                            float dist = round(bot->GetDistance(unit));
                            botAI->TellMaster(" - Found [" + unit->GetName() + "] (Entry: " + std::to_string(unit->GetEntry()) +
                                              ", Dist: " + std::to_string(dist) + " yards)");
                        }
                
                        if (!unit || unit->GetEntry() != creatureEntry || !unit->IsAlive())
                            continue;
                
                        bot->SetSelection(unit->GetGUID());
                
                        std::ostringstream msg;
                        msg << "Quest [" << questId << "] objective #" << objectiveIdx
                            << ": using " << itemLink
                            << " on NPC [" << unit->GetName() << "]"
                            << " (Entry: " << creatureEntry << ")"
                            << " at distance: " << round(bot->GetDistance(unit)) << " yards";
                
                        botAI->TellMaster(msg.str());

                        WorldPacket emptyPacket;
                        bot->GetSession()->HandleCancelMountAuraOpcode(emptyPacket);
                        SetNextMovementDelay(500);
                
                        Event useEvent("use", itemLink);
                        botAI->DoSpecificAction("use", useEvent);
                        return true;
                    }
                
                    botAI->TellMaster("Quest [" + std::to_string(questId) + "] objective #" + std::to_string(objectiveIdx) +
                                      ": could not find target NPC (Entry: " + std::to_string(creatureEntry) +
                                      ") nearby to use " + itemLink);
                }

            }
        }
    }

    if (!botAI->rpgInfo.do_quest.lastReachPOI)
    {
        botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();
        return true;
    }
    // stayed at this POI for more than 5 minutes
    if (GetMSTimeDiffToNow(botAI->rpgInfo.do_quest.lastReachPOI) >= poiStayTime)
    {
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
            /// @TODO: It may be better to make lowPriorityQuest a global set shared by all bots (or saved in db)
            botAI->lowPriorityQuest.insert(questId);
            botAI->rpgStatistic.questAbandoned++;
            LOG_DEBUG("playerbots", "[New rpg] {} marked as abandoned quest {}", bot->GetName(), questId);
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        // clear and select another poi later
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
        botAI->rpgInfo.do_quest.pos = WorldPosition();
        botAI->rpgInfo.do_quest.objectiveIdx = 0;
        return true;
    }

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
        // z = MAX_HEIGHT as we do not know accurate z
        float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(dx, dy));

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
        LOG_DEBUG("playerbots", "[New rpg] {} marked as abandoned quest {}", bot->GetName(), questId);
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }
    return false;
}

// Return a map of [ItemId -> how many more the bot needs]
std::map<uint32, int32> GetMissingQuestItems(Player* bot, Quest const* quest)
{
    std::map<uint32, int32> missingItems;

    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
    {
        uint32 requiredItemId = quest->RequiredItemId[i];
        if (!requiredItemId)
            continue; // no item objective in this slot

        uint32 requiredCount = quest->RequiredItemCount[i];
        if (!requiredCount)
            continue;

        // How many does the bot already have in inventory?
        // "item count" is a typical playerbots named value or you can do your own 
        // inventory count logic directly. This is just an example:
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        uint32 currentCount = botAI->GetAiObjectContext()->GetValue<uint32>("item count", std::to_string(requiredItemId))->Get();
        if (currentCount < requiredCount)
        {
            int32 shortfall = int32(requiredCount) - int32(currentCount);
            missingItems[requiredItemId] += shortfall;
        }
    }

    return missingItems;
}

bool TryUseQuestProductionItems(
    Player* bot,
    PlayerbotAI* botAI,
    Quest const* quest,
    std::map<uint32,int32> const& missingQuestItems,
    std::vector<Item*> const& playercastItems)
{
    // We'll iterate over each missing quest item & see if we can produce it
    // by using one of the player's "playercast" items.

    for (auto const& kv : missingQuestItems)
    {
        uint32 neededItemId = kv.first;  // e.g. 11949
        int32 neededCount   = kv.second; // e.g. 6 still needed
        if (neededCount <= 0)
            continue; // not actually missing

        // Now see if any "playercast" item can CREATE that needed item.
        for (Item* invItem : playercastItems)
        {
            if (!invItem)
                continue;

            ItemTemplate const* proto = invItem->GetTemplate();
            if (!proto)
                continue;

            // Check each possible item spell
            for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                uint32 spellId = proto->Spells[i].SpellId;
                if (!spellId)
                    continue;

                // Must be an On-Use trigger
                if (proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
                    continue;

                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                if (!spellInfo)
                    continue;

                // Does this spell create the item we need?
                // Typically we look at each effect, and if it's CREATE_ITEM => that item is our needed item
                bool createsOurItem = false;
                for (uint8 effIndex = 0; effIndex < MAX_SPELL_EFFECTS; ++effIndex)
                {
                    if (spellInfo->Effects[effIndex].Effect == SPELL_EFFECT_CREATE_ITEM ||
                        spellInfo->Effects[effIndex].Effect == SPELL_EFFECT_CREATE_ITEM_2)
                    {
                        if (spellInfo->Effects[effIndex].ItemType == neededItemId)
                        {
                            createsOurItem = true;
                            break;
                        }
                    }
                }

                if (!createsOurItem)
                    continue; // next spell

                // If we get here, "invItem" has a spell that can produce "neededItemId".
                // Next step: see if that spell has conditions (e.g. must target a specific dead creature).
                ConditionList const& conditions =
                    sConditionMgr->GetConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_SPELL, spellId);

                // We'll search for a valid target that meets the conditions
                if (conditions.empty())
                {
                    // If there are no conditions, maybe it’s an item we can just cast on self or no-target.
                    // Usually "fill jar" spells DO have conditions, but let's handle the “no condition” case:
                    // Try to simply use it on self. If your “use” action requires a selection, that’s your call:
                    // We'll do it the same way you do in "use" action.
                    bot->SetSelection(bot->GetGUID());
                    botAI->TellMaster("Using " + ChatHelper::FormatItem(proto) + " (no conditions needed).");

                    // Cancel mount, etc.
                    WorldPacket emptyPacket;
                    bot->GetSession()->HandleCancelMountAuraOpcode(emptyPacket);

                    SetNextMovementDelay(500);
                    Event useEvent("use", ChatHelper::FormatItem(proto));
                    botAI->DoSpecificAction("use", useEvent);

                    return true;
                }
                else
                {
                    // Usually we want to find a local creature that meets the conditions (e.g. "must be Tainted Ooze, must be dead")
                    // We'll gather nearest corpses, nearest hostile NPCs, etc.
                    GuidVector nearNpcs      = AI_VALUE(GuidVector, "nearest npcs");
                    GuidVector nearHostiles  = AI_VALUE(GuidVector, "nearest hostile npcs");
                    GuidVector nearCorpses   = AI_VALUE(GuidVector, "nearest corpses"); 
                    // (If you don't have "nearest corpses," you can either code your own or merge hostiles that are dead, etc.)

                    // Combine them all into one container
                    GuidVector allUnits;
                    allUnits.insert(allUnits.end(), nearNpcs.begin(), nearNpcs.end());
                    allUnits.insert(allUnits.end(), nearHostiles.begin(), nearHostiles.end());
                    allUnits.insert(allUnits.end(), nearCorpses.begin(), nearCorpses.end());

                    // Attempt usage on each candidate
                    for (ObjectGuid const& guid : allUnits)
                    {
                        Unit* unit = botAI->GetUnit(guid);
                        if (!unit)
                            continue;

                        // Check distance
                        float dist = bot->GetDistance(unit);
                        if (dist > INTERACTION_DISTANCE - 1.5f)
                            continue;

                        // Construct ConditionSourceInfo => [0]=bot(caster), [1]=this unit(target)
                        ConditionSourceInfo cinfo(bot, unit);

                        // Evaluate conditions. If they pass, we can use this item on that target
                        if (!sConditionMgr->IsObjectMeetToConditions(cinfo, conditions))
                            continue;

                        // It's valid. Let's do it.
                        bot->SetSelection(unit->GetGUID());

                        std::ostringstream msg;
                        msg << "Using " << ChatHelper::FormatItem(proto)
                            << " to create needed item [" << neededItemId << "]"
                            << " on " << unit->GetName()
                            << " (SpellId=" << spellId << ", Dist=" << dist << ")";
                        botAI->TellMaster(msg.str());

                        // Cancel mount if any
                        WorldPacket emptyPacket;
                        bot->GetSession()->HandleCancelMountAuraOpcode(emptyPacket);

                        SetNextMovementDelay(500);

                        Event useEvent("use", ChatHelper::FormatItem(proto));
                        botAI->DoSpecificAction("use", useEvent);

                        return true; // done for now
                    }
                }

                // If we tried all nearest units but no valid target was found, we skip to the next item
            } // end for each item spell
        } // end for each playercast item
    } // end for each missing quest item

    // If we never found an item+target combination, return false
    return false;
}
