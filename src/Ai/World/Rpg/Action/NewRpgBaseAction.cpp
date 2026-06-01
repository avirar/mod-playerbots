#include "NewRpgBaseAction.h"
#include "PossibleRpgTargetsValue.h"

#include "BroadcastHelper.h"
#include "ChatHelper.h"
#include "Creature.h"
#include "G3D/Vector2.h"
#include "GameObject.h"
#include "GossipDef.h"
#include "GameGraveyard.h"
#include "GridTerrainData.h"
#include "IVMapMgr.h"
#include "ItemUsageValue.h"
#include "LootMgr.h"
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
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Position.h"
#include "QuestDef.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"
#include "StatsWeightCalculator.h"
#include "Timer.h"
#include "TravelMgr.h"

bool NewRpgBaseAction::MoveFarTo(WorldPosition dest)
{
    if (dest == WorldPosition())
        return false;

    if (dest != botAI->rpgInfo.moveFarPos)
    {
        // clear stuck information if it's a new dest
        botAI->rpgInfo.SetMoveFarTo(dest);
    }

    // performance optimization
    if (IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL))
    {
        return false;
    }

    // stuck check
    float disToDest = bot->GetDistance(dest);
    if (disToDest + 1.0f < botAI->rpgInfo.nearestMoveFarDis)
    {
        botAI->rpgInfo.nearestMoveFarDis = disToDest;
        botAI->rpgInfo.stuckTs = getMSTime();
        botAI->rpgInfo.stuckAttempts = 0;
    }
    else if (++botAI->rpgInfo.stuckAttempts >= 10 && GetMSTimeDiffToNow(botAI->rpgInfo.stuckTs) >= stuckTime)
    {
        // Unfortunately we've been stuck here for over 5 mins, fallback to teleporting directly to the destination
        botAI->rpgInfo.stuckTs = getMSTime();
        botAI->rpgInfo.stuckAttempts = 0;
        const AreaTableEntry* entry = sAreaTableStore.LookupEntry(bot->GetZoneId());
        std::string zone_name = PlayerbotAI::GetLocalizedAreaName(entry);
        if (botAI->HasStrategy("debug", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG(
                "playerbots",
                "[New RPG] Teleport {} from ({},{},{},{}) to ({},{},{},{}) as it stuck when moving far - Zone: {} ({})",
                bot->GetName(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(),
                dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), dest.GetMapId(), bot->GetZoneId(),
                zone_name);
        }
        return bot->TeleportTo(dest);
    }

    float dis = bot->GetExactDist(dest);
    if (dis < pathFinderDis)
    {
        return MoveTo(dest.GetMapId(), dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), false, false,
                      false, true);
    }

    float minDelta = M_PI;
    const float x = bot->GetPositionX();
    const float y = bot->GetPositionY();
    const float z = bot->GetPositionZ();
    float rx, ry, rz;
    bool found = false;
    int attempt = 3;
    while (attempt--)
    {
        float angle = bot->GetAngle(&dest);
        float delta = urand(1, 100) <= 75 ? (rand_norm() - 0.5) * M_PI * 0.5 : (rand_norm() - 0.5) * M_PI * 2;
        angle += delta;
        float dis = rand_norm() * pathFinderDis;
        float dx = x + cos(angle) * dis;
        float dy = y + sin(angle) * dis;
        float dz = z + 0.5f;
        PathGenerator path(bot);
        path.CalculatePath(dx, dy, dz);
        PathType type = path.GetPathType();
        uint32 typeOk = PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_FARFROMPOLY;
        bool canReach = !(type & (~typeOk));

        if (canReach && fabs(delta) <= minDelta)
        {
            found = true;
            const G3D::Vector3& endPos = path.GetActualEndPosition();
            rx = endPos.x;
            ry = endPos.y;
            rz = endPos.z;
            minDelta = fabs(delta);
        }
    }
    if (found)
    {
        return MoveTo(bot->GetMapId(), rx, ry, rz, false, false, false, true);
    }
    return false;
}

bool NewRpgBaseAction::MoveWorldObjectTo(ObjectGuid guid, float distance)
{
    if (IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL))
    {
        return false;
    }

    WorldObject* object = botAI->GetWorldObject(guid);
    if (!object)
        return false;
        
    // Simple upstream approach - no complex LOS handling
        
    float objectX = object->GetPositionX();
    float objectY = object->GetPositionY();
    float objectZ = object->GetPositionZ();
    float mapId = object->GetMapId();
    float angle = 0.f;

    if (!object->ToUnit() || !object->ToUnit()->isMoving())
        angle = object->GetAngle(bot) + (M_PI * irand(-25, 25) / 100.0);
    else
        angle = object->GetOrientation() + (M_PI * irand(-25, 25) / 100.0);

    float rnd = rand_norm();
    float x = objectX + cos(angle) * distance * rnd;
    float y = objectY + sin(angle) * distance * rnd;
    float z = objectZ;

    // Use upstream's simple collision check
    if (!object->GetMap()->CheckCollisionAndGetValidCoords(object, objectX, objectY, objectZ, x, y, z))
    {
        x = objectX;
        y = objectY;
        z = objectZ;
    }
    
    return MoveTo(mapId, x, y, z, false, false, false, true);
}

bool NewRpgBaseAction::MoveRandomNear(float moveStep, MovementPriority priority, WorldObject* center)
{
    if (IsWaitingForLastMove(priority))
    {
        return false;
    }

    float distance = rand_norm() * moveStep;
    Map* map = bot->GetMap();
    const float x = bot->GetPositionX();
    const float y = bot->GetPositionY();
    const float z = bot->GetPositionZ();
    int attempts = 10; // Increase attempts for POI boundary checking
    while (attempts--)
    {
        float angle = (float)rand_norm() * 2 * static_cast<float>(M_PI);
        float dx = x + distance * cos(angle);
        float dy = y + distance * sin(angle);
        float dz = z;

        // Check POI boundary with 40.0f tolerance
        if (!IsWithinPOIBoundary(dx, dy, 40.0f))
            continue;

        PathGenerator path(bot);
        path.CalculatePath(dx, dy, dz);
        PathType type = path.GetPathType();
        uint32 typeOk = PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_FARFROMPOLY;
        bool canReach = !(type & (~typeOk));

        if (!canReach)
            continue;

        if (!map->CanReachPositionAndGetValidCoords(bot, dx, dy, dz))
            continue;

        // if (map->IsInWater(bot->GetPhaseMask(), dx, dy, dz, bot->GetCollisionHeight()))
            // continue;

        bool moved = MoveTo(bot->GetMapId(), dx, dy, dz, false, false, false, true, priority);
        if (moved)
            return true;
    }

    return false;
}

bool NewRpgBaseAction::ForceToWait(uint32 duration, MovementPriority priority)
{
    AI_VALUE(LastMovement&, "last movement")
        .Set(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetOrientation(),
             duration, priority);
    return true;
}

// Safe quest status retrieval - prevents crashes from .at() on missing quests
QuestStatusData const* NewRpgBaseAction::GetSafeQuestStatus(uint32 questId)
{
    auto questStatusMap = bot->getQuestStatusMap();
    auto statusIt = questStatusMap.find(questId);
    if (statusIt == questStatusMap.end())
        return nullptr;
    return &statusIt->second;
}

/// @TODO: Fix redundant code
/// Quest related method refer to TalkToQuestGiverAction.h
bool NewRpgBaseAction::InteractWithNpcOrGameObjectForQuest(ObjectGuid guid)
{
    if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} InteractWithNpcOrGameObjectForQuest called with GUID {}", 
             bot->GetName(), guid.ToString());
    }
             
    WorldObject* object = ObjectAccessor::GetWorldObject(*bot, guid);
    if (!object)
    {
        if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} Object with GUID {} not found", bot->GetName(), guid.ToString());
        }
        return false;
    }
    
    if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} Found object: {}", bot->GetName(), object->GetName());
    }
        
    // Final LOS check before interaction - only fail if we're close enough to interact
    float distance = bot->GetDistance(object);
    /*
    if (distance <= INTERACTION_DISTANCE && !bot->IsWithinLOSInMap(object))
    {
    if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} Cannot interact with NPC/GO {} - no LOS at interaction distance", 
    }
    }
    }
                 bot->GetName(), guid.ToString());
        return false;
    }
    */
    // Handle GameObject quest objectives that need to be used directly
    if (GameObject* go = object->ToGameObject())
    {
        if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} InteractWithNpcOrGameObjectForQuest: Processing GameObject {} (type {})", 
                     bot->GetName(), go->GetGOInfo()->name, go->GetGoType());
        }
        
        // Check if this GameObject is a quest objective that should be used directly
        if (go->GetGoType() != GAMEOBJECT_TYPE_QUESTGIVER)
        {
            if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} GameObject is not a quest giver, checking for quest objectives", 
                         bot->GetName());
            }
            
            // Check if this GameObject is required for any active quest
            QuestStatusMap& questMap = bot->getQuestStatusMap();
            for (auto& questPair : questMap)
            {
                const Quest* quest = sObjectMgr->GetQuestTemplate(questPair.first);
                if (!quest || questPair.second.Status != QUEST_STATUS_INCOMPLETE)
                    continue;
                    
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Checking quest {} for GameObject requirements", 
                             bot->GetName(), questPair.first);
                }
                    
                // Check if this GameObject is a quest objective
                for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                {
                    int32 requiredNpcOrGo = quest->RequiredNpcOrGo[i];
                    if (requiredNpcOrGo < 0 && (-requiredNpcOrGo) == (int32)go->GetEntry())
                    {
                        // Check if we still need this objective
                        uint32 currentCount = questPair.second.CreatureOrGOCount[i];
                        uint32 requiredCount = quest->RequiredNpcOrGoCount[i];
                        
                        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                        {
                            LOG_DEBUG("playerbots", "[New RPG] {} Quest {} objective {}: current {} / required {}", 
                                     bot->GetName(), questPair.first, i, currentCount, requiredCount);
                        }
                        
                        if (currentCount < requiredCount)
                        {
                            // CHECK LOCK REQUIREMENTS BEFORE USING GAMEOBJECT
                            if (go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
                            {
                                uint32 reqItem, skillId, reqSkillValue;
                                if (!CheckGameObjectLockRequirements(go, reqItem, skillId, reqSkillValue) && reqItem > 0)
                                {
                                    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                                    {
                                        ItemTemplate const* keyProto = sObjectMgr->GetItemTemplate(reqItem);
                                        LOG_DEBUG("playerbots", "[New RPG] {} GameObject {} requires key item {} ({}), cannot interact yet", 
                                                 bot->GetName(), go->GetGOInfo()->name, reqItem, 
                                                 keyProto ? keyProto->Name1 : "Unknown");
                                    }
                                    
                                    // Cannot use this GameObject yet - need key item first
                                    continue;
                                }
                            }
                            
                            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                            {
                                LOG_DEBUG("playerbots", "[New RPG] {} Using GameObject {} for quest {} objective {}", 
                                         bot->GetName(), go->GetGOInfo()->name, questPair.first, i);
                            }

                            if (bot->isMoving())
                            {
                                bot->StopMoving();
                                botAI->SetNextCheckDelay(sPlayerbotAIConfig.globalCoolDown);
                                return false;
                            }

                            if (bot->IsMounted())
                            {
                                bot->Dismount();
                                botAI->SetNextCheckDelay(sPlayerbotAIConfig.globalCoolDown);
                            }

                            // Use proper packet-based GameObject interaction
                            WorldPacket packet(CMSG_GAMEOBJ_USE);
                            packet << go->GetGUID();
                            bot->GetSession()->HandleGameObjectUseOpcode(packet);
                            return true;
                        }
                        else
                        {
                            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                            {
                                LOG_DEBUG("playerbots", "[New RPG] {} GameObject {} objective already complete for quest {}", 
                                         bot->GetName(), go->GetGOInfo()->name, questPair.first);
                            }
                        }
                    }
                }
            }
            
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} GameObject {} not required for any active quest", 
                         bot->GetName(), go->GetGOInfo()->name);
            }
        }
    }

    // Handle quest objective NPCs that need gossip interaction FIRST
    // (before checking if they're regular quest givers)
    Creature* creature = object->ToCreature();
    if (creature)
    {
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} Checking creature {} for quest objective interaction", 
                     bot->GetName(), creature->GetName());
        }
                 
        if (IsRequiredQuestObjectiveNPC(creature))
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Initiating gossip with quest objective NPC {}", 
                          bot->GetName(), creature->GetName());
            }

            if (bot->isMoving())
            {
                bot->StopMoving();
                botAI->SetNextCheckDelay(sPlayerbotAIConfig.globalCoolDown);
                return false;
            }

            if (bot->IsMounted())
            {
                bot->Dismount();
                botAI->SetNextCheckDelay(sPlayerbotAIConfig.globalCoolDown);
            }

            // Set target and use existing gossip hello action
            // Add safety check for creature validity
            if (!creature || !creature->IsInWorld())
            {
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Creature {} is invalid or not in world", 
                             bot->GetName(), creature->GetName());
                }
                return false;
            }
            
            bot->SetSelection(creature->GetGUID());
            
            bool actionResult = botAI->DoSpecificAction("gossip hello", Event("gossip hello", creature->GetGUID()));
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} DoSpecificAction('gossip hello') result: {}", 
                         bot->GetName(), actionResult ? "SUCCESS" : "FAILED");
            }
            return true;
        }
        else
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Creature {} is not a required quest objective NPC", 
                         bot->GetName(), creature->GetName());
            }
        }
    }

    // Handle regular quest giver interaction
    if (!bot->CanInteractWithQuestGiver(object))
    {
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} Object {} is not a regular quest giver", 
                     bot->GetName(), object->GetName());
        }
        return false;
    }

    // Creature* creature = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
    // if (creature)
    // {
    //     WorldPacket packet(CMSG_GOSSIP_HELLO);
    //     packet << guid;
    //     bot->GetSession()->HandleGossipHelloOpcode(packet);
    // }

    bot->PrepareQuestMenu(guid);
    const QuestMenu& menu = bot->PlayerTalkClass->GetQuestMenu();
    if (menu.Empty())
        return true;

    for (uint8 idx = 0; idx < menu.GetMenuItemCount(); idx++)
    {
        const QuestMenuItem& item = menu.GetItem(idx);
        const Quest* quest = sObjectMgr->GetQuestTemplate(item.QuestId);
        if (!quest)
            continue;

        const QuestStatus& status = bot->GetQuestStatus(item.QuestId);
        if (status == QUEST_STATUS_NONE && bot->CanTakeQuest(quest, false) && bot->CanAddQuest(quest, false) &&
            IsQuestWorthDoing(quest) && IsQuestCapableDoing(quest))
        {
            AcceptQuest(quest, guid);
            if (botAI->GetMaster() && botAI->HasStrategy("debug", BOT_STATE_NON_COMBAT))
                botAI->TellMaster("Quest accepted " + ChatHelper::FormatQuest(quest));
            BroadcastHelper::BroadcastQuestAccepted(botAI, bot, quest);
            botAI->rpgStatistic.questAccepted++;
            botAI->rpgStatistic.questAcceptedByID[quest->GetQuestId()]++;
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} accept quest {}", bot->GetName(), quest->GetQuestId());
            }
        }
        if (status == QUEST_STATUS_COMPLETE && bot->CanRewardQuest(quest, 0, false))
        {
            TurnInQuest(quest, guid);
            if (botAI->GetMaster() && botAI->HasStrategy("debug", BOT_STATE_NON_COMBAT))
                botAI->TellMaster("Quest rewarded " + ChatHelper::FormatQuest(quest));
            BroadcastHelper::BroadcastQuestTurnedIn(botAI, bot, quest);
            botAI->rpgStatistic.questRewarded++;
            botAI->rpgStatistic.questRewardedByID[quest->GetQuestId()]++;
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} turned in quest {}", bot->GetName(), quest->GetQuestId());
            }
        }
    }

    return true;
}

