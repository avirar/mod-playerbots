#include "NewRpgBaseAction.h"

#include "BroadcastHelper.h"
#include "ChatHelper.h"
#include "Creature.h"
#include "G3D/Vector2.h"
#include "GameObject.h"
#include "GossipDef.h"
#include "GridTerrainData.h"
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
        // Validate reachability before committing to new destination
        if (!ValidateReachability(dest))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} destination not reachable: ({},{},{})", 
                     bot->GetName(), dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ());
            return false;
        }
        
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
        LOG_DEBUG(
            "playerbots",
            "[New RPG] Teleport {} from ({},{},{},{}) to ({},{},{},{}) as it stuck when moving far - Zone: {} ({})",
            bot->GetName(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(),
            dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), dest.getMapId(), bot->GetZoneId(),
            zone_name);
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
        // Use Floor Z instead of Ground Z for better exploration quest handling
        float floorZ = GetProperFloorHeight(bot, dx, dy, dz);
        if (floorZ != INVALID_HEIGHT && floorZ != VMAP_INVALID_HEIGHT_VALUE)
        {
            dz = floorZ;
        }
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
        
    // Check if we're close enough for interaction - if so, validate LOS
    float currentDistance = bot->GetDistance(object);
    if (currentDistance <= INTERACTION_DISTANCE)
    {
        if (!bot->IsWithinLOSInMap(object))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} NPC/GO at interaction distance but no LOS, trying alternative approach", 
                     bot->GetName());
            
            // Try alternative approach angles for better LOS
            float altX, altY, altZ;
            if (TryAlternativeApproachForLOS(guid, altX, altY, altZ))
            {
                return MoveTo(object->GetMapId(), altX, altY, altZ, false, false, false, true);
            }
            
            // If no alternative found, try to get closer
            distance = std::min(distance, INTERACTION_DISTANCE * 0.5f);
        }
    }
        
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

    // Use NPC-aware height calculation to ensure we move to the same elevation as the target
    float properZ = GetProperFloorHeightNearNPC(bot, x, y, objectZ, guid);
    if (properZ != INVALID_HEIGHT && properZ != VMAP_INVALID_HEIGHT_VALUE)
    {
        z = properZ;
    }

    // Validate collision, but preserve Z coordinate from NPC level
    float validX = x, validY = y, validZ = z;
    if (!object->GetMap()->CheckCollisionAndGetValidCoords(object, objectX, objectY, objectZ, validX, validY, validZ))
    {
        // If collision check fails, try moving closer to the object at its Z level
        x = objectX + cos(angle) * (distance * 0.5f);
        y = objectY + sin(angle) * (distance * 0.5f);
        z = objectZ;
    }
    else
    {
        x = validX;
        y = validY;
        z = validZ;
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
    int attempts = 1;
    while (attempts--)
    {
        float angle = (float)rand_norm() * 2 * static_cast<float>(M_PI);
        float dx = x + distance * cos(angle);
        float dy = y + distance * sin(angle);
        float dz = z;
        // Use Floor Z instead of Ground Z for better exploration quest handling
        float floorZ = GetProperFloorHeight(bot, dx, dy, dz);
        if (floorZ != INVALID_HEIGHT && floorZ != VMAP_INVALID_HEIGHT_VALUE)
        {
            dz = floorZ;
        }

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
    WorldObject* object = ObjectAccessor::GetWorldObject(*bot, guid);
    if (!object || !bot->CanInteractWithQuestGiver(object))
        return false;
        
    // Final LOS check before interaction - only fail if we're close enough to interact
    float distance = bot->GetDistance(object);
    if (distance <= INTERACTION_DISTANCE && !bot->IsWithinLOSInMap(object))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} Cannot interact with NPC/GO {} - no LOS at interaction distance", 
                 bot->GetName(), guid.ToString());
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
            if (botAI->GetMaster())
                botAI->TellMasterNoFacing("Quest accepted " + ChatHelper::FormatQuest(quest));
            BroadcastHelper::BroadcastQuestAccepted(botAI, bot, quest);
            botAI->rpgStatistic.questAccepted++;
            LOG_DEBUG("playerbots", "[New RPG] {} accept quest {}", bot->GetName(), quest->GetQuestId());
        }
        if (status == QUEST_STATUS_COMPLETE && bot->CanRewardQuest(quest, 0, false))
        {
            TurnInQuest(quest, guid);
            if (botAI->GetMaster())
                botAI->TellMasterNoFacing("Quest rewarded " + ChatHelper::FormatQuest(quest));
            BroadcastHelper::BroadcastQuestTurnedIn(botAI, bot, quest);
            botAI->rpgStatistic.questRewarded++;
            LOG_DEBUG("playerbots", "[New RPG] {} turned in quest {}", bot->GetName(), quest->GetQuestId());
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
            LOG_DEBUG("playerbots", "[New RPG] {} drop quest {}", bot->GetName(), questId);
            WorldPacket packet(CMSG_QUESTLOG_REMOVE_QUEST);
            packet << (uint8)i;
            bot->GetSession()->HandleQuestLogRemoveQuest(packet);
            if (botAI->GetMaster())
                botAI->TellMasterNoFacing("Quest dropped " + ChatHelper::FormatQuest(quest));
            botAI->rpgStatistic.questDropped++;
            dropped++;
        }
    }

    // drop more than 8 quests at once to avoid repeated accept and drop
    if (dropped >= 8)
        return true;

    // remove festival/class quests and quests in different zone
    for (uint16 i = 0; i < MAX_QUEST_LOG_SIZE; ++i)
    {
        uint32 questId = bot->GetQuestSlotQuestId(i);
        if (!questId)
            continue;

        const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
        if (quest->GetZoneOrSort() < 0 || (quest->GetZoneOrSort() > 0 && quest->GetZoneOrSort() != bot->GetZoneId()))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} drop quest {}", bot->GetName(), questId);
            WorldPacket packet(CMSG_QUESTLOG_REMOVE_QUEST);
            packet << (uint8)i;
            bot->GetSession()->HandleQuestLogRemoveQuest(packet);
            if (botAI->GetMaster())
                botAI->TellMasterNoFacing("Quest dropped " + ChatHelper::FormatQuest(quest));
            botAI->rpgStatistic.questDropped++;
            dropped++;
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
        LOG_DEBUG("playerbots", "[New RPG] {} drop quest {}", bot->GetName(), questId);
        WorldPacket packet(CMSG_QUESTLOG_REMOVE_QUEST);
        packet << (uint8)i;
        bot->GetSession()->HandleQuestLogRemoveQuest(packet);
        if (botAI->GetMaster())
            botAI->TellMasterNoFacing("Quest dropped " + ChatHelper::FormatQuest(quest));
        botAI->rpgStatistic.questDropped++;
    }

    return true;
}

