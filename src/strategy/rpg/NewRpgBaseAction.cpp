#include "NewRpgBaseAction.h"

#include "BroadcastHelper.h"
#include "ChatHelper.h"
#include "Creature.h"
#include "G3D/Vector2.h"
#include "GameObject.h"
#include "GossipDef.h"
#include "GridTerrainData.h"
#include "IVMapMgr.h"
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
                dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), dest.getMapId(), bot->GetZoneId(),
                zone_name);
        }
        return bot->TeleportTo(dest);
    }

    float dis = bot->GetExactDist(dest);
    if (dis < pathFinderDis)
    {
        return MoveTo(dest.getMapId(), dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), false, false,
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

bool NewRpgBaseAction::MoveRandomNear(float moveStep, MovementPriority priority)
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
                                botAI->SetNextCheckDelay(sPlayerbotAIConfig->globalCoolDown);
                                return false;
                            }

                            if (bot->IsMounted())
                            {
                                bot->Dismount();
                                botAI->SetNextCheckDelay(sPlayerbotAIConfig->globalCoolDown);
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
                botAI->SetNextCheckDelay(sPlayerbotAIConfig->globalCoolDown);
                return false;
            }

            if (bot->IsMounted())
            {
                bot->Dismount();
                botAI->SetNextCheckDelay(sPlayerbotAIConfig->globalCoolDown);
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
                creature->GetCreatureTemplate()->trainer_type == TRAINER_TYPE_CLASS &&
                !bot->IsClass((Classes)creature->GetCreatureTemplate()->trainer_class, CLASS_CONTEXT_CLASS_TRAINER))
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
        return false;

    if (quest->IsSeasonal())
        return false;

    return true;
}