bool NewRpgBaseAction::CanInteractWithQuestGiver(Object* questGiver)
{
    // This is a variant of Player::CanInteractWithQuestGiver
    // that removes the distance check and keeps all other checks
    switch (questGiver->GetTypeId())
    {
        case TYPEID_UNIT:
        {
            ObjectGuid guid = questGiver->GetGUID();
            uint32 npcflagmask = UNIT_NPC_FLAG_QUESTGIVER;
            // unit checks
            if (!guid)
                return false;

            if (!bot->IsInWorld())
                return false;

            if (bot->IsInFlight())
                return false;

            // exist (we need look pets also for some interaction (quest/etc)
            Creature* creature = ObjectAccessor::GetCreatureOrPetOrVehicle(*bot, guid);
            if (!creature)
                return false;

            // Deathstate checks
            if (!bot->IsAlive() &&
                !(creature->GetCreatureTemplate()->type_flags & CREATURE_TYPE_FLAG_VISIBLE_TO_GHOSTS))
                return false;

            // alive or spirit healer
            if (!creature->IsAlive() &&
                !(creature->GetCreatureTemplate()->type_flags & CREATURE_TYPE_FLAG_INTERACT_WHILE_DEAD))
                return false;

            // appropriate npc type
            if (npcflagmask && !creature->HasNpcFlag(NPCFlags(npcflagmask)))
                return false;

            // not allow interaction under control, but allow with own pets
            if (creature->GetCharmerGUID())
                return false;

            // xinef: perform better check
            if (creature->GetReactionTo(bot) <= REP_UNFRIENDLY)
                return false;

            // pussywizard: many npcs have missing conditions for class training and rogue trainer can for eg. train
            // dual wield to a shaman :/ too many to change in sql and watch in the future pussywizard: this function is
            // not used when talking, but when already taking action (buy spell, reset talents, show spell list)
            if (npcflagmask & (UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_TRAINER_CLASS) &&
                sObjectMgr->GetTrainer(creature->GetEntry()) &&
                sObjectMgr->GetTrainer(creature->GetEntry())->GetTrainerType() == Trainer::Type::Class &&
                !bot->IsClass((Classes)sObjectMgr->GetTrainer(creature->GetEntry())->GetTrainerRequirement(), CLASS_CONTEXT_CLASS_TRAINER))
                return false;

            return true;
        }
        case TYPEID_GAMEOBJECT:
        {
            ObjectGuid guid = questGiver->GetGUID();
            GameobjectTypes type = GAMEOBJECT_TYPE_QUESTGIVER;
            if (GameObject* go = bot->GetMap()->GetGameObject(guid))
            {
                if (go->GetGoType() == type)
                {
                    // Players cannot interact with gameobjects that use the "Point" icon
                    if (go->GetGOInfo()->IconName == "Point")
                    {
                        return false;
                    }

                    return true;
                }
            }
            return false;
        }
        // unused for now
        // case TYPEID_PLAYER:
        //     return bot->IsAlive() && questGiver->ToPlayer()->IsAlive();
        // case TYPEID_ITEM:
        //     return bot->IsAlive();
        default:
            break;
    }
    return false;
}

bool NewRpgBaseAction::IsWithinInteractionDist(Object* questGiver)
{
    // This is a variant of Player::CanInteractWithQuestGiver
    // that only keep the distance check
    switch (questGiver->GetTypeId())
    {
        case TYPEID_UNIT:
        {
            ObjectGuid guid = questGiver->GetGUID();
            // unit checks
            if (!guid)
                return false;

            // exist (we need look pets also for some interaction (quest/etc)
            Creature* creature = ObjectAccessor::GetCreatureOrPetOrVehicle(*bot, guid);
            if (!creature)
                return false;

            if (!creature->IsWithinDistInMap(bot, INTERACTION_DISTANCE))
                return false;

            return true;
        }
        case TYPEID_GAMEOBJECT:
        {
            ObjectGuid guid = questGiver->GetGUID();
            GameobjectTypes type = GAMEOBJECT_TYPE_QUESTGIVER;
            if (GameObject* go = bot->GetMap()->GetGameObject(guid))
            {
                if (go->IsWithinDistInMap(bot))
                {
                    return true;
                }
            }
            return false;
        }
        // case TYPEID_PLAYER:
        //     return bot->IsAlive() && questGiver->ToPlayer()->IsAlive();
        // case TYPEID_ITEM:
        //     return bot->IsAlive();
        default:
            break;
    }
    return false;
}

bool NewRpgBaseAction::AcceptQuest(Quest const* quest, ObjectGuid guid)
{
    WorldPacket p(CMSG_QUESTGIVER_ACCEPT_QUEST);
    uint32 unk1 = 0;
    p << guid << quest->GetQuestId() << unk1;
    p.rpos(0);
    bot->GetSession()->HandleQuestgiverAcceptQuestOpcode(p);

    return true;
}

bool NewRpgBaseAction::TurnInQuest(Quest const* quest, ObjectGuid guid)
{
    uint32 questID = quest->GetQuestId();

    if (bot->GetQuestRewardStatus(questID))
    {
        return false;
    }

    if (!bot->CanRewardQuest(quest, false))
    {
        return false;
    }

    bot->PlayDistanceSound(621);

    WorldPacket p(CMSG_QUESTGIVER_CHOOSE_REWARD);
    p << guid << quest->GetQuestId();
    if (quest->GetRewChoiceItemsCount() <= 1)
    {
        p << 0;
        bot->GetSession()->HandleQuestgiverChooseRewardOpcode(p);
    }
    else
    {
        uint32 bestId = BestRewardIndex(quest);
        p << bestId;
        bot->GetSession()->HandleQuestgiverChooseRewardOpcode(p);
    }

    return true;
}

uint32 NewRpgBaseAction::BestRewardIndex(Quest const* quest)
{
    ItemIds returnIds;
    ItemUsage bestUsage = ITEM_USAGE_NONE;
    if (quest->GetRewChoiceItemsCount() <= 1)
        return 0;
    else
    {
        for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
        {
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", quest->RewardChoiceItemId[i]);
            if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE)
                bestUsage = ITEM_USAGE_EQUIP;
            else if (usage == ITEM_USAGE_BAD_EQUIP && bestUsage != ITEM_USAGE_EQUIP)
                bestUsage = usage;
            else if (usage != ITEM_USAGE_NONE && bestUsage == ITEM_USAGE_NONE)
                bestUsage = usage;
        }
        StatsWeightCalculator calc(bot);
        uint32 best = 0;
        float bestScore = 0;
        for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
        {
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", quest->RewardChoiceItemId[i]);
            if (usage == bestUsage || usage == ITEM_USAGE_REPLACE)
            {
                float score = calc.CalculateItem(quest->RewardChoiceItemId[i]);
                if (score > bestScore)
                {
                    bestScore = score;
                    best = i;
                }
            }
        }
        return best;
    }
}

bool NewRpgBaseAction::IsQuestWorthDoing(Quest const* quest)
{
    bool isLowLevelQuest =
        bot->GetLevel() > (bot->GetQuestLevel(quest) + sWorld->getIntConfig(CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF));

    if (isLowLevelQuest)
        return false;

    if (quest->IsRepeatable())
    {
        bool hasUsefulReward = false;
        for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
        {
            if (quest->RewardChoiceItemId[i] &&
                AI_VALUE2(ItemUsage, "item usage", quest->RewardChoiceItemId[i]) != ITEM_USAGE_NONE)
            {
                hasUsefulReward = true;
                break;
            }
        }
        if (!hasUsefulReward)
            return false;
    }

    if (quest->IsSeasonal())
        return false;

    return true;
}

bool NewRpgBaseAction::IsQuestCapableDoing(Quest const* quest)
{
    bool highLevelQuest = bot->GetLevel() + 3 < bot->GetQuestLevel(quest);
    if (highLevelQuest)
        return false;

    // Reject disabled quests (QuestType = 1)
    // QuestType 0: Auto-complete quests (skip objectives) - ACCEPT
    // QuestType 1: Disabled/not implemented quests - REJECT
    // QuestType 2: Normal enabled quests - ACCEPT
    if (quest->GetQuestMethod() == 1)
        return false;

    // Reject group quests (2+ players suggested)
    if (quest->GetSuggestedPlayers() >= 2)
        return false;

    // Reject elite quests (QuestInfoID = 1)
    // These are typically elite/boss quests like "Wanted: Hogger"
    // Note: quest->GetType() returns QuestInfoID, not QuestType
    if (quest->GetType() == 1)
        return false;

    // Reject PvP quests (QuestInfoID = 41 or requires player kills)
    // QuestInfoID 41 = battleground/PvP objectives
    if (quest->GetType() == 41 || quest->GetPlayersSlain() > 0)
        return false;

    return true;
}

bool NewRpgBaseAction::HasNeededQuestItemForSale(float distanceLimit)
{
    std::set<uint32> neededItems;

    auto const& questStatusMap = bot->getQuestStatusMap();
    for (auto const& qEntry : questStatusMap)
    {
        uint32 questId = qEntry.first;
        QuestStatusData const& qStatus = qEntry.second;

        if (qStatus.Status != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            if (quest->RequiredItemId[i] && quest->RequiredItemCount[i])
            {
                uint32 current = qStatus.ItemCount[i];
                if (current < quest->RequiredItemCount[i])
                    neededItems.insert(quest->RequiredItemId[i]);
            }
        }
    }

    if (neededItems.empty())
        return false;

    GuidVector nearbyCreatures = AI_VALUE(GuidVector, "possible new rpg targets");
    if (nearbyCreatures.empty())
        nearbyCreatures = AI_VALUE(GuidVector, "possible new rpg targets no los");

    for (ObjectGuid const& guid : nearbyCreatures)
    {
        Creature* creature = ObjectAccessor::GetCreature(*bot, guid);
        if (!creature || !creature->IsInWorld() || !creature->IsVendor())
            continue;

        if (bot->GetExactDist(creature) > distanceLimit)
            continue;

        VendorItemData const* vendorData = creature->GetVendorItems();
        if (!vendorData || vendorData->m_items.empty())
            continue;

        for (VendorItemList::const_iterator itr = vendorData->m_items.begin(); itr != vendorData->m_items.end(); ++itr)
        {
            if (neededItems.count((*itr)->item))
            {
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                {
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate((*itr)->item);
                    LOG_DEBUG("playerbots", "[New RPG] {} Found needed quest item {} ({}) for sale at vendor {} ({:.0f}yd)",
                              bot->GetName(), (*itr)->item, proto ? proto->Name1 : "Unknown",
                              creature->GetName(), bot->GetExactDist(creature));
                }
                return true;
            }
        }
    }

    return false;
}