bool NewRpgBaseAction::SearchQuestGiverAndAcceptOrReward()
{
    OrganizeQuestLog();
    if (ObjectGuid npcOrGo = ChooseNpcOrGameObjectToInteract(true, 80.0f))
    {
        WorldObject* object = ObjectAccessor::GetWorldObject(*bot, npcOrGo);
        if (bot->CanInteractWithQuestGiver(object))
        {
            InteractWithNpcOrGameObjectForQuest(npcOrGo);
            ForceToWait(5000);
            return true;
        }
        return MoveWorldObjectTo(npcOrGo);
    }
    return false;
}

ObjectGuid NewRpgBaseAction::ChooseNpcOrGameObjectToInteract(bool questgiverOnly, float distanceLimit)
{
    // First try LOS-based search for nearby NPCs
    GuidVector possibleTargets = AI_VALUE(GuidVector, "possible new rpg targets");
    GuidVector possibleGameObjects = AI_VALUE(GuidVector, "possible new rpg game objects");

    // If no targets found with LOS, use non-LOS search as fallback
    if (possibleTargets.empty())
    {
        possibleTargets = AI_VALUE(GuidVector, "possible new rpg targets no los");
        LOG_DEBUG("playerbots", "[New RPG] {} Using non-LOS search fallback, found {} targets", 
                 bot->GetName(), possibleTargets.size());
    }

    if (possibleTargets.empty() && possibleGameObjects.empty())
        return ObjectGuid();

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

        // Check elevation difference - prefer NPCs on similar elevation
        float elevationPenalty = 0.0f;
        if (IsNPCOnDifferentElevation(object, bot->GetPositionZ(), 15.0f))
        {
            elevationPenalty = 50.0f; // Add distance penalty for different elevations
        }

        if (CanInteractWithQuestGiver(object) && HasQuestToAcceptOrReward(object))
        {
            float adjustedDistance = bot->GetExactDist(object) + elevationPenalty;
            if (adjustedDistance < nearestDistance)
            {
                nearestObject = object;
                nearestDistance = adjustedDistance;
            }
            break;
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

        // Check elevation difference for GameObjects too
        float elevationPenalty = 0.0f;
        if (IsNPCOnDifferentElevation(object, bot->GetPositionZ(), 15.0f))
        {
            elevationPenalty = 50.0f;
        }

        if (CanInteractWithQuestGiver(object) && HasQuestToAcceptOrReward(object))
        {
            float adjustedDistance = bot->GetExactDist(object) + elevationPenalty;
            if (adjustedDistance < nearestDistance)
            {
                nearestObject = object;
                nearestDistance = adjustedDistance;
            }
            break;
        }
    }

    if (nearestObject)
        return nearestObject->GetGUID();

    // No questgiver to accept or reward
    if (questgiverOnly)
        return ObjectGuid();
    // Priority 2–5: Trainers, Vendors, Repairs
    for (ObjectGuid& guid : possibleTargets)
    {
        if (botAI->rpgInfo.recentNpcVisits.count(guid))
            continue;  // Skip recently visited

        Creature* creature = ObjectAccessor::GetCreature(*bot, guid);
        if (!creature || !creature->IsInWorld())
            continue;

        if (distanceLimit && bot->GetDistance(creature) > distanceLimit)
            continue;

        // Class trainer with GREEN spells
        if (creature->IsTrainer() && creature->IsValidTrainerForPlayer(bot) && creature->GetCreatureTemplate()->trainer_type == TRAINER_TYPE_CLASS)
        {
            const TrainerSpellData* trainerSpells = creature->GetTrainerSpells();
            if (trainerSpells)
            {
                for (const auto& [_, tSpell] : trainerSpells->spellList)
                {
                    if (bot->GetTrainerSpellState(&tSpell) == TRAINER_SPELL_GREEN)
                        return creature->GetGUID();
                }
            }
        }

        /*
        // Profession trainer with GREEN spells (must know the profession spell)
        if (creature->IsTrainer() && creature->IsValidTrainerForPlayer(bot))
        {
            const TrainerSpellData* trainerSpells = creature->GetTrainerSpells();
            if (trainerSpells && creature->GetCreatureTemplate()->trainer_type == TRAINER_TYPE_TRADESKILLS)
            {
                for (const auto& [_, tSpell] : trainerSpells->spellList)
                {
                    if (bot->GetTrainerSpellState(&tSpell) == TRAINER_SPELL_GREEN)
                        return creature->GetGUID();
                }
            }
        }
        */

        // Vendor if bags > 50% full
        if (AI_VALUE(uint8, "bag space") > 80 && creature->IsVendor())
            return creature->GetGUID();

        // Repair if any item < 50% durability
        if (AI_VALUE(uint8, "durability") < 50 &&
            creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_REPAIR))
            return creature->GetGUID();
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

            float dz = GetProperFloorHeight(bot, dx, dy, MAX_HEIGHT);

            if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
                continue;

            if (bot->GetZoneId() != bot->GetMap()->GetZoneId(bot->GetPhaseMask(), dx, dy, dz))
                continue;

            poiInfo.push_back({{dx, dy}, qPoi.ObjectiveIndex});
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
            LOG_DEBUG("playerbots", "[New RPG] {} POI rejected: map mismatch (Bot={}, POI={})", bot->GetName(), bot->GetMapId(), qPoi.MapId);
            continue;
        }
    
        bool inComplete = false;
        LOG_DEBUG("playerbots", "[New RPG] {} Checking for Exploration quest objective", bot->GetName());
        if (qPoi.ObjectiveIndex == 16 && (quest->GetFlags() & QUEST_FLAGS_EXPLORATION))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} Exploration quest objective detected, checking for Area Triggers", bot->GetName());
            // Query areatrigger_involvedrelation to get the trigger ID
            QueryResult result = WorldDatabase.Query("SELECT id FROM areatrigger_involvedrelation WHERE quest = {}", questId);
            if (!result)
            {
                LOG_DEBUG("playerbots", "[New RPG] {} No area trigger found for quest {}", bot->GetName(), questId);
                continue;
            }

            Field* fields = result->Fetch();
            uint32 triggerId = fields[0].Get<uint32>();

            // Now get the actual area trigger data
            result = WorldDatabase.Query("SELECT x, y, z, radius FROM areatrigger WHERE entry = {}", triggerId);
            if (!result)
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Area trigger data not found for ID {}", bot->GetName(), triggerId);
                continue;
            }

            fields = result->Fetch();
            float x = fields[0].Get<float>();
            float y = fields[1].Get<float>();
            float z = fields[2].Get<float>();
            float radius = fields[3].Get<float>();
            LOG_DEBUG("playerbots", "[New RPG] {} Exploration Area Trigger data retrieved from DB", bot->GetName());
            // Use the center of the area trigger as POI
            float dz = GetProperFloorHeightNearNPC(bot, x, y, z);
            if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Invalid Z for area trigger at ({}, {})", bot->GetName(), x, y);
                continue;
            }

            uint32 botZone = bot->GetZoneId();
            uint32 poiZone = bot->GetMap()->GetZoneId(bot->GetPhaseMask(), x, y, dz);

            if (botZone != poiZone)
            {
                LOG_DEBUG("playerbots", "[New RPG] {} Zone mismatch for area trigger POI", bot->GetName());
                continue;
            }

            LOG_DEBUG("playerbots", "[New RPG] {} POI accepted (AreaTrigger): {} at ({}, {}, {})", bot->GetName(), qPoi.ObjectiveIndex, x, y, dz);
            poiInfo.push_back({{x, y}, qPoi.ObjectiveIndex});
            inComplete = true;
        }
        else
        {
            for (uint32 objective : incompleteObjectiveIdx)
            {
                if (qPoi.ObjectiveIndex == objective)
                {
                    LOG_DEBUG("playerbots", "[New RPG] {} POI ObjectiveIndex {} matched an incomplete objective", bot->GetName(), qPoi.ObjectiveIndex);
                    inComplete = true;
                    break;
                }
            }
        }
   
        if (!inComplete)
        {
            LOG_DEBUG("playerbots", "[New RPG] {} POI rejected: ObjectiveIndex {} not in incomplete list", bot->GetName(), qPoi.ObjectiveIndex);
            continue;
        }
    
        if (qPoi.points.empty())
        {
            LOG_DEBUG("playerbots", "[New RPG] {} POI rejected: no polygon points", bot->GetName());
            continue;
        }
    
        // Instead of calculating the center point, select a random point from the polygon
        float randomX = 0, randomY = 0;
        if (!GetRandomPointInPolygon(qPoi.points, randomX, randomY))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} Failed to generate random point in polygon", bot->GetName());
            continue;
        }
        
        // Use the random point for all subsequent operations and look for nearby NPCs for Z reference
        ObjectGuid nearbyNPC = FindNearbyQuestNPC(questId, randomX, randomY, 100.0f);
        float dz = GetProperFloorHeightNearNPC(bot, randomX, randomY, MAX_HEIGHT, nearbyNPC);
        
        if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
        {
            LOG_DEBUG("playerbots", "[New RPG] {} POI rejected: invalid Z at ({}, {})", bot->GetName(), randomX, randomY);
            continue;
        }
        
        uint32 botZone = bot->GetZoneId();
        uint32 poiZone = bot->GetMap()->GetZoneId(bot->GetPhaseMask(), randomX, randomY, dz);
        
        if (botZone != poiZone)
        {
            LOG_DEBUG("playerbots", "[New RPG] {} POI rejected: zone mismatch (Bot={}, POI={})", bot->GetName(), botZone, poiZone);
            continue;
        }
        
        LOG_DEBUG("playerbots", "[New RPG] {} POI accepted: {} at ({}, {}, {})", bot->GetName(), qPoi.ObjectiveIndex, randomX, randomY, dz);
        poiInfo.push_back({{randomX, randomY}, qPoi.ObjectiveIndex});
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
    LOG_DEBUG("playerbots", "[New RPG] Bot {} select random grind pos Map:{} X:{} Y:{} Z:{} ({}+{} available in {})",
              bot->GetName(), dest.GetMapId(), dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(),
              hi_prepared_locs.size(), lo_prepared_locs.size() - hi_prepared_locs.size(), locs.size());
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
        uint32 idx = urand(0, prepared_locs.size() - 1);
        dest = prepared_locs[idx];
    }
    LOG_DEBUG("playerbots", "[New RPG] Bot {} select random inn keeper pos Map:{} X:{} Y:{} Z:{} ({} available in {})",
              bot->GetName(), dest.GetMapId(), dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(),
              prepared_locs.size(), locs.size());
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
    LOG_DEBUG("playerbots", "[New RPG] Bot {} select random flight taxi node from:{} (node {}) to:{} ({} available)",
              bot->GetName(), flightMaster.GetEntry(), fromNode, toNode, availableToNodes.size());
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