bool NewRpgBaseAction::IsQuestCapableDoing(Quest const* quest)
{
    bool highLevelQuest = bot->GetLevel() + 3 < bot->GetQuestLevel(quest);
    if (highLevelQuest)
        return false;

    // Elite quest and dungeon quest etc
    if (quest->GetType() != 0)
        return false;

    // now we only capable of doing solo quests
    if (quest->GetSuggestedPlayers() >= 2)
        return false;

    return true;
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
                const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
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
        if (botAI->rpgInfo.recentNpcVisits.count(guid))
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
        if (botAI->rpgInfo.recentNpcVisits.count(guid))
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
            if (botAI->rpgInfo.recentNpcVisits.count(guid))
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
        if (botAI->rpgInfo.recentNpcVisits.count(guid))
            continue;  // Skip recently visited

        Creature* creature = ObjectAccessor::GetCreature(*bot, guid);
        if (!creature || !creature->IsInWorld())
            continue;

        if (distanceLimit && bot->GetDistance(creature) > distanceLimit)
            continue;

        float distance = bot->GetExactDist(creature);

        // Priority 1: Riding trainers with GREEN spells (mobility is crucial)
        if (creature->IsTrainer() && creature->IsValidTrainerForPlayer(bot) && 
            creature->GetCreatureTemplate()->trainer_type == TRAINER_TYPE_MOUNTS)
        {
            const TrainerSpellData* trainerSpells = creature->GetTrainerSpells();
            if (trainerSpells)
            {
                for (const auto& [_, tSpell] : trainerSpells->spellList)
                {
                    if (bot->GetTrainerSpellState(&tSpell) == TRAINER_SPELL_GREEN)
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
        }
        
        // Priority 2: Class trainers with GREEN spells
        else if (creature->IsTrainer() && creature->IsValidTrainerForPlayer(bot) && 
                 creature->GetCreatureTemplate()->trainer_type == TRAINER_TYPE_CLASS)
        {
            const TrainerSpellData* trainerSpells = creature->GetTrainerSpells();
            if (trainerSpells)
            {
                for (const auto& [_, tSpell] : trainerSpells->spellList)
                {
                    if (bot->GetTrainerSpellState(&tSpell) == TRAINER_SPELL_GREEN)
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
        }
        
        // Priority 3: Pet trainers with GREEN spells (for hunters)
        else if (creature->IsTrainer() && creature->IsValidTrainerForPlayer(bot) && 
                 creature->GetCreatureTemplate()->trainer_type == TRAINER_TYPE_PETS)
        {
            const TrainerSpellData* trainerSpells = creature->GetTrainerSpells();
            if (trainerSpells)
            {
                for (const auto& [_, tSpell] : trainerSpells->spellList)
                {
                    if (bot->GetTrainerSpellState(&tSpell) == TRAINER_SPELL_GREEN)
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
        }

        // Priority 4: Profession trainers with GREEN spells (secondary professions only)
        else if (creature->IsTrainer() && creature->IsValidTrainerForPlayer(bot) && 
                 creature->GetCreatureTemplate()->trainer_type == TRAINER_TYPE_TRADESKILLS)
        {
            const TrainerSpellData* trainerSpells = creature->GetTrainerSpells();
            if (trainerSpells)
            {
                for (const auto& [_, tSpell] : trainerSpells->spellList)
                {
                    if (bot->GetTrainerSpellState(&tSpell) == TRAINER_SPELL_GREEN)
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

        // Priority 5: Vendors if bags > 50% full
        if (AI_VALUE(uint8, "bag space") > 50 && creature->IsVendor())
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

            if (bot->GetZoneId() != bot->GetMap()->GetZoneId(bot->GetPhaseMask(), dx, dy, dz))
                continue;

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
    const std::vector<WorldLocation>& locs = sRandomPlayerbotMgr->locsPerLevelCache[bot->GetLevel()];
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
    const std::vector<WorldLocation>& locs = IsAlliance(bot->getRace())
                                                 ? sRandomPlayerbotMgr->allianceStarterPerLevelCache[bot->GetLevel()]
                                                 : sRandomPlayerbotMgr->hordeStarterPerLevelCache[bot->GetLevel()];

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

bool NewRpgBaseAction::SelectRandomFlightTaxiNode(ObjectGuid& flightMaster, uint32& fromNode, uint32& toNode)
{
    const std::vector<uint32>& flightMasters = IsAlliance(bot->getRace())
                                                   ? sRandomPlayerbotMgr->allianceFlightMasterCache
                                                   : sRandomPlayerbotMgr->hordeFlightMasterCache;
    Creature* nearestFlightMaster = nullptr;
    for (const uint32& guid : flightMasters)
    {
        Creature* flightMaster = ObjectAccessor::GetSpawnedCreatureByDBGUID(bot->GetMapId(), guid);
        if (!flightMaster)
            continue;

        if (bot->GetMapId() != flightMaster->GetMapId())
            continue;

        if (!nearestFlightMaster || bot->GetDistance(nearestFlightMaster) > bot->GetDistance(flightMaster))
            nearestFlightMaster = flightMaster;
    }
    if (!nearestFlightMaster || bot->GetDistance(nearestFlightMaster) > 500.0f)
        return false;

    fromNode = sObjectMgr->GetNearestTaxiNode(nearestFlightMaster->GetPositionX(), nearestFlightMaster->GetPositionY(),
                                              nearestFlightMaster->GetPositionZ(), nearestFlightMaster->GetMapId(),
                                              bot->GetTeamId());

    if (!fromNode)
        return false;

    std::vector<uint32> availableToNodes;
    for (uint32 i = 1; i < sTaxiNodesStore.GetNumRows(); ++i)
    {
        if (fromNode == i)
            continue;

        TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(i);

        // check map
        if (!node || node->map_id != bot->GetMapId() ||
            (!node->MountCreatureID[bot->GetTeamId() == TEAM_ALLIANCE ? 1 : 0]))  // dk flight
            continue;

        // check taxi node known
        if (!bot->isTaxiCheater() && !bot->m_taxi.IsTaximaskNodeKnown(i))
            continue;

        // check distance by level
        if (!botAI->CheckLocationDistanceByLevel(bot, WorldLocation(node->map_id, node->x, node->y, node->z), false))
            continue;

        // check path
        uint32 path, cost;
        sObjectMgr->GetTaxiPath(fromNode, i, path, cost);
        if (!path)
            continue;

        // check area level
        uint32 nodeZoneId = bot->GetMap()->GetZoneId(bot->GetPhaseMask(), node->x, node->y, node->z);
        bool capital = false;
        if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(nodeZoneId))
        {
            capital = zone->flags & AREA_FLAG_CAPITAL;
        }

        auto itr = sRandomPlayerbotMgr->zone2LevelBracket.find(nodeZoneId);
        if (!capital && itr == sRandomPlayerbotMgr->zone2LevelBracket.end())
            continue;

        if (!capital && (bot->GetLevel() < itr->second.low || bot->GetLevel() > itr->second.high))
            continue;

        availableToNodes.push_back(i);
    }
    if (availableToNodes.empty())
        return false;

    flightMaster = nearestFlightMaster->GetGUID();
    toNode = availableToNodes[urand(0, availableToNodes.size() - 1)];
    if (botAI->HasStrategy("debug rpg", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[New RPG] Bot {} select random flight taxi node from:{} (node {}) to:{} ({} available)",
                  bot->GetName(), flightMaster.GetEntry(), fromNode, toNode, availableToNodes.size());
    }
    return true;
}

bool NewRpgBaseAction::RandomChangeStatus(std::vector<NewRpgStatus> candidateStatus)
{
    std::vector<NewRpgStatus> availableStatus;
    uint32 probSum = 0;
    for (NewRpgStatus status : candidateStatus)
    {
        if (sPlayerbotAIConfig->RpgStatusProbWeight[status] == 0)
            continue;

        if (CheckRpgStatusAvailable(status))
        {
            availableStatus.push_back(status);
            probSum += sPlayerbotAIConfig->RpgStatusProbWeight[status];
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
        accumulate += sPlayerbotAIConfig->RpgStatusProbWeight[status];
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
            if (SelectRandomFlightTaxiNode(flightMaster, fromNode, toNode))
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
            return SelectRandomFlightTaxiNode(flightMaster, fromNode, toNode);
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