bool NewRpgBaseAction::IsRequiredQuestObjectiveNPC(Creature* creature)
{
    if (!creature)
        return false;

    
    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} Checking if NPC {} (entry {}) is required quest objective", 
            bot->GetName(), creature->GetName(), creature->GetEntry());
    }
    
    // First, let's see all active quests
    int activeQuestCount = 0;
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (questId > 0)
        {
            QuestStatus status = bot->GetQuestStatus(questId);
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Quest slot {}: ID {}, status {}", 
                    bot->GetName(), slot, questId, (int)status);
            }
            if (status == QUEST_STATUS_INCOMPLETE)
                activeQuestCount++;
        }
    }
    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} Total incomplete quests: {}", bot->GetName(), activeQuestCount);
    }

    // Only check friendly/neutral creatures (hostile ones are handled by grind strategy)
    ReputationRank reaction = creature->GetReactionTo(bot);
    if (reaction < REP_NEUTRAL)
    {
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            LOG_DEBUG("playerbots", "[New RPG] {} NPC {} has hostile reaction {}, skipping", 
                     bot->GetName(), creature->GetName(), (int)reaction);
        return false;
    }

    uint32 creatureEntry = creature->GetEntry();
    
    // Check all active quests for this creature as a talk objective
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;
            
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                LOG_DEBUG("playerbots", "[New RPG] {} Quest {} not found in ObjectMgr", bot->GetName(), questId);
            continue;
        }
        
        QuestStatus questStatus = bot->GetQuestStatus(questId);
        if (questStatus != QUEST_STATUS_INCOMPLETE)
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                LOG_DEBUG("playerbots", "[New RPG] {} Quest {} status is {} (not incomplete)", 
                         bot->GetName(), questId, (int)questStatus);
            continue;
        }
        
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            LOG_DEBUG("playerbots", "[New RPG] {} Checking quest {} for SPEAKTO flag", bot->GetName(), questId);
        
        // Check if this quest has SPEAKTO flag or similar talk requirements
        if (!quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_SPEAKTO))
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                LOG_DEBUG("playerbots", "[New RPG] {} Quest {} does not have SPEAKTO flag", bot->GetName(), questId);
            continue;
        }
        
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            LOG_DEBUG("playerbots", "[New RPG] {} Quest {} HAS SPEAKTO flag, checking objectives", bot->GetName(), questId);
            
        // Check if this creature is a required objective
        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 requiredNpcOrGo = quest->RequiredNpcOrGo[i];
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                LOG_DEBUG("playerbots", "[New RPG] {} Quest {} objective {}: RequiredNpcOrGo = {}, checking against creature entry {}", 
                         bot->GetName(), questId, i, requiredNpcOrGo, creatureEntry);
                     
            if (requiredNpcOrGo > 0 && requiredNpcOrGo == (int32)creatureEntry)
            {
                // Check if we still need this objective
   auto questStatusMap = bot->getQuestStatusMap();
    auto statusIt = questStatusMap.find(questId);
    if (statusIt == questStatusMap.end())
        return false;

    const QuestStatusData& q_status = statusIt->second;
                uint32 currentCount = q_status.CreatureOrGOCount[i];
                uint32 requiredCount = quest->RequiredNpcOrGoCount[i];
                
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                    LOG_DEBUG("playerbots", "[New RPG] {} Quest {} objective {} match! Current count: {}, Required count: {}", 
                             bot->GetName(), questId, i, currentCount, requiredCount);
                
                if (currentCount < requiredCount)
                {
                    // Keep this as high-level info for generic debug
                    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                        LOG_DEBUG("playerbots", "[New RPG] {} NPC {} IS REQUIRED for SPEAKTO quest {} (objective {})", 
                                 bot->GetName(), creature->GetName(), questId, i);
                    return true;
                }
                else
                {
                    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                        LOG_DEBUG("playerbots", "[New RPG] {} NPC {} objective already complete for quest {} ({}/{})", 
                                 bot->GetName(), creature->GetName(), questId, currentCount, requiredCount);
                }
            }
        }
    }
    
    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
        LOG_DEBUG("playerbots", "[New RPG] {} NPC {} is NOT required for any quest objective", 
                 bot->GetName(), creature->GetName());
    return false;
}

bool NewRpgBaseAction::TryInteractWithQuestObjective(uint32 questId, int32 objectiveIdx)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest || objectiveIdx >= QUEST_OBJECTIVES_COUNT) 
    {
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            LOG_DEBUG("playerbots", "[New RPG] {} Invalid quest {} or objective index {}", 
                     bot->GetName(), questId, objectiveIdx);
        return false;
    }
    
    int32 requiredNpcOrGo = quest->RequiredNpcOrGo[objectiveIdx];
    if (requiredNpcOrGo == 0) 
    {
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            LOG_DEBUG("playerbots", "[New RPG] {} Quest {} objective {} has no required NPC or GO", 
                     bot->GetName(), questId, objectiveIdx);
        return false;
    }
    
    // Check if objective is already complete
    const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
    if (q_status.CreatureOrGOCount[objectiveIdx] >= quest->RequiredNpcOrGoCount[objectiveIdx])
    {
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            LOG_DEBUG("playerbots", "[New RPG] {} Quest {} objective {} already complete ({}/{})", 
                     bot->GetName(), questId, objectiveIdx, 
                     q_status.CreatureOrGOCount[objectiveIdx], quest->RequiredNpcOrGoCount[objectiveIdx]);
        return false;
    }
    
    // Search for the target
    WorldObject* target = nullptr;
    
    if (requiredNpcOrGo > 0) 
    {
        // Search for NPC
        uint32 targetEntry = (uint32)requiredNpcOrGo;
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            LOG_DEBUG("playerbots", "[New RPG] {} Searching for quest objective NPC entry {}", 
                     bot->GetName(), targetEntry);
                 
        GuidVector nearbyNPCs = AI_VALUE(GuidVector, "nearest npcs");
        for (const ObjectGuid& guid : nearbyNPCs) 
        {
            Creature* creature = ObjectAccessor::GetCreature(*bot, guid);
            if (creature && creature->GetEntry() == targetEntry) 
            {
                target = creature;
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                    LOG_DEBUG("playerbots", "[New RPG] {} Found quest objective NPC {} at distance {:.1f}", 
                             bot->GetName(), creature->GetName(), bot->GetDistance(creature));
                break;
            }
        }
    } 
    else 
    {
        // Search for GameObject  
        uint32 targetEntry = (uint32)(-requiredNpcOrGo);
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            LOG_DEBUG("playerbots", "[New RPG] {} Searching for quest objective GameObject entry {}", 
                     bot->GetName(), targetEntry);
                 
        GuidVector nearbyGOs = AI_VALUE(GuidVector, "nearest game objects");
        for (const ObjectGuid& guid : nearbyGOs) 
        {
            GameObject* go = ObjectAccessor::GetGameObject(*bot, guid);
            if (go && go->GetEntry() == targetEntry) 
            {
                target = go;
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                    LOG_DEBUG("playerbots", "[New RPG] {} Found quest objective GameObject {} at distance {:.1f}", 
                             bot->GetName(), go->GetGOInfo()->name, bot->GetDistance(go));
                
                // CHECK LOCK REQUIREMENTS FOR GOOBER OBJECTS
                if (go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
                {
                    uint32 reqItem, skillId, reqSkillValue;
                    bool canAccess = CheckGameObjectLockRequirements(go, reqItem, skillId, reqSkillValue);
                    
                    if (!canAccess && reqItem > 0)
                    {
                        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                        {
                            ItemTemplate const* keyProto = sObjectMgr->GetItemTemplate(reqItem);
                            LOG_DEBUG("playerbots", "[New RPG] {} GameObject {} requires key item {} before interaction", 
                                     bot->GetName(), go->GetGOInfo()->name, 
                                     keyProto ? keyProto->Name1 : "Unknown");
                        }
                        
                        // Quest objective requires a key item we don't have
                        // This should trigger getting the key item first
                        return false;
                    }
                }
                break;
            }
        }
    }
    
    if (!target) 
    {
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            LOG_DEBUG("playerbots", "[New RPG] {} Quest objective target not found for quest {} objective {}", 
                     bot->GetName(), questId, objectiveIdx);
        return false;
    }
    
    // Check if we can interact
    bool canInteract = false;
    
    if (requiredNpcOrGo > 0) 
    {
        // NPC interaction check
        Creature* creature = target->ToCreature();
        if (creature && IsRequiredQuestObjectiveNPC(creature)) 
        {
            float distance = bot->GetDistance(creature);
            canInteract = distance <= INTERACTION_DISTANCE;
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                LOG_DEBUG("playerbots", "[New RPG] {} NPC {} interaction check: distance {:.1f}, can interact: {}", 
                         bot->GetName(), creature->GetName(), distance, canInteract);
        }
    } 
    else 
    {
        // GameObject interaction check  
        GameObject* go = target->ToGameObject();
        canInteract = go && IsWithinInteractionDist(go);
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            LOG_DEBUG("playerbots", "[New RPG] {} GameObject {} interaction check: can interact: {}", 
                     bot->GetName(), go->GetGOInfo()->name, canInteract);
    }
    
    if (canInteract) 
    {
        // High-level interaction log for generic debug
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            LOG_DEBUG("playerbots", "[New RPG] {} Interacting with quest objective {}", 
                     bot->GetName(), target->GetName());
        return InteractWithNpcOrGameObjectForQuest(target->GetGUID());
    } 
    else 
    {
        // High-level movement log for generic debug  
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            LOG_DEBUG("playerbots", "[New RPG] {} Moving closer to quest objective {}", 
                     bot->GetName(), target->GetName());
        return MoveWorldObjectTo(target->GetGUID());
    }
}

bool NewRpgBaseAction::OrganizeQuestLog()
{
    int32 freeSlotNum = 0;

    for (uint16 i = 0; i < MAX_QUEST_LOG_SIZE; ++i)
    {
        uint32 questId = bot->GetQuestSlotQuestId(i);
        if (!questId)
            freeSlotNum++;
    }

    // it's ok if we have two more free slots
    if (freeSlotNum >= 2)
        return false;

    int32 dropped = 0;
    // remove quests that not worth doing or not capable of doing
    for (uint16 i = 0; i < MAX_QUEST_LOG_SIZE; ++i)
    {
        uint32 questId = bot->GetQuestSlotQuestId(i);
        if (!questId)
            continue;

        const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!IsQuestWorthDoing(quest) || !IsQuestCapableDoing(quest) ||
            bot->GetQuestStatus(questId) == QUEST_STATUS_FAILED)
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} drop quest {}", bot->GetName(), questId);
            }
            WorldPacket packet(CMSG_QUESTLOG_REMOVE_QUEST);
            packet << (uint8)i;
            bot->GetSession()->HandleQuestLogRemoveQuest(packet);
            if (botAI->GetMaster() && botAI->HasStrategy("debug", BOT_STATE_NON_COMBAT))
                botAI->TellMaster("Quest dropped " + ChatHelper::FormatQuest(quest));
            botAI->rpgStatistic.questDropped++;
            botAI->rpgStatistic.questDroppedByID[questId]++;
            botAI->rpgStatistic.questDropReasons["not_worth_or_capable_or_failed"]++;
            botAI->rpgStatistic.questDropReasonsByID[questId]["not_worth_or_capable_or_failed"]++;
            dropped++;
        }
    }

    // drop more than 8 quests at once to avoid repeated accept and drop
    if (dropped >= 8)
        return true;

    // Recalculate free slots after dropping unworthwhile quests
    freeSlotNum = 0;
    for (uint16 i = 0; i < MAX_QUEST_LOG_SIZE; ++i)
    {
        uint32 questId = bot->GetQuestSlotQuestId(i);
        if (!questId)
            freeSlotNum++;
    }

    // Only drop wrong-zone quests if we still don't have enough free slots
    if (freeSlotNum < 2)
    {
        // remove festival/class quests and quests in different zone
        for (uint16 i = 0; i < MAX_QUEST_LOG_SIZE; ++i)
        {
            uint32 questId = bot->GetQuestSlotQuestId(i);
            if (!questId)
                continue;

            const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
            if (quest->GetZoneOrSort() < 0 || (quest->GetZoneOrSort() > 0 && quest->GetZoneOrSort() != bot->GetZoneId()))
            {
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} drop quest {} (wrong_zone, free slots: {})", bot->GetName(), questId, freeSlotNum);
                }
                WorldPacket packet(CMSG_QUESTLOG_REMOVE_QUEST);
                packet << (uint8)i;
                bot->GetSession()->HandleQuestLogRemoveQuest(packet);
                if (botAI->GetMaster() && botAI->HasStrategy("debug", BOT_STATE_NON_COMBAT))
                    botAI->TellMaster("Quest dropped " + ChatHelper::FormatQuest(quest));
                botAI->rpgStatistic.questDropped++;
                botAI->rpgStatistic.questDroppedByID[questId]++;
                botAI->rpgStatistic.questDropReasons["wrong_zone"]++;
                botAI->rpgStatistic.questDropReasonsByID[questId]["wrong_zone"]++;
                dropped++;
            }
        }
    }

    if (dropped >= 8)
        return true;

    // clear quests log
    for (uint16 i = 0; i < MAX_QUEST_LOG_SIZE; ++i)
    {
        uint32 questId = bot->GetQuestSlotQuestId(i);
        if (!questId)
            continue;

        const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
		if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
		{
			LOG_DEBUG("playerbots", "[New RPG] {} drop quest {}", bot->GetName(), questId);
		}
        WorldPacket packet(CMSG_QUESTLOG_REMOVE_QUEST);
        packet << (uint8)i;
        bot->GetSession()->HandleQuestLogRemoveQuest(packet);
        if (botAI->GetMaster() && botAI->HasStrategy("debug", BOT_STATE_NON_COMBAT))
            botAI->TellMaster("Quest dropped " + ChatHelper::FormatQuest(quest));
        botAI->rpgStatistic.questDropped++;
        botAI->rpgStatistic.questDroppedByID[questId]++;
        botAI->rpgStatistic.questDropReasons["clear_log"]++;
        botAI->rpgStatistic.questDropReasonsByID[questId]["clear_log"]++;
    }

    return true;
}