float NewRpgBaseAction::GetProperFloorHeight(Player* bot, float dx, float dy, float dz)
{
    float groundHeight = bot->GetMap()->GetGridHeight(dx, dy);
    
    // Get VMAP height for more accurate positioning
    float vmapHeight = bot->GetMap()->GetHeight(dx, dy, dz + 10.0f, true, 50.0f);
    
    // Use VMAP height if valid, otherwise fall back to ground height
    if (vmapHeight > INVALID_HEIGHT && vmapHeight != VMAP_INVALID_HEIGHT_VALUE)
    {
        return vmapHeight;
    }
    
    return groundHeight;
}

float NewRpgBaseAction::GetProperFloorHeightNearNPC(Player* bot, float dx, float dy, float referenceZ, ObjectGuid npcGuid)
{
    // If we have a specific NPC, try to match its elevation
    if (!npcGuid.IsEmpty())
    {
        WorldObject* npc = ObjectAccessor::GetWorldObject(*bot, npcGuid);
        if (npc)
        {
            float npcZ = npc->GetPositionZ();
            float distance2D = bot->GetDistance2d(npc);
            
            // If we're moving close to the NPC, try to match its Z level
            if (distance2D < 50.0f)
            {
                // Check if the NPC's Z level is reachable from our position
                float heightDiff = abs(npcZ - bot->GetPositionZ());
                if (heightDiff < 100.0f) // Reasonable elevation difference
                {
                    // Use NPC's Z level with slight VMAP adjustment
                    float vmapHeight = bot->GetMap()->GetHeight(dx, dy, npcZ + 5.0f, true, 10.0f);
                    if (vmapHeight > INVALID_HEIGHT && vmapHeight != VMAP_INVALID_HEIGHT_VALUE)
                    {
                        return vmapHeight;
                    }
                    return npcZ;
                }
            }
        }
    }
    
    // If no NPC context or NPC method failed, use reference Z with validation
    if (referenceZ > INVALID_HEIGHT && referenceZ != VMAP_INVALID_HEIGHT_VALUE)
    {
        float vmapHeight = bot->GetMap()->GetHeight(dx, dy, referenceZ + 5.0f, true, 10.0f);
        if (vmapHeight > INVALID_HEIGHT && vmapHeight != VMAP_INVALID_HEIGHT_VALUE)
        {
            return vmapHeight;
        }
        return referenceZ;
    }
    
    // Fallback to standard height calculation
    return GetProperFloorHeight(bot, dx, dy, bot->GetPositionZ());
}