bool NewRpgBaseAction::SearchQuestGiverAndAcceptOrReward()
{
    if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} SearchQuestGiverAndAcceptOrReward called", bot->GetName());
    }
    
    OrganizeQuestLog();
    if (ObjectGuid npcOrGo = ChooseNpcOrGameObjectToInteract(true, 80.0f))
    {
        WorldObject* object = ObjectAccessor::GetWorldObject(*bot, npcOrGo);
        if (!object)
            return false;

        bool canInteract = false;

        // Check if it's a regular questgiver
        if (bot->CanInteractWithQuestGiver(object))
        {
            canInteract = true;
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Object {} is a regular quest giver", 
                         bot->GetName(), object->GetName());
            }
        }
        // Check if it's a quest objective NPC that needs gossip interaction
        else if (Creature* creature = object->ToCreature())
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Checking creature {} in SearchQuestGiverAndAcceptOrReward", 
                         bot->GetName(), creature->GetName());
            }
            if (IsRequiredQuestObjectiveNPC(creature))
            {
                // For quest objective NPCs, always try interaction (will handle distance automatically)
                uint32 creatureEntry = creature->GetEntry();
                
                // Find which quest this NPC belongs to
                for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
                {
                    uint32 questId = bot->GetQuestSlotQuestId(slot);
                    if (!questId)
                        continue;
                        
                    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                    if (!quest || bot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
                        continue;
                        
                    // Find the objective index for this creature
                    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                    {
                        int32 requiredNpcOrGo = quest->RequiredNpcOrGo[i];
                        if (requiredNpcOrGo > 0 && requiredNpcOrGo == (int32)creatureEntry)
                        {
                            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                            {
                                LOG_DEBUG("playerbots", "[New RPG] {} Using unified quest objective interaction for quest {} objective {}", 
                                         bot->GetName(), questId, i);
                            }
                            return TryInteractWithQuestObjective(questId, i);
                        }
                    }
                }
                
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Object {} is a quest objective NPC but no matching quest found", 
                             bot->GetName(), creature->GetName());
                }
            }
            else
            {
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Object {} is NOT a quest objective NPC", 
                             bot->GetName(), creature->GetName());
                }
            }
        }
        // Check if it's a quest objective gameobject
        else if (GameObject* go = object->ToGameObject())
        {
            if (go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
            {
                // Check if we're close enough to interact
                if (IsWithinInteractionDist(go))
                {
                    canInteract = true;
                }
                else
                {
                    // We need to move closer first
                    if (botAI && botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                    {
                        LOG_DEBUG("playerbots", "[New RPG] {} Need to move closer to gameobject {} (distance: {})", 
                                  bot->GetName(), go->GetName(), bot->GetDistance(go));
                    }
                    return MoveWorldObjectTo(npcOrGo);
                }
            }
        }
        
        if (canInteract)
        {
			if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
			{
				LOG_DEBUG("playerbots", "[New RPG] {} Can interact with object {}, calling InteractWithNpcOrGameObjectForQuest", 
                     bot->GetName(), object->GetName());
			}
            InteractWithNpcOrGameObjectForQuest(npcOrGo);
            ForceToWait(5000);
            return true;
        }
        else
        {
			if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
			{
				LOG_DEBUG("playerbots", "[New RPG] {} Cannot interact with object {}, moving closer", 
                     bot->GetName(), object->GetName());
			}
        }
        return MoveWorldObjectTo(npcOrGo);
    }
    return false;
}

ObjectGuid NewRpgBaseAction::ChooseNpcOrGameObjectToInteract(bool questgiverOnly, float distanceLimit)
{
    if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
    {
		LOG_DEBUG("playerbots", "[New RPG] {} ChooseNpcOrGameObjectToInteract called (questgiverOnly: {}, distanceLimit: {:.1f})", 
             bot->GetName(), questgiverOnly, distanceLimit);
	}
             
    // First try LOS-based search for nearby NPCs
    GuidVector possibleTargets = AI_VALUE(GuidVector, "possible new rpg targets");
    GuidVector possibleGameObjects = AI_VALUE(GuidVector, "possible new rpg game objects");

    if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
    {
		LOG_DEBUG("playerbots", "[New RPG] {} Found {} possible targets with LOS", bot->GetName(), possibleTargets.size());
	}
    // If no targets found with LOS, use non-LOS search as fallback
    if (possibleTargets.empty())
    {
        possibleTargets = AI_VALUE(GuidVector, "possible new rpg targets no los");
		if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
		{
			LOG_DEBUG("playerbots", "[New RPG] {} Using fallback no-LOS search, found {} targets", bot->GetName(), possibleTargets.size());
		}
    }

    if (possibleTargets.empty() && possibleGameObjects.empty())
    {
		if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
		{
			LOG_DEBUG("playerbots", "[New RPG] {} No possible targets found", bot->GetName());
		}
        return ObjectGuid();
    }

    WorldObject* nearestObject = nullptr;
    float nearestDistance = std::numeric_limits<float>::max();
    botAI->rpgInfo.PruneOldVisits(30 * 60 * 1000); // 30 minutes
    
    for (ObjectGuid& guid : possibleTargets)
    {
        if (botAI->rpgInfo.ignoredRpgNpcs.count(guid))
            continue;  // Skip recently visited

        WorldObject* object = ObjectAccessor::GetWorldObject(*bot, guid);

        if (!object || !object->IsInWorld())
            continue;

        if (distanceLimit && bot->GetDistance(object) > distanceLimit)
            continue;

        if (CanInteractWithQuestGiver(object) && HasQuestToAcceptOrReward(object))
        {
            float adjustedDistance = bot->GetExactDist(object);
            if (adjustedDistance < nearestDistance)
            {
                nearestObject = object;
                nearestDistance = adjustedDistance;
            }
            break;
        }

        // Priority: Quest objective NPCs that need to be talked to
        Creature* creature = object->ToCreature();
        if (creature)
        {
            if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Checking creature {} (entry {}) at distance {:.1f}", 
                         bot->GetName(), creature->GetName(), creature->GetEntry(), bot->GetDistance(creature));
            }
                     
            if (IsRequiredQuestObjectiveNPC(creature))
            {
                float adjustedDistance = bot->GetExactDist(creature);
                if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Found quest objective NPC {} at distance {:.1f} (nearest: {:.1f})", 
                              bot->GetName(), creature->GetName(), adjustedDistance, nearestDistance);
                }
                          
                if (adjustedDistance < nearestDistance)
                {
                    nearestObject = creature;
                    nearestDistance = adjustedDistance;
                    if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
                    {
                        LOG_DEBUG("playerbots", "[New RPG] {} Selected quest objective NPC {} as nearest target", 
                                 bot->GetName(), creature->GetName());
                    }
                }
                break; // Prioritize quest objectives
            }
        }
    }

    for (ObjectGuid& guid : possibleGameObjects)
    {
        if (botAI->rpgInfo.ignoredRpgNpcs.count(guid))
            continue;  // Skip recently visited

        WorldObject* object = ObjectAccessor::GetWorldObject(*bot, guid);

        if (!object || !object->IsInWorld())
            continue;

        if (distanceLimit && bot->GetDistance(object) > distanceLimit)
            continue;

        // Check if it's a questgiver or a quest objective gameobject
        bool isValidTarget = false;
        
        if (CanInteractWithQuestGiver(object) && HasQuestToAcceptOrReward(object))
        {
            isValidTarget = true;
        }
        else if (GameObject* go = object->ToGameObject())
        {
            // For quest objective gameobjects, check if they're needed for current quests
            if (go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
            {
                int32 goEntry = go->GetEntry();
                
                // Check if this gameobject is required by any active quest
                for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
                {
                    uint32 questId = bot->GetQuestSlotQuestId(slot);
                    if (!questId)
                        continue;
                        
                    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                    if (!quest)
                        continue;
                    
                    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                    {
                        int32 requiredEntry = quest->RequiredNpcOrGo[i];
                        if ((requiredEntry < 0 && -requiredEntry == goEntry) && quest->RequiredNpcOrGoCount[i] > 0)
                        {
                            // Check if this objective is not yet completed
                            QuestStatusData const& q_status = bot->getQuestStatusMap().at(questId);
                            if (q_status.CreatureOrGOCount[i] < quest->RequiredNpcOrGoCount[i])
                            {
                                isValidTarget = true;
                                break;
                            }
                        }
                    }
                    if (isValidTarget)
                        break;
                }
            }
        }
        
        if (isValidTarget)
        {
            float adjustedDistance = bot->GetExactDist(object);
            if (adjustedDistance < nearestDistance)
            {
                nearestObject = object;
                nearestDistance = adjustedDistance;
            }
            
            if (botAI && botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[Debug RPG Target] {} Selected gameobject {} (distance: {})", 
                          bot->GetName(), object->GetName(), adjustedDistance);
            }
            break;
        }
        else if (botAI && botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[Debug RPG Target] {} Gameobject {} not a valid target", 
                      bot->GetName(), object->GetName());
        }
    }

    if (nearestObject)
    {
        if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} Returning nearest object: {} (distance: {:.1f})", 
                     bot->GetName(), nearestObject->GetName(), nearestDistance);
        }
        return nearestObject->GetGUID();
    }

    // If questgiverOnly is true, we still want to find quest objective NPCs for talk quests
    if (questgiverOnly)
    {
        if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} No quest givers found, checking for quest objective NPCs (questgiverOnly mode)", bot->GetName());
        }
        
        // Do a second pass specifically for quest objective NPCs
        for (ObjectGuid& guid : possibleTargets)
        {
            if (botAI->rpgInfo.ignoredRpgNpcs.count(guid))
                continue;

            Creature* creature = ObjectAccessor::GetCreature(*bot, guid);
            if (!creature || !creature->IsInWorld())
                continue;

            if (distanceLimit && bot->GetDistance(creature) > distanceLimit)
                continue;

            if (IsRequiredQuestObjectiveNPC(creature))
            {
                float distance = bot->GetExactDist(creature);
                if (distance < nearestDistance)
                {
                    nearestObject = creature;
                    nearestDistance = distance;
                }
                if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Found quest objective NPC {} in questgiverOnly mode", 
                             bot->GetName(), creature->GetName());
                }
                break;
            }
        }
        
        if (nearestObject)
        {
            if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Returning quest objective NPC: {}", bot->GetName(), nearestObject->GetName());
            }
            return nearestObject->GetGUID();
        }
        
        if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} No quest-related NPCs found in questgiverOnly mode", bot->GetName());
        }
        return ObjectGuid();
    }
    
    // Priority-based trainer selection with distance comparison
    WorldObject* bestRidingTrainer = nullptr;
    WorldObject* bestClassTrainer = nullptr;
    WorldObject* bestProfessionTrainer = nullptr;
    WorldObject* bestPetTrainer = nullptr;
    WorldObject* bestVendor = nullptr;
    WorldObject* bestRepairNPC = nullptr;
    
    float bestRidingDistance = std::numeric_limits<float>::max();
    float bestClassDistance = std::numeric_limits<float>::max();
    float bestProfessionDistance = std::numeric_limits<float>::max();
    float bestPetDistance = std::numeric_limits<float>::max();
    float bestVendorDistance = std::numeric_limits<float>::max();
    float bestRepairDistance = std::numeric_limits<float>::max();
    
    for (ObjectGuid& guid : possibleTargets)
    {
        if (botAI->rpgInfo.ignoredRpgNpcs.count(guid))
            continue;  // Skip recently visited

        Creature* creature = ObjectAccessor::GetCreature(*bot, guid);
        if (!creature || !creature->IsInWorld())
            continue;

        if (distanceLimit && bot->GetDistance(creature) > distanceLimit)
            continue;

        float distance = bot->GetExactDist(creature);

        // Priority 1: Riding trainers with GREEN spells (mobility is crucial)
        {
            Trainer::Trainer const* tData = sObjectMgr->GetTrainer(creature->GetEntry());
            if (creature->IsTrainer() && tData && tData->IsTrainerValidForPlayer(bot) &&
                tData->GetTrainerType() == Trainer::Type::Mount)
            {
                for (Trainer::Spell const& tSpell : tData->GetSpells())
                {
                    if (tData->CanTeachSpell(bot, &tSpell))
                    {
                        if (distance < bestRidingDistance)
                        {
                            bestRidingTrainer = creature;
                            bestRidingDistance = distance;
                        }
                        break;
                    }
                }
            }
            else if (creature->IsTrainer() && tData && tData->IsTrainerValidForPlayer(bot) &&
                     tData->GetTrainerType() == Trainer::Type::Class)
            {
                // Priority 2: Class trainers with GREEN spells
                for (Trainer::Spell const& tSpell : tData->GetSpells())
                {
                    if (tData->CanTeachSpell(bot, &tSpell))
                    {
                        if (distance < bestClassDistance)
                        {
                            bestClassTrainer = creature;
                            bestClassDistance = distance;
                        }
                        break;
                    }
                }
            }
            else if (creature->IsTrainer() && tData && tData->IsTrainerValidForPlayer(bot) &&
                     tData->GetTrainerType() == Trainer::Type::Pet)
            {
                // Priority 3: Pet trainers with GREEN spells (for hunters)
                for (Trainer::Spell const& tSpell : tData->GetSpells())
                {
                    if (tData->CanTeachSpell(bot, &tSpell))
                    {
                        if (distance < bestPetDistance)
                        {
                            bestPetTrainer = creature;
                            bestPetDistance = distance;
                        }
                        break;
                    }
                }
            }
            else if (creature->IsTrainer() && tData && tData->IsTrainerValidForPlayer(bot) &&
                     tData->GetTrainerType() == Trainer::Type::Tradeskill)
            {
                // Priority 4: Profession trainers with GREEN spells (secondary professions only)
                for (Trainer::Spell const& tSpell : tData->GetSpells())
                {
                    if (tData->CanTeachSpell(bot, &tSpell))
                    {
                        if (distance < bestProfessionDistance)
                        {
                            bestProfessionTrainer = creature;
                            bestProfessionDistance = distance;
                        }
                        break;
                    }
                }
            }
        }

        // Priority 5: Vendors if bags > 50% full or quest items needed
        if ((AI_VALUE(uint8, "bag space") > 50 || HasNeededQuestItemForSale()) && creature->IsVendor())
        {
            if (distance < bestVendorDistance)
            {
                bestVendor = creature;
                bestVendorDistance = distance;
            }
        }

        // Priority 6: Repair NPCs if any item < 50% durability
        if (AI_VALUE(uint8, "durability") < 50 &&
            creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_REPAIR))
        {
            if (distance < bestRepairDistance)
            {
                bestRepairNPC = creature;
                bestRepairDistance = distance;
            }
        }
    }
    
    // Return the highest priority trainer found, prioritizing by importance
    if (bestRidingTrainer)
    {
        if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} - Selected closest riding trainer at {:.1f}yd", 
                      bot->GetName(), bestRidingDistance);
        }
        return bestRidingTrainer->GetGUID();
    }
    if (bestClassTrainer)
    {
        if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} - Selected closest class trainer at {:.1f}yd", 
                      bot->GetName(), bestClassDistance);
        }
        return bestClassTrainer->GetGUID();
    }
    if (bestPetTrainer)
    {
        if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} - Selected closest pet trainer at {:.1f}yd", 
                      bot->GetName(), bestPetDistance);
        }
        return bestPetTrainer->GetGUID();
    }
    if (bestProfessionTrainer)
    {
        if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} - Selected closest profession trainer at {:.1f}yd", 
                      bot->GetName(), bestProfessionDistance);
        }
        return bestProfessionTrainer->GetGUID();
    }
    if (bestVendor)
    {
        if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} - Selected closest vendor at {:.1f}yd", 
                      bot->GetName(), bestVendorDistance);
        }
        return bestVendor->GetGUID();
    }
    if (bestRepairNPC)
    {
        if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} - Selected closest repair NPC at {:.1f}yd", 
                      bot->GetName(), bestRepairDistance);
        }
        return bestRepairNPC->GetGUID();
    }

    if (possibleTargets.empty())
        return ObjectGuid();

    int idx = urand(0, possibleTargets.size() - 1);
    ObjectGuid guid = possibleTargets[idx];
    WorldObject* object = ObjectAccessor::GetCreatureOrPetOrVehicle(*bot, guid);
    if (!object)
        object = ObjectAccessor::GetGameObject(*bot, guid);

    if (object && object->IsInWorld())
    {
        return object->GetGUID();
    }
    return ObjectGuid();
}

bool NewRpgBaseAction::HasQuestToAcceptOrReward(WorldObject* object)
{
    ObjectGuid guid = object->GetGUID();
    bot->PrepareQuestMenu(guid);
    const QuestMenu& menu = bot->PlayerTalkClass->GetQuestMenu();
    if (menu.Empty())
        return false;

    for (uint8 idx = 0; idx < menu.GetMenuItemCount(); idx++)
    {
        const QuestMenuItem& item = menu.GetItem(idx);
        const Quest* quest = sObjectMgr->GetQuestTemplate(item.QuestId);
        if (!quest)
            continue;
        const QuestStatus& status = bot->GetQuestStatus(item.QuestId);
        if (status == QUEST_STATUS_COMPLETE && bot->CanRewardQuest(quest, 0, false))
        {
            return true;
        }
    }
    for (uint8 idx = 0; idx < menu.GetMenuItemCount(); idx++)
    {
        const QuestMenuItem& item = menu.GetItem(idx);
        const Quest* quest = sObjectMgr->GetQuestTemplate(item.QuestId);
        if (!quest)
            continue;

        const QuestStatus& status = bot->GetQuestStatus(item.QuestId);
        if (status == QUEST_STATUS_NONE && bot->CanTakeQuest(quest, false) && bot->CanAddQuest(quest, false) &&
            IsQuestWorthDoing(quest) && IsQuestCapableDoing(quest))
        {
            return true;
        }
    }
    return false;
}

static std::vector<float> GenerateRandomWeights(int n)
{
    std::vector<float> weights(n);
    float sum = 0.0;

    for (int i = 0; i < n; ++i)
    {
        weights[i] = rand_norm();
        sum += weights[i];
    }
    for (int i = 0; i < n; ++i)
    {
        weights[i] /= sum;
    }
    return weights;
}

bool NewRpgBaseAction::GetQuestPOIPosAndObjectiveIdx(uint32 questId, std::vector<POIInfo>& poiInfo, bool toComplete)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return false;

    const QuestPOIVector* poiVector = sObjectMgr->GetQuestPOIVector(questId);
    if (!poiVector)
    {
        return false;
    }

    const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);

    if (toComplete && q_status.Status == QUEST_STATUS_COMPLETE)
    {
        for (const QuestPOI& qPoi : *poiVector)
        {
            if (qPoi.MapId != bot->GetMapId())
                continue;

            // not the poi pos to reward quest
            if (qPoi.ObjectiveIndex != -1)
                continue;

            if (qPoi.points.size() == 0)
                continue;

            float dx = 0, dy = 0;
            std::vector<float> weights = GenerateRandomWeights(qPoi.points.size());
            for (size_t i = 0; i < qPoi.points.size(); i++)
            {
                const QuestPOIPoint& point = qPoi.points[i];
                dx += point.x * weights[i];
                dy += point.y * weights[i];
            }

            if (bot->GetDistance2d(dx, dy) >= 2500.0f)
                continue;

            float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), 
                               bot->GetMap()->GetWaterLevel(dx, dy));

            if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
                continue;

            // Zone check removed for quest completion POIs
            // Coastal NPCs are often detected in ocean zones due to boundary imprecision
            // Map ID and distance checks provide sufficient safety

            // Create POI entry for quest completion (toComplete=true means going to turn in quest)
            POIInfo completionPOI;
            completionPOI.pos = {dx, dy};
            completionPOI.objectiveIdx = qPoi.ObjectiveIndex;
            completionPOI.z = 0.0f;
            completionPOI.useExactZ = false;
            completionPOI.radius = 0.0f;
            poiInfo.push_back(completionPOI);
        }

        if (poiInfo.empty())
            return false;

        return true;
    }

    if (q_status.Status != QUEST_STATUS_INCOMPLETE)
        return false;

    // Get incomplete quest objective index
    std::vector<int32> incompleteObjectiveIdx;
    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
    {
        int32 npcOrGo = quest->RequiredNpcOrGo[i];
        if (!npcOrGo)
            continue;

        if (q_status.CreatureOrGOCount[i] < quest->RequiredNpcOrGoCount[i])
            incompleteObjectiveIdx.push_back(i);
    }
    for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; i++)
    {
        uint32 itemId = quest->RequiredItemId[i];
        if (!itemId)
            continue;

        if (q_status.ItemCount[i] < quest->RequiredItemCount[i])
            incompleteObjectiveIdx.push_back(QUEST_OBJECTIVES_COUNT + i);
    }

    // Handle quests with no objectives (talk-to-NPC quests like "The Missing Fisherman")
    // For these quests, the turn-in POI (ObjectiveIndex = -1) is the destination
    if (incompleteObjectiveIdx.empty())
        incompleteObjectiveIdx.push_back(-1);

    // Get POIs to go
    for (const QuestPOI &qPoi : *poiVector)
    {
        if (qPoi.MapId != bot->GetMapId())
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} POI rejected: map mismatch (Bot={}, POI={})", bot->GetName(), bot->GetMapId(), qPoi.MapId);
            }
            continue;
        }
    
        bool inComplete = false;
        if (qPoi.ObjectiveIndex == 16 && (quest->GetFlags() & QUEST_FLAGS_EXPLORATION))
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Exploration quest objective detected, checking for Area Triggers", bot->GetName());
            }
            // Query areatrigger_involvedrelation to get the trigger ID
            QueryResult result = WorldDatabase.Query("SELECT id FROM areatrigger_involvedrelation WHERE quest = {}", questId);
            if (!result)
            {
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} No area trigger found for exploration quest {}", bot->GetName(), questId);
                }
                continue;
            }

            Field* fields = result->Fetch();
            uint32 triggerId = fields[0].Get<uint32>();

            // Now get the actual area trigger data
            result = WorldDatabase.Query("SELECT x, y, z, radius FROM areatrigger WHERE entry = {}", triggerId);
            if (!result)
            {
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Area trigger data not found for ID {}", bot->GetName(), triggerId);
                }
                continue;
            }

            fields = result->Fetch();
            float x = fields[0].Get<float>();
            float y = fields[1].Get<float>();
            float z = fields[2].Get<float>();
            float radius = fields[3].Get<float>();
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Exploration Area Trigger data retrieved from DB: x={}, y={}, z={}, radius={}",
                         bot->GetName(), x, y, z, radius);
            }

            // Use the area trigger's actual Z coordinate from the database, not ground-level height
            // This is critical for area triggers in mines, caves, or elevated locations
            float dz = z;

            // Verify the Z coordinate is valid
            if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
            {
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Invalid Z from area trigger database at ({}, {})", bot->GetName(), x, y);
                }
                continue;
            }

            // For area triggers, don't reject based on zone mismatch
            // The bot may need to travel from outside the zone TO the area trigger
            // Only verify we're on the same map (already checked above)
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                uint32 botZone = bot->GetZoneId();
                uint32 poiZone = bot->GetMap()->GetZoneId(bot->GetPhaseMask(), x, y, dz);
                LOG_DEBUG("playerbots", "[New RPG] {} Area trigger zone: {} (bot zone: {})",
                         bot->GetName(), poiZone, botZone);
            }

            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} POI accepted (AreaTrigger): {} at ({}, {}, {}) with radius {}",
                         bot->GetName(), qPoi.ObjectiveIndex, x, y, dz, radius);
            }

            // Store area trigger POI with exact Z coordinate and radius from database
            POIInfo triggerPOI;
            triggerPOI.pos = {x, y};
            triggerPOI.objectiveIdx = qPoi.ObjectiveIndex;
            triggerPOI.z = dz;
            triggerPOI.useExactZ = true;  // Use this exact Z instead of recalculating from ground height
            triggerPOI.radius = radius;   // Store radius so bot can move close enough to enter trigger
            poiInfo.push_back(triggerPOI);
            inComplete = true;
        }
        else
        {
            for (uint32 objective : incompleteObjectiveIdx)
            {
                if (qPoi.ObjectiveIndex == objective)
                {
                    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                    {
                        LOG_DEBUG("playerbots", "[New RPG] {} POI ObjectiveIndex {} matched an incomplete objective", bot->GetName(), qPoi.ObjectiveIndex);
                    }
                    inComplete = true;
                    break;
                }
            }
        }
   
        if (!inComplete)
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} POI rejected: ObjectiveIndex {} not in incomplete list", bot->GetName(), qPoi.ObjectiveIndex);
            }
            continue;
        }
    
        if (qPoi.points.empty())
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} POI rejected: no polygon points", bot->GetName());
            }
            continue;
        }
    
        // Instead of calculating the center point, select a random point from the polygon
        float randomX = 0, randomY = 0;
        if (!GetRandomPointInPolygon(qPoi.points, randomX, randomY))
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Failed to generate random point in polygon", bot->GetName());
            }
            continue;
        }
        
        // Use upstream clean approach for Z calculation
        float dz = std::max(bot->GetMap()->GetHeight(randomX, randomY, MAX_HEIGHT), 
                           bot->GetMap()->GetWaterLevel(randomX, randomY));
        
        if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} POI rejected: invalid Z at ({}, {})", bot->GetName(), randomX, randomY);
            }
            continue;
        }
        
        uint32 botZone = bot->GetZoneId();
        uint32 poiZone = bot->GetMap()->GetZoneId(bot->GetPhaseMask(), randomX, randomY, dz);
        
        if (botZone != poiZone)
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} POI rejected: zone mismatch (Bot={}, POI={})", bot->GetName(), botZone, poiZone);
            }
            continue;
        }
        
        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} POI accepted: {} at ({}, {}, {})", bot->GetName(), qPoi.ObjectiveIndex, randomX, randomY, dz);
        }

        // Create POI entry for regular quest objective
        POIInfo regularPOI;
        regularPOI.pos = {randomX, randomY};
        regularPOI.objectiveIdx = qPoi.ObjectiveIndex;
        regularPOI.z = 0.0f;
        regularPOI.useExactZ = false;
        regularPOI.radius = 0.0f;
        poiInfo.push_back(regularPOI);
    }


    if (poiInfo.size() == 0)
    {
        // LOG_DEBUG("playerbots", "[New rpg] {}: No available poi can be found for quest {}", bot->GetName(), questId);
        return false;
    }

    return true;
}