bool NewRpgBaseAction::IsNPCOnDifferentElevation(WorldObject* npc, float botZ, float tolerance)
{
    if (!npc)
        return false;
        
    float heightDiff = abs(npc->GetPositionZ() - botZ);
    return heightDiff > tolerance;
}

bool NewRpgBaseAction::ValidateReachability(WorldPosition dest, ObjectGuid targetNpc)
{
    if (dest == WorldPosition())
        return false;
        
    // Basic distance check
    float distance = bot->GetDistance(dest);
    if (distance > 2500.0f)
        return false;
        
    // If we have a target NPC, check if the destination is on a reasonable elevation
    if (!targetNpc.IsEmpty())
    {
        WorldObject* npc = ObjectAccessor::GetWorldObject(*bot, targetNpc);
        if (npc)
        {
            float npcZ = npc->GetPositionZ();
            float destZ = dest.GetPositionZ();
            float elevationDiff = abs(destZ - npcZ);
            
            // If destination is far from NPC elevation, it might not be reachable
            if (elevationDiff > 50.0f)
            {
                LOG_DEBUG("playerbots", "[New RPG] {} destination Z ({}) too far from NPC Z ({}), elevation diff: {}",
                         bot->GetName(), destZ, npcZ, elevationDiff);
                return false;
            }
        }
    }
    
    // Basic pathfinding check for short distances
    if (distance < pathFinderDis)
    {
        PathGenerator path(bot);
        path.CalculatePath(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ());
        PathType type = path.GetPathType();
        uint32 typeOk = PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_FARFROMPOLY;
        return !(type & (~typeOk));
    }
    
    return true; // Assume reachable for long distances (will be handled by MoveFarTo logic)
}