WorldPosition NewRpgBaseAction::SelectRandomGrindPos(Player* bot)
{
    const std::vector<WorldLocation>& locs = sTravelMgr.GetLocsPerLevelCache(bot->GetLevel());
    float hiRange = 500.0f;
    float loRange = 2500.0f;
    if (bot->GetLevel() < 5)
    {
        hiRange /= 3;
        loRange /= 3;
    }
    std::vector<WorldLocation> lo_prepared_locs, hi_prepared_locs;

    bool inCity = false;
    if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId()))
    {
        if (zone->flags & AREA_FLAG_CAPITAL)
            inCity = true;
    }

    for (auto& loc : locs)
    {
        if (bot->GetMapId() != loc.GetMapId())
            continue;

        if (bot->GetExactDist(loc) > 2500.0f)
            continue;

        if (!inCity && bot->GetMap()->GetZoneId(bot->GetPhaseMask(), loc.GetPositionX(), loc.GetPositionY(),
                                                loc.GetPositionZ()) != bot->GetZoneId())
            continue;

        if (bot->GetExactDist(loc) < hiRange)
        {
            hi_prepared_locs.push_back(loc);
        }

        if (bot->GetExactDist(loc) < loRange)
        {
            lo_prepared_locs.push_back(loc);
        }
    }

    // If no grind spots found in current zone, try graveyard as escape route
    // This helps bots stuck in invalid zones (ocean, arena, etc.)
    if (hi_prepared_locs.empty() && lo_prepared_locs.empty() && !inCity)
    {
        GraveyardStruct const* graveyard = sGraveyard->GetClosestGraveyard(bot, bot->GetTeamId());
        if (graveyard)
        {
            WorldPosition escapePos(graveyard->Map, graveyard->x, graveyard->y, graveyard->z);
            if (bot->GetMapId() == graveyard->Map && bot->GetExactDist(escapePos) <= 5000.0f)
                hi_prepared_locs.push_back(escapePos);
        }
    }

    WorldPosition dest{};
    if (urand(1, 100) <= 50 && !hi_prepared_locs.empty())
    {
        uint32 idx = urand(0, hi_prepared_locs.size() - 1);
        dest = hi_prepared_locs[idx];
    }
    else if (!lo_prepared_locs.empty())
    {
        uint32 idx = urand(0, lo_prepared_locs.size() - 1);
        dest = lo_prepared_locs[idx];
    }
    // Note: Cannot add debug strategy check here as this is a static function
    // LOG_DEBUG("playerbots", "[New RPG] Bot {} select random grind pos Map:{} X:{} Y:{} Z:{} ({}+{} available in {})",
    //           bot->GetName(), dest.GetMapId(), dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(),
    //           hi_prepared_locs.size(), lo_prepared_locs.size() - hi_prepared_locs.size(), locs.size());
    return dest;
}

 WorldPosition NewRpgBaseAction::SelectRandomCampPos(Player* bot)
{
    const std::vector<WorldLocation> locs = sTravelMgr.GetTravelHubs(bot);

    bool inCity = false;

    if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId()))
    {
        if (zone->flags & AREA_FLAG_CAPITAL)
            inCity = true;
    }

    std::vector<WorldLocation> prepared_locs;
    for (auto& loc : locs)
    {
        if (bot->GetMapId() != loc.GetMapId())
            continue;

        float range = bot->GetLevel() <= 5 ? 500.0f : 2500.0f;
        if (bot->GetExactDist(loc) > range)
            continue;

        if (bot->GetExactDist(loc) < 50.0f)
            continue;

        if (!inCity && bot->GetMap()->GetZoneId(bot->GetPhaseMask(), loc.GetPositionX(), loc.GetPositionY(),
                                                loc.GetPositionZ()) != bot->GetZoneId())
            continue;

        prepared_locs.push_back(loc);
    }

    // If no camps found in current zone, try graveyard as escape route
    // This helps bots stuck in invalid zones (ocean, arena, etc.)
    if (prepared_locs.empty() && !inCity)
    {
        GraveyardStruct const* graveyard = sGraveyard->GetClosestGraveyard(bot, bot->GetTeamId());
        if (graveyard)
        {
            WorldPosition escapePos(graveyard->Map, graveyard->x, graveyard->y, graveyard->z);
            if (bot->GetMapId() == graveyard->Map && bot->GetExactDist(escapePos) > 50.0f && bot->GetExactDist(escapePos) <= 5000.0f)
                prepared_locs.push_back(escapePos);
        }
    }

    WorldPosition dest{};
    if (!prepared_locs.empty())
    {
        // 66% chance to favor nearest camp, 34% chance for random camp
        if (urand(1, 100) <= 66)
        {
            // Find nearest camp
            float nearestDistance = std::numeric_limits<float>::max();
            uint32 nearestIdx = 0;
            for (uint32 i = 0; i < prepared_locs.size(); ++i)
            {
                float distance = bot->GetExactDist(prepared_locs[i]);
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearestIdx = i;
                }
            }
            dest = prepared_locs[nearestIdx];
            // Note: Cannot add debug strategy check here as this is a static function
            // LOG_DEBUG("playerbots", "[New RPG] Bot {} selected NEAREST camp at {:.1f}yd (66% chance)", 
            //           bot->GetName(), nearestDistance);
        }
        else
        {
            // Random camp selection
            uint32 idx = urand(0, prepared_locs.size() - 1);
            dest = prepared_locs[idx];
            float randomDistance = bot->GetExactDist(dest);
            // Note: Cannot add debug strategy check here as this is a static function
            // LOG_DEBUG("playerbots", "[New RPG] Bot {} selected RANDOM camp at {:.1f}yd (34% chance)", 
            //           bot->GetName(), randomDistance);
        }
    }
    // Note: Cannot add debug strategy check here as this is a static function
    // LOG_DEBUG("playerbots", "[New RPG] Bot {} select random inn keeper pos Map:{} X:{} Y:{} Z:{} ({} available in {})",
    //           bot->GetName(), dest.GetMapId(), dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(),
    //           prepared_locs.size(), locs.size());
    return dest;
}

bool NewRpgBaseAction::SelectRandomFlightTaxiNode(ObjectGuid& flightMasterGuid, uint32& fromNode, uint32& toNode, WorldPosition& flightMasterPos)
{
    TravelMgr::FlightMasterInfo const* nearestFlightMaster = sTravelMgr.GetNearestFlightMasterInfo(bot);
    if (!nearestFlightMaster || bot->GetDistance(nearestFlightMaster->pos) > 500.0f)
        return false;

    fromNode = nearestFlightMaster->taxiNodeId;
    if (!fromNode)
        return false;

    std::vector<std::vector<uint32>> optimalDestinations = sTravelMgr.GetOptimalFlightDestinations(bot);
    if (optimalDestinations.empty())
        return false;

    std::vector<uint32> chosenPath = optimalDestinations[urand(0, optimalDestinations.size() - 1)];
    if (chosenPath.empty())
        return false;

    toNode = chosenPath.back();

    TaxiNodesEntry const* destNode = sTaxiNodesStore.LookupEntry(toNode);
    if (!destNode)
        return false;

    if (!bot->isTaxiCheater() && !bot->m_taxi.IsTaximaskNodeKnown(toNode))
        return false;

    flightMasterGuid = ObjectGuid::Create<HighGuid::Unit>(nearestFlightMaster->templateEntry, nearestFlightMaster->dbGuid);
    flightMasterPos = nearestFlightMaster->pos;

    if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[New RPG] Bot {} select random flight taxi node from:{} (node {}) to:{}",
                  bot->GetName(), flightMasterGuid.GetCounter(), fromNode, toNode);
    }
    return true;
}

bool NewRpgBaseAction::RandomChangeStatus(std::vector<NewRpgStatus> candidateStatus)
{
    std::vector<NewRpgStatus> availableStatus;
    uint32 probSum = 0;
    for (NewRpgStatus status : candidateStatus)
    {
        if (sPlayerbotAIConfig.RpgStatusProbWeight[status] == 0)
            continue;

        if (CheckRpgStatusAvailable(status))
        {
            availableStatus.push_back(status);
            probSum += sPlayerbotAIConfig.RpgStatusProbWeight[status];
        }
    }
    // Safety check. Default to "rest" if all RPG weights = 0
    if (availableStatus.empty() || probSum == 0)
    {
        botAI->rpgInfo.ChangeToRest();
        bot->SetStandState(UNIT_STAND_STATE_SIT);
        return true;
    }
    uint32 rand = urand(1, probSum);
    uint32 accumulate = 0;
    NewRpgStatus chosenStatus = RPG_STATUS_END;
    for (NewRpgStatus status : availableStatus)
    {
        accumulate += sPlayerbotAIConfig.RpgStatusProbWeight[status];
        if (accumulate >= rand)
        {
            chosenStatus = status;
            break;
        }
    }

    switch (chosenStatus)
    {
        case RPG_WANDER_RANDOM:
        {
            botAI->rpgInfo.ChangeToWanderRandom();
            return true;
        }
        case RPG_WANDER_NPC:
        {
            botAI->rpgInfo.ChangeToWanderNpc();
            return true;
        }
        case RPG_GO_GRIND:
        {
            WorldPosition pos = SelectRandomGrindPos(bot);
            if (pos != WorldPosition())
            {
                botAI->rpgInfo.ChangeToGoGrind(pos);
                return true;
            }
            return false;
        }
        case RPG_GO_CAMP:
        {
            WorldPosition pos = SelectRandomCampPos(bot);
            if (pos != WorldPosition())
            {
                botAI->rpgInfo.ChangeToGoCamp(pos);
                return true;
            }
            return false;
        }
        case RPG_DO_QUEST:
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
                    botAI->rpgInfo.ChangeToDoQuest(questId, quest);
                    return true;
                }
            }
            return false;
        }
        case RPG_TRAVEL_FLIGHT:
        {
            ObjectGuid flightMaster;
            uint32 fromNode, toNode;
            WorldPosition flightMasterPos;
            if (SelectRandomFlightTaxiNode(flightMaster, fromNode, toNode, flightMasterPos))
            {
                botAI->rpgInfo.ChangeToTravelFlight(flightMaster, fromNode, toNode);
                return true;
            }
            return false;
        }
        case RPG_IDLE:
        {
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        case RPG_REST:
        {
            botAI->rpgInfo.ChangeToRest();
            bot->SetStandState(UNIT_STAND_STATE_SIT);
            return true;
        }
        default:
        {
            botAI->rpgInfo.ChangeToRest();
            bot->SetStandState(UNIT_STAND_STATE_SIT);
            return true;
        }
    }
    return false;
}

bool NewRpgBaseAction::CheckRpgStatusAvailable(NewRpgStatus status)
{
    switch (status)
    {
        case RPG_IDLE:
        case RPG_REST:
            return true;
        case RPG_WANDER_RANDOM:
        {
            Unit* target = AI_VALUE(Unit*, "grind target");
            return target != nullptr;
        }
        case RPG_GO_GRIND:
        {
            WorldPosition pos = SelectRandomGrindPos(bot);
            return pos != WorldPosition();
        }
        case RPG_GO_CAMP:
        {
            WorldPosition pos = SelectRandomCampPos(bot);
            return pos != WorldPosition();
        }
        case RPG_WANDER_NPC:
        {
            GuidVector possibleTargets = AI_VALUE(GuidVector, "possible new rpg targets");
            return possibleTargets.size() >= 3;
        }
        case RPG_DO_QUEST:
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
                    return true;
                }
            }
            return false;
        }
        case RPG_TRAVEL_FLIGHT:
        {
            ObjectGuid flightMaster;
            uint32 fromNode, toNode;
            WorldPosition flightMasterPos;
            return SelectRandomFlightTaxiNode(flightMaster, fromNode, toNode, flightMasterPos);
        }
        default:
            return false;
    }
    return false;
}

// Helper function to generate a random point inside a convex polygon
bool NewRpgBaseAction::GetRandomPointInPolygon(const std::vector<QuestPOIPoint>& points, float& outX, float& outY)
{
    if (points.empty())
        return false;

    // If we have only 1 point, return that point
    if (points.size() == 1)
    {
        outX = points[0].x;
        outY = points[0].y;
        return true;
    }

    // If we have only 2 points, return a random point on the line segment
    if (points.size() == 2)
    {
        float t = (float)rand() / RAND_MAX;
        outX = points[0].x + t * (points[1].x - points[0].x);
        outY = points[0].y + t * (points[1].y - points[0].y);
        return true;
    }

    // For 3+ points, use barycentric sampling from triangles
    // Simple method: pick a random triangle from the polygon (assuming convex)
    size_t numTriangles = points.size() - 2;
    if (numTriangles == 0)
        return false;

    size_t triangleIndex = urand(0, numTriangles - 1);

    // Triangle vertices
    const QuestPOIPoint& a = points[0];
    const QuestPOIPoint& b = points[triangleIndex + 1];
    const QuestPOIPoint& c = points[triangleIndex + 2];

    // Barycentric coordinates
    float r1 = (float)rand() / RAND_MAX;
    float r2 = (float)rand() / RAND_MAX;

    // Ensure r1 + r2 <= 1
    if (r1 + r2 > 1)
    {
        r1 = 1 - r1;
        r2 = 1 - r2;
    }

    float u = 1 - r1 - r2;
    float v = r1;
    float w = r2;

    outX = u * a.x + v * b.x + w * c.x;
    outY = u * a.y + v * b.y + w * c.y;

    return true;
}

bool NewRpgBaseAction::SearchForActualQuestTargets(uint32 questId)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest) return false;

    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} Smart fallback search for quest {}", bot->GetName(), questId);
    }

    // Use far-range search (300y) to find quest objectives in caves/dungeons
    // This allows bots to find objectives even when POI points to wrong location (e.g. surface above cave)
    GuidVector nearbyNPCs = AI_VALUE(GuidVector, "far npcs");
    GuidVector nearbyGOs = AI_VALUE(GuidVector, "far game objects no los");

    // Check direct kill credit requirements first
    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        int32 requiredNpcOrGo = quest->RequiredNpcOrGo[i];
        if (requiredNpcOrGo == 0) continue;

        // Check if we still need this objective
        const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
        if (q_status.CreatureOrGOCount[i] >= quest->RequiredNpcOrGoCount[i])
            continue;

        if (requiredNpcOrGo > 0) // NPC kill credit
        {
            uint32 targetEntry = requiredNpcOrGo;
            
            for (const ObjectGuid& guid : nearbyNPCs)
            {
                Creature* creature = ObjectAccessor::GetCreature(*bot, guid);
                if (!creature || !creature->IsInWorld()) continue;
                
                if (creature->GetEntry() == targetEntry && bot->GetDistance(creature) <= 200.0f)
                {
                    // Validate NPC is near quest objective POI (not a different spawn)
                    // This prevents bots from selecting wrong NPC copies (e.g., wandering beach NPCs vs event-spawned NPCs)
                    std::vector<POIInfo> questPOIs;
                    if (GetQuestPOIPosAndObjectiveIdx(questId, questPOIs, false))
                    {
                        // Find POI for this specific objective index
                        float minDistanceToPOI = 999999.0f;
                        bool foundMatchingPOI = false;

                        for (const POIInfo& poi : questPOIs)
                        {
                            if (poi.objectiveIdx == i) // Match objective index
                            {
                                foundMatchingPOI = true;
                                float distToPOI = creature->GetDistance2d(poi.pos.x, poi.pos.y);
                                minDistanceToPOI = std::min(minDistanceToPOI, distToPOI);
                            }
                        }

                        // Skip this NPC if it's far from relevant POI
                        // This filters out wrong spawns (e.g., wandering copy vs event-spawned copy)
                        if (foundMatchingPOI && minDistanceToPOI > 100.0f)
                        {
                            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                            {
                                LOG_DEBUG("playerbots", "[New RPG] {} Skipping NPC {} - too far from quest POI ({:.1f} yards)",
                                         bot->GetName(), creature->GetName(), minDistanceToPOI);
                            }
                            continue; // Skip this NPC, check next one
                        }
                    }

                    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                    {
                        LOG_DEBUG("playerbots", "[New RPG] {} Found direct kill target {} at exact position",
                                 bot->GetName(), creature->GetName());
                    }

                    // Use the actual target's position - no Z calculations needed!
                    WorldPosition targetPos(creature->GetMapId(), creature->GetPositionX(),
                                          creature->GetPositionY(), creature->GetPositionZ());

                    botAI->rpgInfo.do_quest.pos = targetPos;
                    botAI->rpgInfo.do_quest.objectiveIdx = i;
                    botAI->rpgInfo.do_quest.lastReachPOI = 0;

                    return true;
                }
            }
        }
        else // GameObject interaction
        {
            uint32 targetEntry = -requiredNpcOrGo;
            
            for (const ObjectGuid& guid : nearbyGOs)
            {
                GameObject* go = ObjectAccessor::GetGameObject(*bot, guid);
                if (!go || !go->IsInWorld()) continue;
                
                if (go->GetEntry() == targetEntry && bot->GetDistance(go) <= 200.0f)
                {
                    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                    {
                        LOG_DEBUG("playerbots", "[New RPG] {} Found quest GameObject {} at exact position", 
                                 bot->GetName(), go->GetGOInfo()->name);
                    }
                    
                    WorldPosition targetPos(go->GetMapId(), go->GetPositionX(), 
                                          go->GetPositionY(), go->GetPositionZ());
                    
                    botAI->rpgInfo.do_quest.pos = targetPos;
                    botAI->rpgInfo.do_quest.objectiveIdx = i;
                    botAI->rpgInfo.do_quest.lastReachPOI = 0;
                    
                    return true;
                }
            }
        }
    }

    // Check for creatures that drop needed quest items using server's loot system
    for (const ObjectGuid& guid : nearbyNPCs)
    {
        Creature* creature = ObjectAccessor::GetCreature(*bot, guid);
        if (!creature || !creature->IsInWorld()) continue;
        
        if (bot->GetDistance(creature) > 200.0f) continue;

        // Use the server's built-in quest loot detection system!
        if (LootTemplates_Creature.HaveQuestLootForPlayer(creature->GetEntry(), bot))
        {
            
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Found quest item dropper {} (server confirmed quest loot)", 
                         bot->GetName(), creature->GetName());
            }

            // Use creature's exact position - no Z calculations needed
            WorldPosition targetPos(creature->GetMapId(), creature->GetPositionX(), 
                                  creature->GetPositionY(), creature->GetPositionZ());
            
            botAI->rpgInfo.do_quest.pos = targetPos;
            botAI->rpgInfo.do_quest.objectiveIdx = 0; // Item objectives
            botAI->rpgInfo.do_quest.lastReachPOI = 0;
            
            return true;
        }
    }

    return false;
}

bool NewRpgBaseAction::IsWithinPOIBoundary(float x, float y, float tolerance)
{
    // Check if bot is doing a quest and has POI data available
    if (botAI->rpgInfo.status == RPG_DO_QUEST && botAI->rpgInfo.do_quest.questId > 0)
    {
        std::vector<POIInfo> poiInfo;
        if (GetQuestPOIPosAndObjectiveIdx(botAI->rpgInfo.do_quest.questId, poiInfo))
        {
            // Check if point is inside any quest POI polygon or within tolerance buffer
            const QuestPOIVector* poiVector = sObjectMgr->GetQuestPOIVector(botAI->rpgInfo.do_quest.questId);
            if (poiVector)
            {
                for (const QuestPOI& qPoi : *poiVector)
                {
                    if (qPoi.MapId != bot->GetMapId())
                        continue;
                        
                    if (qPoi.points.empty())
                        continue;
                    
                    // First check: Is point inside polygon? (ray casting algorithm)
                    bool inside = false;
                    size_t j = qPoi.points.size() - 1;
                    for (size_t i = 0; i < qPoi.points.size(); j = i++)
                    {
                        const QuestPOIPoint& pi = qPoi.points[i];
                        const QuestPOIPoint& pj = qPoi.points[j];
                        
                        if (((pi.y > y) != (pj.y > y)) &&
                            (x < (pj.x - pi.x) * (y - pi.y) / (pj.y - pi.y) + pi.x))
                        {
                            inside = !inside;
                        }
                    }
                    
                    // If inside polygon, always allow
                    if (inside)
                        return true;
                    
                    // If outside polygon, check if within tolerance buffer
                    float minDistanceToEdge = FLT_MAX;
                    for (size_t i = 0; i < qPoi.points.size(); ++i)
                    {
                        const QuestPOIPoint& p1 = qPoi.points[i];
                        const QuestPOIPoint& p2 = qPoi.points[(i + 1) % qPoi.points.size()];
                        
                        // Distance from point to line segment
                        float A = x - p1.x;
                        float B = y - p1.y;
                        float C = p2.x - p1.x;
                        float D = p2.y - p1.y;
                        
                        float dot = A * C + B * D;
                        float lenSq = C * C + D * D;
                        float param = (lenSq != 0) ? dot / lenSq : -1;
                        
                        float xx, yy;
                        if (param < 0)
                        {
                            xx = p1.x;
                            yy = p1.y;
                        }
                        else if (param > 1)
                        {
                            xx = p2.x;
                            yy = p2.y;
                        }
                        else
                        {
                            xx = p1.x + param * C;
                            yy = p1.y + param * D;
                        }
                        
                        float dx = x - xx;
                        float dy = y - yy;
                        float distance = std::sqrt(dx * dx + dy * dy);
                        
                        if (distance < minDistanceToEdge)
                            minDistanceToEdge = distance;
                    }
                    
                    // If within tolerance buffer outside polygon, allow
                    if (minDistanceToEdge <= tolerance)
                        return true;
                }
            }
        }
    }
    
    // If no quest POI data available, use zone-based fallback
    // Stay within reasonable distance of current position (conservative approach)
    float currentX = bot->GetPositionX();
    float currentY = bot->GetPositionY();
    float distanceFromStart = std::sqrt((x - currentX) * (x - currentX) + (y - currentY) * (y - currentY));
    
    return distanceFromStart <= tolerance;
}

bool NewRpgBaseAction::CheckGameObjectLockRequirements(GameObject* go, uint32& reqItem, uint32& skillId, uint32& reqSkillValue)
{
    // Only check GOOBER type objects (type 10)
    if (go->GetGoType() != GAMEOBJECT_TYPE_GOOBER)
        return true; // Non-GOOBER objects are always accessible
        
    // Get lock info from Data0 field
    uint32 lockId = go->GetGOInfo()->goober.lockId; // Data0 for type 10
    if (lockId == 0)
        return true; // No lock requirements
        
    // Use existing lock logic from LootObjectStack
    LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
    if (!lockInfo)
        return true;
        
    // Find best lock option (adapted from LootObjectStack logic)
    bool foundAccessibleLock = false;
    uint32 bestReqItem = 0;
    uint32 bestSkillId = SKILL_NONE;
    uint32 bestReqSkillValue = 0;
    
    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} Checking lock {} for GameObject {}", 
                 bot->GetName(), lockId, go->GetGOInfo()->name);
    }
    
    for (uint8 i = 0; i < 8; ++i)
    {
        switch (lockInfo->Type[i])
        {
            case LOCK_KEY_ITEM:
                if (lockInfo->Index[i] > 0)
                {
                    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                    {
                        ItemTemplate const* keyProto = sObjectMgr->GetItemTemplate(lockInfo->Index[i]);
                        LOG_DEBUG("playerbots", "[New RPG] {} Lock requires key item {} ({})", 
                                 bot->GetName(), lockInfo->Index[i], 
                                 keyProto ? keyProto->Name1 : "Unknown");
                    }
                    
                    if (bot->HasItemCount(lockInfo->Index[i], 1))
                    {
                        // Bot has the key - this is accessible
                        reqItem = lockInfo->Index[i];
                        skillId = SKILL_NONE;
                        reqSkillValue = 0;
                        
                        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                        {
                            LOG_DEBUG("playerbots", "[New RPG] {} Bot has required key item - GameObject is accessible", 
                                     bot->GetName());
                        }
                        return true;
                    }
                    else
                    {
                        // Remember this requirement for later
                        bestReqItem = lockInfo->Index[i];
                        
                        if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                        {
                            LOG_DEBUG("playerbots", "[New RPG] {} Bot does not have required key item", bot->GetName());
                        }
                    }
                }
                break;
                
            case LOCK_KEY_SKILL:
                // Add skill-based unlocking logic if needed in the future
                if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} Lock requires skill {} (not implemented)", 
                             bot->GetName(), lockInfo->Index[i]);
                }
                break;
                
            default:
                break;
        }
    }
    
    // Return the requirements found
    reqItem = bestReqItem;
    skillId = bestSkillId;  
    reqSkillValue = bestReqSkillValue;
    
    // Object is not currently accessible
    if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} GameObject {} is not accessible - missing requirements", 
                 bot->GetName(), go->GetGOInfo()->name);
    }
    return false;
}

bool NewRpgBaseAction::CanAccessLockedGameObject(GameObject* go)
{
    uint32 reqItem, skillId, reqSkillValue;
    return CheckGameObjectLockRequirements(go, reqItem, skillId, reqSkillValue);
}

bool NewRpgBaseAction::HasRequiredKeyItem(uint32 itemId)
{
    return bot->HasItemCount(itemId, 1);
}

bool NewRpgBaseAction::HasQuestItemInDropTable(uint32 questId, uint32 itemId)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return false;

    // Check ItemDrop fields
    for (uint8 i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
    {
        if (quest->ItemDrop[i] == itemId)
        {
            if (botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Quest {} has item {} in ItemDrop[{}]",
                          bot->GetName(), questId, itemId, i);
            }
            return true;
        }
    }

    return false;
}

/* ==================== WANDER NPC CACHE + DISTRICT ==================== */

bool NewRpgBaseAction::IsInCapitalCity(Player* bot)
{
    uint32 areaId = bot->GetAreaId();
    while (areaId)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
        if (!area)
            break;
        if (area->flags & AREA_FLAG_CAPITAL)
            return true;
        areaId = area->zone;
    }
    return false;
}

uint32 NewRpgBaseAction::GetCurrentDistrictId(Player* bot)
{
    if (IsInCapitalCity(bot))
        return bot->GetAreaId();
    return bot->GetZoneId();
}

WorldPosition NewRpgBaseAction::GetDistrictCenter(Player* bot, uint32 areaId)
{
    // Try TravelMgr hubs first
    std::vector<WorldLocation> hubs = sTravelMgr.GetTravelHubs(bot);
    WorldPosition center;
    float sumX = 0, sumY = 0, sumZ = 0;
    uint32 count = 0;
    for (WorldLocation const& loc : hubs)
    {
        WorldPosition pos(loc);
        if (pos.GetMapId() == bot->GetMapId())
        {
            uint32 posAreaId = pos.getAreaId();
            if (posAreaId == areaId)
            {
                sumX += pos.GetPositionX();
                sumY += pos.GetPositionY();
                sumZ += pos.GetPositionZ();
                ++count;
            }
        }
    }
    if (count > 0)
    {
        center = WorldPosition(bot->GetMapId(), sumX / count, sumY / count, sumZ / count);
        return center;
    }

    // Fallback: locs per level
    auto const& locs = sTravelMgr.GetLocsPerLevelCache(bot->GetLevel());
    count = 0;
    sumX = sumY = sumZ = 0;
    for (WorldLocation const& loc : locs)
    {
        WorldPosition pos(loc);
        if (pos.GetMapId() == bot->GetMapId() && pos.getAreaId() == areaId)
        {
            sumX += pos.GetPositionX();
            sumY += pos.GetPositionY();
            sumZ += pos.GetPositionZ();
            ++count;
        }
    }
    if (count > 0)
    {
        center = WorldPosition(bot->GetMapId(), sumX / count, sumY / count, sumZ / count);
        return center;
    }

    // Fallback: current position
    return WorldPosition(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
}

void NewRpgBaseAction::UpdateNpcCache()
{
    NewRpgInfo& info = botAI->rpgInfo;
    info.cachedNpcs.clear();

    uint32 visibleCount = 0;
    uint32 travelMgrCount = 0;

    // Add visible NPCs
    GuidVector nearbyCreatures = AI_VALUE(GuidVector, "possible new rpg targets");
    if (nearbyCreatures.empty())
        nearbyCreatures = AI_VALUE(GuidVector, "possible new rpg targets no los");

    for (ObjectGuid const& guid : nearbyCreatures)
    {
        Creature* creature = ObjectAccessor::GetCreature(*bot, guid);
        if (!creature || !creature->IsInWorld())
            continue;

        // Immediately ignore vendors with no useful items
        if (creature->HasNpcFlag(UNIT_NPC_FLAG_VENDOR_MASK) && !HasUsefulVendorItems(creature))
        {
            info.ignoredRpgNpcs[guid] = getMSTime();
            continue;
        }

        float utility = CalculateNpcUtility(creature);
        if (utility < sPlayerbotAIConfig.rpgMinNpcUtility)
            continue;

        CachedNpc npc;
        npc.guid = guid;
        npc.pos = WorldPosition(creature);
        npc.areaId = creature->GetAreaId();
        npc.utility = utility;
        npc.lastConsidered = 0;
        npc.fromTravelMgr = false;

        // Check if flight master
        if (creature->HasNpcFlag(UNIT_NPC_FLAG_FLIGHTMASTER))
        {
            npc.isFlightMaster = true;
            npc.taxiNodeId = GetTaxiNodeForCreature(creature);
            npc.taxiNodeKnown = npc.taxiNodeId ? bot->m_taxi.IsTaximaskNodeKnown(npc.taxiNodeId) : false;
            if (!npc.taxiNodeKnown && npc.taxiNodeId)
                npc.utility = std::max(npc.utility, 0.88f);
        }

        info.cachedNpcs.push_back(npc);
        ++visibleCount;
    }

    // Add TravelMgr flight master entries not already in cache
    TravelMgr::FlightMasterInfo const* nearestFM = sTravelMgr.GetNearestFlightMasterInfo(bot);
    if (nearestFM)
    {
        float dist = bot->GetDistance(nearestFM->pos);
        if (dist <= 500.0f)
        {
            bool alreadyCached = false;
            for (CachedNpc const& cached : info.cachedNpcs)
            {
                if (cached.pos.GetMapId() == nearestFM->pos.GetMapId() &&
                    std::sqrt((cached.pos.GetPositionX() - nearestFM->pos.GetPositionX()) * (cached.pos.GetPositionX() - nearestFM->pos.GetPositionX()) +
                              (cached.pos.GetPositionY() - nearestFM->pos.GetPositionY()) * (cached.pos.GetPositionY() - nearestFM->pos.GetPositionY())) < 10.0f)
                {
                    alreadyCached = true;
                    break;
                }
            }
            if (!alreadyCached)
            {
                CachedNpc npc;
                npc.guid = ObjectGuid::Create<HighGuid::Unit>(nearestFM->templateEntry, nearestFM->dbGuid);
                npc.pos = nearestFM->pos;
                npc.areaId = nearestFM->zoneId;
                npc.fromTravelMgr = true;
                npc.isFlightMaster = true;
                npc.taxiNodeId = nearestFM->taxiNodeId;
                npc.taxiNodeKnown = npc.taxiNodeId ? bot->m_taxi.IsTaximaskNodeKnown(npc.taxiNodeId) : false;
                npc.utility = npc.taxiNodeKnown ? 0.35f : 0.88f;
                npc.lastConsidered = 0;

                info.cachedNpcs.push_back(npc);
                ++travelMgrCount;
            }
        }
    }

    info.lastCacheUpdate = getMSTime();
    info.cachedDistrictId = GetCurrentDistrictId(bot);

    bool debug = botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT) || sPlayerbotAIConfig.rpgDebugDistrictTracking;
    if (debug)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} Cache rebuilt: {} visible + {} TravelMgr NPCs (total {})",
                  bot->GetName(), visibleCount, travelMgrCount, info.cachedNpcs.size());
    }
}