ObjectGuid NewRpgBaseAction::FindNearbyQuestNPC(uint32 questId, float x, float y, float searchRadius)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return ObjectGuid();

    // Use non-LOS search for quest NPCs to find NPCs in multi-level areas
    GuidVector possibleTargets = AI_VALUE(GuidVector, "possible new rpg targets no los");
    ObjectGuid closestNPC;
    float closestDistance = searchRadius;

    for (ObjectGuid& guid : possibleTargets)
    {
        WorldObject* object = ObjectAccessor::GetWorldObject(*bot, guid);
        if (!object || !object->IsInWorld())
            continue;

        Creature* creature = object->ToCreature();
        if (!creature)
            continue;

        float distance2D = sqrt((creature->GetPositionX() - x) * (creature->GetPositionX() - x) +
                               (creature->GetPositionY() - y) * (creature->GetPositionY() - y));

        if (distance2D < closestDistance)
        {
            // Check if this NPC is related to the quest (quest giver, objective, etc.)
            if (CanInteractWithQuestGiver(creature))
            {
                closestNPC = guid;
                closestDistance = distance2D;
            }
        }
    }

    return closestNPC;
}

bool NewRpgBaseAction::TryAlternativeApproachForLOS(ObjectGuid guid, float& bestX, float& bestY, float& bestZ)
{
    WorldObject* object = ObjectAccessor::GetWorldObject(*bot, guid);
    if (!object)
        return false;
        
    float objectX = object->GetPositionX();
    float objectY = object->GetPositionY();
    float objectZ = object->GetPositionZ();
    
    // Try different approach angles around the NPC/GO to find LOS
    float testDistance = INTERACTION_DISTANCE * 0.8f;
    int attempts = 8; // Test 8 different angles around the object
    
    for (int i = 0; i < attempts; ++i)
    {
        float angle = (2.0f * M_PI / attempts) * i;
        float testX = objectX + cos(angle) * testDistance;
        float testY = objectY + sin(angle) * testDistance;
        float testZ = GetProperFloorHeightNearNPC(bot, testX, testY, objectZ, guid);
        
        if (testZ == INVALID_HEIGHT || testZ == VMAP_INVALID_HEIGHT_VALUE)
            continue;
            
        // Create temporary position to test LOS from
        WorldPosition testPos(bot->GetMapId(), testX, testY, testZ);
        
        // Check if this position would have LOS to the object
        // We can't easily test LOS from arbitrary positions, so just check if it's reachable
        if (ValidateReachability(testPos, guid))
        {
            bestX = testX;
            bestY = testY;
            bestZ = testZ;
            LOG_DEBUG("playerbots", "[New RPG] {} Found alternative approach angle {} for better LOS to {}", 
                     bot->GetName(), i, guid.ToString());
            return true;
        }
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