float NewRpgBaseAction::CalculateNpcUtility(Creature* creature)
{
    if (!creature || !creature->IsInWorld())
        return 0.0f;

    uint32 npcFlags = creature->GetCreatureTemplate()->npcflag;

    // Quest accept/reward
    if (bot->CanInteractWithQuestGiver(creature))
    {
        bot->PrepareQuestMenu(creature->GetGUID());
        const QuestMenu& menu = bot->PlayerTalkClass->GetQuestMenu();
        if (!menu.Empty())
        {
            for (uint8 idx = 0; idx < menu.GetMenuItemCount(); ++idx)
            {
                const QuestMenuItem& item = menu.GetItem(idx);
                QuestStatus status = bot->GetQuestStatus(item.QuestId);
                if (status == QUEST_STATUS_NONE)
                    return 1.0f; // Quest available to accept
                if (status == QUEST_STATUS_COMPLETE)
                    return 1.0f; // Quest available to turn in
            }
        }
    }

    // Quest objective NPC
    if (IsRequiredQuestObjectiveNPC(creature))
        return 0.95f;

    // Trainer checks
    Trainer::Trainer const* trainerData = sObjectMgr->GetTrainer(creature->GetEntry());
    if (trainerData && trainerData->IsTrainerValidForPlayer(bot))
    {
        Trainer::Type tType = trainerData->GetTrainerType();
        bool hasGreenSpells = false;
        for (Trainer::Spell const& tSpell : trainerData->GetSpells())
        {
            if (trainerData->CanTeachSpell(bot, &tSpell))
            {
                hasGreenSpells = true;
                break;
            }
        }
        if (hasGreenSpells)
        {
            switch (tType)
            {
                case Trainer::Type::Mount: return 0.9f;
                case Trainer::Type::Class: return 0.85f;
                case Trainer::Type::Pet: return 0.8f;
                case Trainer::Type::Tradeskill:
                {
                    static TrainerClassifier classifier;
                    if (classifier.IsValidSecondaryTrainer(bot, creature))
                        return 0.7f;
                    return 0.0f;
                }
                default: return 0.1f;
            }
        }
    }

    // Flight master
    if (npcFlags & UNIT_NPC_FLAG_FLIGHTMASTER)
    {
        uint32 nodeId = GetTaxiNodeForCreature(creature);
        if (nodeId && !bot->m_taxi.IsTaximaskNodeKnown(nodeId))
            return 0.88f;
        return 0.35f;
    }

    // Vendor (higher utility when bags are full)
    if (npcFlags & UNIT_NPC_FLAG_VENDOR_MASK)
    {
        if (!HasUsefulVendorItems(creature))
            return 0.0f;

        uint32 totalSlots = 0;
        uint32 usedSlots = 0;
        for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        {
            if (Item* bag = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                totalSlots += MAX_BAG_SIZE;
                for (uint8 j = 0; j < MAX_BAG_SIZE; ++j)
                    if (bot->GetItemByPos(i, j))
                        ++usedSlots;
            }
        }
        float bagUsage = totalSlots > 0 ? (float)usedSlots / totalSlots : 0.0f;
        if (bagUsage > 0.5f)
            return 0.6f;
        return 0.2f;
    }

    // Repair
    if (npcFlags & UNIT_NPC_FLAG_REPAIR)
    {
        // Simple heuristic: check if any equipment has durability below 50%
        bool needsRepair = false;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            {
                if (item->IsInBag() && item->GetTemplate()->MaxDurability > 0)
                {
                    uint32 curDur = item->GetUInt32Value(ITEM_FIELD_DURATION);
                    if (curDur < item->GetTemplate()->MaxDurability / 2)
                    {
                        needsRepair = true;
                        break;
                    }
                }
            }
        }
        if (needsRepair)
            return 0.55f;
        return 0.15f;
    }

    // Banker / innkeeper
    if (npcFlags & (UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_INNKEEPER))
        return 0.4f;

    return 0.1f;
}

bool NewRpgBaseAction::ShouldVisit(ObjectGuid guid, CachedNpc& npc)
{
    // Already visited recently?
    if (botAI->rpgInfo.ignoredRpgNpcs.count(guid))
        return false;

    // Already considered this tick?
    if (npc.lastConsidered == getMSTime() / 1000)
        return false;

    // In exhausted district?
    if (IsDistrictExhausted(npc.areaId))
        return false;

    // Meets utility threshold?
    if (npc.utility < sPlayerbotAIConfig.rpgMinNpcUtility)
        return false;

    return true;
}

ObjectGuid NewRpgBaseAction::SelectBestNpcFromCache()
{
    NewRpgInfo& info = botAI->rpgInfo;
    ObjectGuid bestGuid;
    float bestUtility = sPlayerbotAIConfig.rpgMinNpcUtility;
    float bestDist = FLT_MAX;

    for (CachedNpc& npc : info.cachedNpcs)
    {
        if (!ShouldVisit(npc.guid, npc))
            continue;

        npc.lastConsidered = getMSTime() / 1000;

        float dist = npc.fromTravelMgr ?
            bot->GetDistance(npc.pos) :
            (npc.pos.GetMapId() == bot->GetMapId() ? bot->GetDistance2d(npc.pos.GetPositionX(), npc.pos.GetPositionY()) : FLT_MAX);

        if (npc.utility > bestUtility || (npc.utility == bestUtility && dist < bestDist))
        {
            bestUtility = npc.utility;
            bestDist = dist;
            bestGuid = npc.guid;
        }
    }

    return bestGuid;
}

uint32 NewRpgBaseAction::GetNextUnvisitedDistrict()
{
    NewRpgInfo& info = botAI->rpgInfo;

    // Collect all districts from cache
    std::unordered_map<uint32, float> districtCenters; // areaId -> avg distance
    std::unordered_map<uint32, uint32> districtCounts;

    for (CachedNpc const& npc : info.cachedNpcs)
    {
        float dist = npc.fromTravelMgr ?
            bot->GetDistance(npc.pos) : FLT_MAX;
        if (districtCenters.count(npc.areaId))
        {
            districtCenters[npc.areaId] = std::min(districtCenters[npc.areaId], dist);
            districtCounts[npc.areaId]++;
        }
        else
        {
            districtCenters[npc.areaId] = dist;
            districtCounts[npc.areaId] = 1;
        }
    }

    // Find nearest unvisited district
    uint32 bestDistrict = 0;
    float bestDist = FLT_MAX;

    for (auto const& [areaId, dist] : districtCenters)
    {
        if (areaId == info.currentDistrictId)
            continue;

        // Check if in recent visits
        bool recentlyVisited = false;
        for (DistrictVisit const& dv : info.recentDistrictVisits)
        {
            if (dv.areaId == areaId)
            {
                recentlyVisited = true;
                break;
            }
        }
        if (recentlyVisited)
            continue;

        if (dist < bestDist)
        {
            bestDist = dist;
            bestDistrict = areaId;
        }
    }

    return bestDistrict;
}

bool NewRpgBaseAction::IsDistrictExhausted(uint32 areaId)
{
    NewRpgInfo& info = botAI->rpgInfo;

    // Check if district is in recent visits
    for (DistrictVisit const& dv : info.recentDistrictVisits)
    {
        if (dv.areaId == areaId)
        {
            return dv.npcsVisited >= std::min(dv.npcsTotal, sPlayerbotAIConfig.rpgDistrictExhaustThreshold);
        }
    }

    // Check current district visit
    DistrictVisit* current = info.GetCurrentDistrictVisit();
    if (current->areaId == areaId)
    {
        // Count total NPCs in this district from cache
        uint32 totalInDistrict = 0;
        for (CachedNpc const& npc : info.cachedNpcs)
        {
            if (npc.areaId == areaId && npc.utility >= sPlayerbotAIConfig.rpgMinNpcUtility)
                ++totalInDistrict;
        }
        current->npcsTotal = std::max(current->npcsTotal, totalInDistrict);
        return current->npcsVisited >= std::min(current->npcsTotal, sPlayerbotAIConfig.rpgDistrictExhaustThreshold);
    }

    return false;
}

void NewRpgBaseAction::EarlyRemoveDistrict(uint32 areaId)
{
    NewRpgInfo& info = botAI->rpgInfo;
    info.recentDistrictVisits.erase(
        std::remove_if(info.recentDistrictVisits.begin(), info.recentDistrictVisits.end(),
            [areaId](DistrictVisit const& dv) { return dv.areaId == areaId; }),
        info.recentDistrictVisits.end());
}

bool NewRpgBaseAction::DiscoverFlightPath(Creature* flightMaster)
{
    if (!flightMaster || !flightMaster->IsInWorld())
        return false;

    uint32 nodeId = GetTaxiNodeForCreature(flightMaster);
    if (!nodeId)
        return false;

    if (bot->m_taxi.IsTaximaskNodeKnown(nodeId))
        return false; // Already known

    if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} Discovering flight path at node {} ({})",
                  bot->GetName(), nodeId, flightMaster->GetName());
    }

    bot->GetSession()->SendLearnNewTaxiNode(flightMaster);

    // Update cache
    NewRpgInfo& info = botAI->rpgInfo;
    for (CachedNpc& npc : info.cachedNpcs)
    {
        if (npc.guid == flightMaster->GetGUID())
        {
            npc.taxiNodeKnown = true;
            npc.utility = 0.35f; // Lower utility now that node is known
            break;
        }
    }

    if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} Flight path node {} learned successfully",
                  bot->GetName(), nodeId);
    }

    return true;
}

uint32 NewRpgBaseAction::GetTaxiNodeForCreature(Creature* creature)
{
    if (!creature || !creature->IsInWorld())
        return 0;

    uint32 nodeId = sObjectMgr->GetNearestTaxiNode(
        creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(),
        creature->GetMapId(), bot->GetTeamId());

    return nodeId;
}

bool NewRpgBaseAction::HasUsefulVendorItems(Creature* creature)
{
    if (!creature || !creature->IsInWorld())
        return false;

    VendorItemData const* vendorData = creature->GetVendorItems();
    if (!vendorData || vendorData->m_items.empty())
        return false;

    for (VendorItemList::const_iterator itr = vendorData->m_items.begin();
         itr != vendorData->m_items.end(); ++itr)
    {
        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", (*itr)->item);

        // Whitelist: only consider actually useful item types
        // EQUIP - can equip (empty slot or broken current)
        // REPLACE - stat upgrade over current gear
        // BROKEN_EQUIP - replacement for broken item in bags
        // QUEST - needed for active quest
        // SKILL - needed for skill reagents
        // USE - useful consumable or key
        // AMMO - ammo for ranged weapons
        // Exclude: BAD_EQUIP (zero stat value), GUILD_TASK (niche),
        //          DISENCHANT (material only), AH (auction value),
        //          KEEP (already have enough), VENDOR (trash)
        switch (usage)
        {
            case ITEM_USAGE_EQUIP:
            case ITEM_USAGE_REPLACE:
            case ITEM_USAGE_BROKEN_EQUIP:
            case ITEM_USAGE_QUEST:
            case ITEM_USAGE_SKILL:
            case ITEM_USAGE_USE:
            case ITEM_USAGE_AMMO:
                return true;
            default:
                break;
        }
    }
    return false;
}
