/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgDoQuest.h"

#include <algorithm>
#include <cfloat>
#include <unordered_map>

#include "BroadcastHelper.h"
#include "CellImpl.h"
#include "ChatHelper.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "G3D/Vector2.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "GridTerrainData.h"
#include "IVMapMgr.h"
#include "Item.h"
#include "LastMovementValue.h"
#include "LootMgr.h"
#include "LootObjectStack.h"
#include "NearestGameObjects.h"
#include "NewRpgInfo.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "QuestPoiZStore.h"
#include "Random.h"
#include "SharedDefines.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "TravelMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

bool StartRpgDoQuestAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    std::string const text = event.getParam();
    PlayerbotChatHandler ch(owner);
    uint32 questId = ch.extractQuestId(text);
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (quest)
    {
        botAI->rpgInfo.ChangeToDoQuest(questId, quest);
        bot->Whisper("Start to do quest " + std::to_string(questId), LANG_UNIVERSAL, owner);
        return true;
    }
    bot->Whisper("Invalid quest " + text, LANG_UNIVERSAL, owner);
    return false;
}

Unit* NewRpgAttackQuestTargetAction::GetTarget()
{
    auto* data = std::get_if<NewRpgInfo::DoQuest>(&botAI->rpgInfo.data);
    if (!data || !data->targetGuid.IsAnyTypeCreature())
        return nullptr;

    return botAI->GetUnit(data->targetGuid);
}

bool NewRpgAttackQuestTargetAction::Execute(Event /*event*/)
{
    verbose = false;

    Unit* target = GetTarget();
    if (!target || !target->IsInWorld() || !target->IsAlive())
        return false;

    if (bot->IsFriendlyTo(target) || !bot->IsValidAttackTarget(target))
        return false;

    bot->SetSelection(target->GetGUID());
    context->GetValue<Unit*>("current target")->Set(target);

    // Halt even when the attack is deferred: the in-flight spline ends on the mob itself.
    LastMovement& lastMovement = AI_VALUE(LastMovement&, "last movement");
    bool moveControlled = bot->GetMotionMaster()->GetMotionSlotType(MOTION_SLOT_CONTROLLED) != NULL_MOTION_TYPE;
    if (bot->isMoving() && lastMovement.priority < MovementPriority::MOVEMENT_COMBAT && !moveControlled)
    {
        lastMovement.clear();
        bot->GetMotionMaster()->Clear(false);
        bot->StopMoving();
    }

    if (bot->IsMounted())
    {
        bot->Dismount();
        return true;
    }

    bool result = Attack(target);
    LOG_DEBUG("playerbots", "[New RPG] {} attack quest target {} ({}): {}", bot->GetName(), target->GetName(),
              target->GetGUID().ToString(), result ? "engaged" : "refused");
    return result;
}

bool NewRpgDoQuestAction::Execute(Event /*event*/)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    NewRpgInfo& info = botAI->rpgInfo;
    auto* dataPtr = std::get_if<NewRpgInfo::DoQuest>(&info.data);
    if (!dataPtr)
        return false;
    auto& data = *dataPtr;
    uint32 questId = data.questId;
    uint8 questStatus = bot->GetQuestStatus(questId);
    switch (questStatus)
    {
        case QUEST_STATUS_INCOMPLETE:
            return DoIncompleteQuest(data);
        case QUEST_STATUS_COMPLETE:
            return DoCompletedQuest(data);
        default:
            break;
    }
    info.ChangeToIdle();
    return true;
}

QuestStatusData const* NewRpgDoQuestAction::GetQuestStatus(uint32 questId) const
{
    auto const& statusMap = bot->getQuestStatusMap();
    auto it = statusMap.find(questId);
    return it == statusMap.end() ? nullptr : &it->second;
}

bool NewRpgDoQuestAction::IsObjectiveCompleted(NewRpgInfo::DoQuest const& data) const
{
    // -1 while heading to turn-in; a quest that reverted to incomplete needs a fresh objective.
    if (data.objectiveIdx < 0)
        return true;

    Quest const* quest = sObjectMgr->GetQuestTemplate(data.questId);
    QuestStatusData const* q_status = GetQuestStatus(data.questId);
    if (!quest || !q_status)
        return true;

    int32 idx = data.objectiveIdx;
    if (idx < QUEST_OBJECTIVES_COUNT)
        return q_status->CreatureOrGOCount[idx] >= quest->RequiredNpcOrGoCount[idx];

    if (idx < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
        return q_status->ItemCount[idx - QUEST_OBJECTIVES_COUNT] >=
               quest->RequiredItemCount[idx - QUEST_OBJECTIVES_COUNT];

    return true;
}

bool NewRpgDoQuestAction::HasObjectiveProgress(NewRpgInfo::DoQuest const& data) const
{
    if (data.objectiveIdx < 0)
        return false;

    Quest const* quest = sObjectMgr->GetQuestTemplate(data.questId);
    QuestStatusData const* q_status = GetQuestStatus(data.questId);
    if (!quest || !q_status)
        return false;

    int32 idx = data.objectiveIdx;
    if (idx < QUEST_OBJECTIVES_COUNT)
        return q_status->CreatureOrGOCount[idx] != 0 && quest->RequiredNpcOrGoCount[idx];

    if (idx < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
        return q_status->ItemCount[idx - QUEST_OBJECTIVES_COUNT] != 0 &&
               quest->RequiredItemCount[idx - QUEST_OBJECTIVES_COUNT];

    return false;
}

void NewRpgDoQuestAction::ClearTarget(NewRpgInfo::DoQuest& data)
{
    if (data.targetGuid.IsGameObject())
    {
        LootObject lootTarget = AI_VALUE(LootObject, "loot target");
        if (lootTarget.guid == data.targetGuid)
            context->GetValue<LootObject>("loot target")->Set(LootObject());
    }
    data.targetGuid = ObjectGuid::Empty;
    data.targetPos = WorldPosition();
    data.targetSince = 0;
    data.lastEngage = 0;
}

void NewRpgDoQuestAction::ResetObjectiveSpawn(NewRpgInfo::DoQuest& data)
{
    data.spawnGuid = 0;
    data.pos = WorldPosition();
    data.objectiveIdx = 0;
    data.lastReachPOI = 0;
    data.spawnSince = 0;
    data.lastScan = 0;
    data.poiFallback = false;
    ClearTarget(data);
}

bool NewRpgDoQuestAction::SelectObjectiveSpawn(NewRpgInfo::DoQuest& data)
{
    Quest const* quest = data.quest;
    QuestStatusData const* q_status = GetQuestStatus(data.questId);
    if (!quest || !q_status)
        return false;

    GuidPosition const* best = nullptr;
    int32 bestObjective = 0;
    float bestDist = FLT_MAX;

    for (int32 idx = 0; idx < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT; ++idx)
    {
        if (idx < QUEST_OBJECTIVES_COUNT)
        {
            if (!quest->RequiredNpcOrGo[idx] || q_status->CreatureOrGOCount[idx] >= quest->RequiredNpcOrGoCount[idx])
                continue;
        }
        else
        {
            int32 const item = idx - QUEST_OBJECTIVES_COUNT;
            if (!quest->RequiredItemId[item] || !quest->RequiredItemCount[item] ||
                q_status->ItemCount[item] >= quest->RequiredItemCount[item])
                continue;
        }

        for (int32 entry : GetQuestObjectiveEntries(data.questId, static_cast<uint32>(1 << idx)))
        {
            // A credit marker's spawn is never a destination; its kill sources are indexed instead.
            if (entry > 0 && !IsEngageableCreatureEntry(uint32(entry)))
                continue;

            for (GuidPosition const& pos : GetEntrySpawns(entry, bot->GetMapId()))
            {
                if (data.triedSpawns.count(pos.GetRawValue()))
                    continue;

                // Spawns floating far above every floor or water surface cannot be walked to.
                float const floorZ =
                    std::max(bot->GetMap()->GetHeight(pos.GetPositionX(), pos.GetPositionY(), MAX_HEIGHT),
                             bot->GetMap()->GetWaterLevel(pos.GetPositionX(), pos.GetPositionY()));
                if ((floorZ == INVALID_HEIGHT || floorZ == VMAP_INVALID_HEIGHT_VALUE ||
                     pos.GetPositionZ() - floorZ > spawnFloorGap))
                    continue;

                float const dist = bot->GetDistance(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
                if (dist < bestDist)
                {
                    bestDist = dist;
                    best = &pos;
                    bestObjective = idx;
                }
            }
        }
    }

    if (!best)
    {
        // All candidates tried (or none): let the explore/POI fallback take over instead of
        // re-picking the same spawns every tick.
        data.triedSpawns.clear();
        return false;
    }

    data.spawnGuid = best->GetRawValue();
    data.objectiveIdx = bestObjective;
    data.pos = *best;
    data.lastReachPOI = 0;
    data.spawnSince = getMSTime();
    data.lastScan = 0;
    return true;
}

bool NewRpgDoQuestAction::SelectObjectivePoi(NewRpgInfo::DoQuest& data)
{
    std::vector<POIInfo> poiInfo;
    if (!GetQuestPOIPosAndObjectiveIdx(data.questId, poiInfo))
        return false;

    POIInfo const& poi = poiInfo[urand(0, poiInfo.size() - 1)];
    data.spawnGuid = 0;
    data.objectiveIdx = poi.objectiveIdx;
    data.pos = WorldPosition(bot->GetMapId(), poi.pos.x, poi.pos.y, poi.z);
    data.poiFallback = true;
    data.lastReachPOI = 0;
    data.spawnSince = getMSTime();
    data.lastScan = 0;
    return true;
}

// Quest POI rows carry x/y only, so the turn-in goes by the ender's spawn point.
bool NewRpgDoQuestAction::SelectQuestEnder(NewRpgInfo::DoQuest& data)
{
    GuidPosition const* best = nullptr;
    float bestDist = FLT_MAX;
    for (int32 entry : GetQuestEnderEntries(data.questId))
    {
        for (GuidPosition const& pos : GetEntrySpawns(entry, bot->GetMapId()))
        {
            if (data.triedSpawns.count(pos.GetRawValue()))
                continue;

            float const dist = bot->GetDistance(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
            if (dist < bestDist)
            {
                bestDist = dist;
                best = &pos;
            }
        }
    }

    if (!best)
        return false;

    data.spawnGuid = best->GetRawValue();
    data.pos = *best;
    return true;
}

bool NewRpgDoQuestAction::IsIncompleteObjectiveEntry(NewRpgInfo::DoQuest const& data, uint32 entry) const
{
    QuestStatusData const* q_status = GetQuestStatus(data.questId);
    if (!data.quest || !q_status)
        return false;

    for (int32 idx = 0; idx < QUEST_OBJECTIVES_COUNT; ++idx)
        if (data.quest->RequiredNpcOrGo[idx] == int32(entry) &&
            q_status->CreatureOrGOCount[idx] < data.quest->RequiredNpcOrGoCount[idx])
            return true;

    return false;
}

bool NewRpgDoQuestAction::IsWrittenOff(NewRpgInfo::DoQuest const& data, ObjectGuid guid) const
{
    auto it = data.visited.find(guid);
    return it != data.visited.end() && GetMSTimeDiffToNow(it->second) < writeOffTime;
}

bool NewRpgDoQuestAction::IsValidCreatureTarget(NewRpgInfo::DoQuest const& data, Creature* creature) const
{
    if (!creature || !creature->IsInWorld() || !creature->IsAlive())
        return false;

    // Credit markers cannot be engaged; only their kill sources are legitimate targets.
    if (creature->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE) || creature->HasUnitFlag(UNIT_FLAG_IMMUNE_TO_PC))
        return false;

    if (IsWrittenOff(data, creature->GetGUID()))
        return false;

    if (creature->hasLootRecipient() && !creature->isTappedBy(bot))
        return false;

    return true;
}

bool NewRpgDoQuestAction::IsValidGoTarget(NewRpgInfo::DoQuest const& data, GameObject* go) const
{
    if (!go || !go->IsInWorld() || !go->isSpawned())
        return false;

    if (go->GetGoState() != GO_STATE_READY)
        return false;

    if (IsWrittenOff(data, go->GetGUID()))
        return false;

    return true;
}

bool NewRpgDoQuestAction::HasNeededQuestItem(GameObject* go) const
{
    GameObjectQuestItemList const* items = sObjectMgr->GetGameObjectQuestItemList(go->GetEntry());
    if (!items)
        return false;

    for (uint32 itemId : *items)
        if (LootObject::IsNeededForQuest(bot, itemId))
            return true;

    return false;
}

// Bounded: a despawned corpse leaves "loot target" set with nothing to clear it.
bool NewRpgDoQuestAction::HoldForLoot(NewRpgInfo::DoQuest& data)
{
    if (AI_VALUE(LootObject, "loot target").IsEmpty())
    {
        data.lootHoldSince = 0;
        return false;
    }

    if (!data.lootHoldSince)
        data.lootHoldSince = getMSTime();

    return GetMSTimeDiffToNow(data.lootHoldSince) < lootHoldTime;
}

void NewRpgDoQuestAction::WriteOffTarget(NewRpgInfo::DoQuest& data)
{
    if (data.targetGuid)
        data.visited[data.targetGuid] = getMSTime();
    ClearTarget(data);
}

WorldObject* NewRpgDoQuestAction::ScanEntryObjectives(NewRpgInfo::DoQuest const& data, float scanRange) const
{
    QuestStatusData const* q_status = GetQuestStatus(data.questId);
    if (!q_status)
        return nullptr;

    WorldObject* nearest = nullptr;
    float nearestDist = scanRange;
    auto keepNearest = [&](WorldObject* obj)
    {
        float const dist = bot->GetDistance(obj);
        if (dist < nearestDist)
        {
            nearestDist = dist;
            nearest = obj;
        }
    };

    std::vector<uint32> creatureEntries;
    std::vector<uint32> goEntries;
    for (int32 idx = 0; idx < QUEST_OBJECTIVES_COUNT; ++idx)
    {
        int32 const entry = data.quest->RequiredNpcOrGo[idx];
        if (!entry || q_status->CreatureOrGOCount[idx] >= data.quest->RequiredNpcOrGoCount[idx])
            continue;

        if (entry > 0)
        {
            if (IsEngageableCreatureEntry(uint32(entry)))
                creatureEntries.push_back(uint32(entry));

            std::vector<uint32> const& sources = GetQuestKillSources(data.questId, uint32(idx));
            creatureEntries.insert(creatureEntries.end(), sources.begin(), sources.end());
        }
        else
        {
            goEntries.push_back(static_cast<uint32>(-entry));
        }
    }

    if (!creatureEntries.empty())
    {
        std::list<Creature*> creatures;
        bot->GetCreatureListWithEntryInGrid(creatures, creatureEntries, scanRange);
        for (Creature* creature : creatures)
            if (IsValidCreatureTarget(data, creature))
                keepNearest(creature);
    }

    if (!goEntries.empty())
    {
        std::list<GameObject*> gos;
        bot->GetGameObjectListWithEntryInGrid(gos, goEntries, scanRange);
        for (GameObject* go : gos)
            if (IsValidGoTarget(data, go))
                keepNearest(go);
    }

    return nearest;
}

WorldObject* NewRpgDoQuestAction::ScanItemObjective(NewRpgInfo::DoQuest const& data, float scanRange) const
{
    std::vector<int32> const* craftSources = nullptr;
    if (!GetQuestCraftReagents(data.questId, uint32(data.objectiveIdx) - QUEST_OBJECTIVES_COUNT).empty())
        craftSources = &GetQuestObjectiveEntries(data.questId, 1u << data.objectiveIdx);

    std::vector<int32> const& creationSources =
        GetQuestCreationSources(data.questId, uint32(data.objectiveIdx) - QUEST_OBJECTIVES_COUNT);

    WorldObject* nearest = nullptr;
    float nearestDist = scanRange;
    auto keepNearest = [&](WorldObject* obj)
    {
        float const dist = bot->GetDistance(obj);
        if (dist < nearestDist)
        {
            nearestDist = dist;
            nearest = obj;
        }
    };

    std::unordered_set<uint32> const& transformSources = GetQuestTransformSources(data.questId);
    std::unordered_map<uint32, bool> questLoot;
    std::list<Unit*> units;
    Acore::AnyUnitInObjectRangeCheck check(bot, scanRange);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, units, check);
    Cell::VisitObjects(bot, searcher, scanRange);
    for (Unit* unit : units)
    {
        Creature* creature = unit->ToCreature();
        if (!creature)
            continue;

        // A creation source is the cast-spell's own condition target; alive or corpse both work
        // (a live one is fought into the corpse state the spell demands).
        if (std::find(creationSources.begin(), creationSources.end(), int32(creature->GetEntry())) !=
            creationSources.end())
        {
            if (!IsWrittenOff(data, creature->GetGUID()))
                keepNearest(creature);
            continue;
        }

        if (!IsValidCreatureTarget(data, creature))
            continue;

        uint32 lootId = creature->GetCreatureTemplate()->lootid;
        bool const dropsReagent = craftSources && std::find(craftSources->begin(), craftSources->end(),
                                                            int32(creature->GetEntry())) != craftSources->end();
        bool dropsWanted = false;
        if (!dropsReagent && lootId)
        {
            auto cached = questLoot.find(lootId);
            if (cached == questLoot.end())
                cached = questLoot.emplace(lootId, LootTemplates_Creature.HaveQuestLootForPlayer(lootId, bot)).first;

            dropsWanted = cached->second;
        }

        if (!dropsReagent && !dropsWanted && !transformSources.count(creature->GetEntry()))
            continue;

        keepNearest(creature);
    }

    std::list<GameObject*> gos;
    AnyGameObjectInObjectRangeCheck goCheck(bot, scanRange);
    Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> goSearcher(bot, gos, goCheck);
    Cell::VisitObjects(bot, goSearcher, scanRange);
    for (GameObject* go : gos)
        if (IsValidGoTarget(data, go) && HasNeededQuestItem(go))
            keepNearest(go);

    return nearest;
}

bool NewRpgDoQuestAction::ScanForObjectiveTarget(NewRpgInfo::DoQuest& data, float maxDist)
{
    if (!data.quest)
        return false;

    float const scanRange = std::min({maxDist, objectiveScanRange, sPlayerbotAIConfig.SightDistance});
    WorldObject* target = data.objectiveIdx < QUEST_OBJECTIVES_COUNT ? ScanEntryObjectives(data, scanRange)
                                                                     : ScanItemObjective(data, scanRange);
    if (!target)
        return false;

    data.targetGuid = target->GetGUID();
    data.targetPos =
        WorldPosition(bot->GetMapId(), target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
    data.targetSince = 0;
    LOG_DEBUG("playerbots", "[New RPG] {} found quest target {} (dist {}) for quest {} objective {}", bot->GetName(),
              data.targetGuid.ToString(), bot->GetDistance(target), data.questId, data.objectiveIdx);
    return true;
}

bool NewRpgDoQuestAction::EngageTarget(NewRpgInfo::DoQuest& data)
{
    if (bot->IsInCombat())
    {
        data.targetSince = getMSTime();
        return false;
    }

    if (data.lastEngage && GetMSTimeDiffToNow(data.lastEngage) > 5000)
        data.targetSince = getMSTime();
    data.lastEngage = getMSTime();

    if (data.targetGuid.IsAnyTypeCreature())
    {
        Unit* unit = botAI->GetUnit(data.targetGuid);
        Creature* creature = unit ? unit->ToCreature() : nullptr;
        // A creation-source corpse is the cast target itself, not loot fodder.
        if (!creature || (!creature->IsAlive() && !IsCreationSource(data, creature->GetEntry())))
        {
            if (creature)
                AI_VALUE(LootObjectStack*, "available loot")->Add(data.targetGuid);
            ClearTarget(data);
            return true;
        }

        if (creature->hasLootRecipient() && !creature->isTappedBy(bot))
        {
            WriteOffTarget(data);
            return true;
        }

        if (!data.targetSince)
            data.targetSince = getMSTime();
        else if (GetMSTimeDiffToNow(data.targetSince) >= targetEngageTimeout)
        {
            LOG_DEBUG("playerbots", "[New RPG] {} wrote off quest target {} for quest {}", bot->GetName(),
                      data.targetGuid.ToString(), data.questId);
            WriteOffTarget(data);
            return true;
        }

        return EngageCreature(data, creature);
    }

    if (data.targetGuid.IsGameObject())
    {
        GameObject* go = botAI->GetGameObject(data.targetGuid);
        if (!go || !go->isSpawned())
        {
            ClearTarget(data);
            return true;
        }

        if (!data.targetSince)
            data.targetSince = getMSTime();
        else if (GetMSTimeDiffToNow(data.targetSince) >= targetEngageTimeout)
        {
            LOG_DEBUG("playerbots", "[New RPG] {} wrote off quest target {} for quest {}", bot->GetName(),
                      data.targetGuid.ToString(), data.questId);
            WriteOffTarget(data);
            return true;
        }

        return EngageGameObject(data, go);
    }

    ClearTarget(data);
    return true;
}

bool NewRpgDoQuestAction::EngageCreature(NewRpgInfo::DoQuest& data, Creature* creature)
{
    if (IsCreationSource(data, creature->GetEntry()))
        return EngageCreationTarget(data, creature);

    if (IsIncompleteObjectiveEntry(data, creature->GetEntry()))
        if (Item* castItem = GetCastQuestItem(data))
            return EngageCastTarget(data, creature, castItem);

    if (data.quest && GetQuestTransformSources(data.questId).count(creature->GetEntry()))
        if (Item* item = bot->GetItemByEntry(data.quest->GetSrcItemId()))
            return EngageTransformTarget(data, creature, item);

    if (uint32 spellId = GetQuestAbilitySpell(data, creature->GetEntry()))
        return EngageAbilityTarget(data, creature, spellId);

    if (!bot->IsValidAttackTarget(creature))
    {
        // Most item-on-target quests lack the CAST flag, so try the quest's source item first.
        if (data.quest && data.quest->GetSrcItemId())
            if (Item* item = bot->GetItemByEntry(data.quest->GetSrcItemId()))
                if (GetItemUseSpell(item))
                    return EngageCastTarget(data, creature, item);

        // Speak-to credit only for gossip NPCs; triggers and immune mobs need something the bot can't do.
        if (creature->HasNpcFlag(UNIT_NPC_FLAG_GOSSIP))
            return EngageTalkTarget(data, creature);

        WriteOffTarget(data);
        return true;
    }

    if (creature->GetExactDist(data.targetPos) > 10.0f)
        data.targetPos = WorldPosition(bot->GetMapId(), creature->GetPositionX(), creature->GetPositionY(),
                                       creature->GetPositionZ());

    float const dist = bot->GetDistance(creature);

    // Switching needs a 2x closer target, so equidistant mobs can't ping-pong.
    if (dist > engageRange && (!data.lastScan || GetMSTimeDiffToNow(data.lastScan) >= scanInterval))
    {
        data.lastScan = getMSTime();
        ObjectGuid const held = data.targetGuid;
        if (ScanForObjectiveTarget(data, dist * 0.5f) && data.targetGuid != held)
        {
            data.lastEngage = 0;
            LOG_DEBUG("playerbots", "[New RPG] {} switched quest target {} -> {} for quest {}", bot->GetName(),
                      held.ToString(), data.targetGuid.ToString(), data.questId);
            return true;
        }
    }

    if (dist > engageRange || !bot->IsWithinLOSInMap(creature))
    {
        if (MoveFarTo(data.targetPos))
            return true;
        return MoveRandomNear(10.0f);
    }

    return botAI->DoSpecificAction("new rpg attack quest target", Event(), true);
}

bool NewRpgDoQuestAction::EngageGameObject(NewRpgInfo::DoQuest& data, GameObject* go)
{
    if (bot->GetDistance(go) > INTERACTION_DISTANCE - 2.0f)
    {
        if (bot->isMoving())
            return false;
        return MoveTo(bot->GetMapId(), go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), false, false, false,
                      false);
    }

    if (bot->IsMounted())
    {
        bot->Dismount();
        return true;
    }
    if (bot->GetShapeshiftForm() != FORM_NONE)
    {
        bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
        return true;
    }

    // The core grants gameobject credit only to goobers; a chest progresses through its loot.
    LootObject lootObj(bot, go->GetGUID());
    if (go->GetGoType() == GAMEOBJECT_TYPE_CHEST && !lootObj.IsEmpty())
    {
        LootObject currentTarget = AI_VALUE(LootObject, "loot target");
        bool keepCurrent = !currentTarget.IsEmpty() && currentTarget.IsLootPossible(bot) &&
                           AI_VALUE2(float, "distance", "loot target") <= sPlayerbotAIConfig.LootDistance;
        if (currentTarget.guid != go->GetGUID() && !keepCurrent)
            context->GetValue<LootObject>("loot target")->Set(LootObject(bot, go->GetGUID()));
        return AI_VALUE(LootObjectStack*, "available loot")->Add(go->GetGUID());
    }

    // One try: buttons credit only through a linked trap.
    WorldPacket packet(CMSG_GAMEOBJ_USE, 8);
    packet << go->GetGUID();
    bot->GetSession()->HandleGameObjectUseOpcode(packet);
    LOG_DEBUG("playerbots", "[New RPG] {} used quest gameobject {} for quest {}", bot->GetName(), go->GetEntry(),
              data.questId);
    WriteOffTarget(data);
    return true;
}

bool NewRpgDoQuestAction::EngageTalkTarget(NewRpgInfo::DoQuest& data, Creature* creature)
{
    // No isMoving yield: a competing strategy would pull the bot off a target it cannot attack.
    if (bot->GetDistance(creature) > INTERACTION_DISTANCE - 1.0f)
    {
        if (MoveFarTo(data.targetPos))
            return true;
        return MoveRandomNear(5.0f);
    }

    if (bot->IsMounted())
    {
        bot->Dismount();
        return true;
    }

    // TalkedToCreature grants the SPEAKTO credit directly. The gossip handler's own call is
    // commented out (NPCHandler.cpp), so opening a gossip menu would credit nothing.
    bot->SetFacingToObject(creature);
    bot->TalkedToCreature(creature->GetEntry(), creature->GetGUID());
    LOG_DEBUG("playerbots", "[New RPG] {} talked to quest npc {} for quest {}", bot->GetName(), creature->GetEntry(),
              data.questId);
    WriteOffTarget(data);
    return true;
}

void NewRpgDoQuestAction::BuildCaches()
{
    IsCastQuest(0);
    GetQuestAreaTriggerId(0);
}

bool NewRpgDoQuestAction::IsCastQuest(uint32 questId)
{
    static std::unordered_set<uint32> const castQuests = []
    {
        std::unordered_set<uint32> s;
        if (QueryResult result =
                WorldDatabase.Query("SELECT ID FROM quest_template_addon WHERE (SpecialFlags & 0x20) <> 0"))
            do
            {
                s.insert((*result)[0].Get<uint32>());
            } while (result->NextRow());
        return s;
    }();

    return castQuests.count(questId) != 0;
}

Item* NewRpgDoQuestAction::GetCastQuestItem(NewRpgInfo::DoQuest const& data)
{
    if (!IsCastQuest(data.questId) || !data.quest)
        return nullptr;

    uint32 itemId = data.quest->GetSrcItemId();
    if (!itemId)
        return nullptr;

    return bot->GetItemByEntry(itemId);
}

uint32 NewRpgDoQuestAction::GetItemUseSpell(Item* item)
{
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        if (item->GetTemplate()->Spells[i].SpellId > 0)
            return item->GetTemplate()->Spells[i].SpellId;

    return 0;
}

bool NewRpgDoQuestAction::EngageTransformTarget(NewRpgInfo::DoQuest& data, Creature* creature, Item* item)
{
    // From range: combat pauses this engine, so a bot in melee would kill the mob untransformed.
    if (bot->GetDistance(creature) > transformCastRange || !bot->IsWithinLOSInMap(creature))
    {
        if (MoveFarTo(data.targetPos))
            return true;
        return MoveRandomNear(10.0f);
    }

    if (bot->IsMounted())
    {
        bot->Dismount();
        return true;
    }

    uint32 const spellId = GetItemUseSpell(item);
    if (spellId && bot->HasSpellItemCooldown(spellId, item->GetEntry()))
        return true;

    // Not written off: SmartAI changes the entry in place, so the same guid becomes the kill target.
    bot->SetFacingToObject(creature);
    botAI->ImbueItem(item, creature);
    LOG_DEBUG("playerbots", "[New RPG] {} used quest item {} to transform npc {} for quest {}", bot->GetName(),
              item->GetEntry(), creature->GetEntry(), data.questId);
    return true;
}

bool NewRpgDoQuestAction::EngageCastTarget(NewRpgInfo::DoQuest& data, Creature* creature, Item* item)
{
    if (bot->GetDistance(creature) > INTERACTION_DISTANCE - 1.0f)
    {
        if (MoveFarTo(data.targetPos))
            return true;
        return MoveRandomNear(5.0f);
    }

    if (bot->IsMounted())
    {
        bot->Dismount();
        return true;
    }

    uint32 const spellId = GetItemUseSpell(item);
    if (spellId && bot->HasSpellItemCooldown(spellId, item->GetEntry()))
        return true;

    bot->SetFacingToObject(creature);
    botAI->ImbueItem(item, creature);
    LOG_DEBUG("playerbots", "[New RPG] {} used quest item {} on npc {} for quest {}", bot->GetName(), item->GetEntry(),
              creature->GetEntry(), data.questId);
    WriteOffTarget(data);
    return true;
}

// The trigger spell this bot knows for a spellhit-credit objective the creature stands in for.
uint32 NewRpgDoQuestAction::GetQuestAbilitySpell(NewRpgInfo::DoQuest const& data, uint32 entry) const
{
    if (!sPlayerbotAIConfig.QuestAbilityCredit || data.objectiveIdx < 0 ||
        data.objectiveIdx >= QUEST_OBJECTIVES_COUNT)
        return 0;

    std::vector<uint32> const& sources = GetQuestKillSources(data.questId, uint32(data.objectiveIdx));
    if (std::find(sources.begin(), sources.end(), entry) == sources.end())
        return 0;

    for (uint32 spell : GetQuestAbilitySpells(data.questId, uint32(data.objectiveIdx)))
        if (bot->HasSpell(spell))
            return spell;

    return 0;
}

float NewRpgDoQuestAction::QuestAbilityCastRange(uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info)
        return 5.0f;

    float range = 5.0f;
    for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
        if (info->Effects[e].IsEffect())
            range = std::max(range, info->Effects[e].CalcRadius());

    return range;
}

bool NewRpgDoQuestAction::EngageAbilityTarget(NewRpgInfo::DoQuest& data, Creature* creature, uint32 spellId)
{
    // The creature is only a spellhit victim here, so the cast outranks fighting it.
    if (bot->GetDistance(creature) > QuestAbilityCastRange(spellId) || !bot->IsWithinLOSInMap(creature))
    {
        if (MoveFarTo(data.targetPos))
            return true;
        return MoveRandomNear(5.0f);
    }

    if (bot->IsMounted())
    {
        bot->Dismount();
        return true;
    }

    if (bot->IsNonMeleeSpellCast(false))
        return true;

    if (bot->HasSpellCooldown(spellId))
        return true;

    if (data.lastItemUse && GetMSTimeDiffToNow(data.lastItemUse) < QuestItemUseGrace(spellId))
        return true;

    botAI->CastSpell(spellId, bot);
    data.lastItemUse = getMSTime();
    LOG_DEBUG("playerbots", "[New RPG] {} cast ability {} on {} for quest {} objective {}", bot->GetName(), spellId,
              creature->GetEntry(), data.questId, data.objectiveIdx);
    return true;
}

// A vial-style creation source: a live one is fought into the corpse the spell demands, a corpse
// receives the creator item's cast.
bool NewRpgDoQuestAction::EngageCreationTarget(NewRpgInfo::DoQuest& data, Creature* creature)
{
    if (creature->IsAlive())
        return false;

    uint32 const itemIdx = uint32(data.objectiveIdx) - QUEST_OBJECTIVES_COUNT;
    if (Item* item = bot->GetItemByEntry(GetQuestCreationItem(data.questId, itemIdx)))
        return EngageCastTarget(data, creature, item);

    LOG_DEBUG("playerbots", "[New RPG] {} holds no creation item for quest {} objective {}", bot->GetName(),
              data.questId, data.objectiveIdx);
    WriteOffTarget(data);
    return true;
}

bool NewRpgDoQuestAction::IsCreationSource(NewRpgInfo::DoQuest const& data, uint32 entry) const
{
    if (data.objectiveIdx < QUEST_OBJECTIVES_COUNT || !sPlayerbotAIConfig.QuestItemLinks)
        return false;

    std::vector<int32> const& sources =
        GetQuestCreationSources(data.questId, uint32(data.objectiveIdx) - QUEST_OBJECTIVES_COUNT);
    return std::find(sources.begin(), sources.end(), int32(entry)) != sources.end();
}

bool NewRpgDoQuestAction::DoScriptedItemObjective(NewRpgInfo::DoQuest& data)
{
    QuestScriptedItemTarget const& scripted = GetQuestScriptedItemTarget(data.questId);
    if (!scripted.creatureEntry || !data.quest)
        return false;

    if (bot->GetQuestStatus(data.questId) == QUEST_STATUS_COMPLETE)
        return false;

    Item* item = bot->GetItemByEntry(scripted.itemId);
    if (!item)
        return false;

    uint32 const spellId = GetItemUseSpell(item);
    if (!spellId || bot->HasSpellItemCooldown(spellId, item->GetEntry()))
        return false;

    if (data.lastItemUse && GetMSTimeDiffToNow(data.lastItemUse) < QuestItemUseGrace(spellId))
        return true;

    if (bot->IsNonMeleeSpellCast(false))
        return true;

    Creature* target = nullptr;
    float nearest = FLT_MAX;
    std::list<Creature*> creatures;
    bot->GetCreatureListWithEntryInGrid(creatures, scripted.creatureEntry, scriptedTargetSearchRange);
    for (Creature* creature : creatures)
    {
        if (!creature->IsInWorld() || (!creature->IsAlive() && creature->getDeathState() != DeathState::Corpse))
            continue;

        float const dist = bot->GetDistance(creature);
        if (dist < nearest)
        {
            nearest = dist;
            target = creature;
        }
    }

    if (target)
    {
        if (target->IsAlive())
        {
            data.targetGuid = target->GetGUID();
            data.targetPos = WorldPosition(target);
            data.targetSince = 0;
            if (EngageTarget(data))
                return true;
            return IsMovementPending();
        }

        float const castRange = INTERACTION_DISTANCE - 1.0f;
        if (bot->GetDistance(target) <= castRange && bot->IsWithinLOSInMap(target))
        {
            if (bot->IsMounted())
            {
                bot->Dismount();
                return true;
            }

            bot->SetFacingToObject(target);
            botAI->ImbueItem(item, target);
            data.lastItemUse = getMSTime();
            LOG_DEBUG("playerbots", "[New RPG] {} used quest item {} on {} for quest {}", bot->GetName(),
                      item->GetEntry(), target->GetEntry(), data.questId);
            WriteOffTarget(data);
            return true;
        }

        if (MoveFarTo(WorldPosition(target)))
            return true;
        if (IsMovementPending())
            return true;
        return MoveRandomNear(5.0f);
    }

    WorldPosition dest;
    for (GuidPosition const& pos : GetEntrySpawns(int32(scripted.creatureEntry), bot->GetMapId()))
    {
        WorldPosition candidate(pos.GetMapId(), pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
        if (dest == WorldPosition() || bot->GetDistance(candidate) < bot->GetDistance(dest))
            dest = candidate;
    }

    if (dest == WorldPosition())
    {
        if (QuestPOIVector const* pois = sObjectMgr->GetQuestPOIVector(data.questId))
        {
            for (QuestPOI const& poi : *pois)
            {
                if (poi.MapId != bot->GetMapId() || poi.ObjectiveIndex != 16 || poi.points.empty())
                    continue;

                float x = 0.0f, y = 0.0f;
                for (QuestPOIPoint const& point : poi.points)
                {
                    x += point.x;
                    y += point.y;
                }
                x /= float(poi.points.size());
                y /= float(poi.points.size());

                float bx = 0.0f, by = 0.0f, bz = 0.0f;
                if (QuestPoiZStore::Instance().FindNearestWalkablePoint(data.questId, poi.Id, bot->GetMapId(), x, y,
                                                                       bx, by, bz))
                {
                    dest = WorldPosition(bot->GetMapId(), bx, by, bz);
                    break;
                }

                float z = std::max(bot->GetMap()->GetHeight(x, y, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(x, y));
                if (z == INVALID_HEIGHT || z == VMAP_INVALID_HEIGHT_VALUE)
                    z = bot->GetPositionZ();
                dest = WorldPosition(bot->GetMapId(), x, y, z);
                break;
            }
        }
    }

    if (dest == WorldPosition())
        return false;

    if (bot->GetDistance(dest) > 10.0f)
    {
        if (MoveFarTo(dest))
            return true;
        if (IsMovementPending())
            return true;
        return MoveRandomNear(10.0f);
    }
    if (MoveRandomNear(5.0f))
        return true;
    return IsMovementPending();
}

// Use the quest's item for the active objective: a KILL_CREDIT item at a creature (feed-style),
// or a focus-requiring creation item cast next to its spell focus (moonwell-style).
bool NewRpgDoQuestAction::DoUseCreditItem(NewRpgInfo::DoQuest& data)
{
    if (!sPlayerbotAIConfig.QuestItemLinks || data.objectiveIdx < 0 || IsObjectiveCompleted(data))
        return false;

    if (data.objectiveIdx >= QUEST_OBJECTIVES_COUNT)
    {
        uint32 const itemIdx = uint32(data.objectiveIdx) - QUEST_OBJECTIVES_COUNT;
        uint32 const focusId = GetQuestCreationFocus(data.questId, itemIdx);
        if (!focusId)
            return false;

        Item* item = bot->GetItemByEntry(GetQuestCreationItem(data.questId, itemIdx));
        if (!item)
            return false;

        uint32 const spellId = GetItemUseSpell(item);
        if (!spellId || bot->HasSpellItemCooldown(spellId, item->GetEntry()))
            return false;

        if (data.lastItemUse && GetMSTimeDiffToNow(data.lastItemUse) < QuestItemUseGrace(spellId))
            return true;

        if (bot->IsNonMeleeSpellCast(false))
            return true;

        // Focus objects can be deep inside a cave or hive. Their spawns are indexed statically,
        // so travel to the nearest one instead of relying on the bot's loaded visibility.
        WorldPosition focusSpawn;
        float focusSpawnDist = FLT_MAX;
        for (uint32 goEntry : GetFocusObjectEntries(focusId))
        {
            for (GuidPosition const& pos : GetEntrySpawns(-int32(goEntry), bot->GetMapId()))
            {
                WorldPosition const candidate(pos.GetMapId(), pos.GetPositionX(), pos.GetPositionY(),
                                              pos.GetPositionZ());
                float const dist = bot->GetDistance(candidate);
                if (dist < focusSpawnDist)
                {
                    focusSpawnDist = dist;
                    focusSpawn = candidate;
                }
            }
        }

        if (focusSpawn != WorldPosition() && focusSpawnDist > INTERACTION_DISTANCE)
        {
            LOG_DEBUG("playerbots", "[New RPG] {} heading to focus spawn {} for quest {} ({:.0f} yd)",
                      bot->GetName(), focusId, data.questId, focusSpawnDist);
            if (MoveFarTo(focusSpawn))
                return true;
            if (IsMovementPending())
                return true;
            return MoveRandomNear(5.0f);
        }

        GameObject* focus = FindNearestFocusAnchor(focusId, focusAnchorScanRange);
        if (!focus)
        {
            // Last resort when no indexed spawn exists: the item-objective POI.
            if (data.pos != WorldPosition() && bot->GetDistance(data.pos) > 10.0f)
            {
                if (MoveFarTo(data.pos))
                    return true;
                if (IsMovementPending())
                    return true;
                return MoveRandomNear(5.0f);
            }
            return false;
        }

        // GameObjectFocusCheck - the rule the core casts by - accepts the caster at half the
        // template's dist. A yard of margin so a cast that starts in range also finishes there.
        float const useRange = std::max(float(focus->GetGOInfo()->spellFocus.dist) / 2.0f - 1.0f, 2.0f);
        if (bot->GetDistance(focus) > useRange)
        {
            if (MoveWorldObjectTo(focus->GetGUID(), useRange))
                return true;
            if (IsMovementPending())
                return true;
            return MoveRandomNear(5.0f);
        }

        if (bot->IsMounted())
        {
            bot->Dismount();
            return true;
        }

        bot->SetFacingToObject(focus);
        botAI->ImbueItem(item);
        data.lastItemUse = getMSTime();
        LOG_DEBUG("playerbots", "[New RPG] {} used quest item {} at spell focus {} for quest {} objective {}",
                  bot->GetName(), item->GetEntry(), focus->GetEntry(), data.questId, data.objectiveIdx);
        return true;
    }

    for (uint32 itemId : GetQuestCreditItems(data.questId, uint32(data.objectiveIdx)))
    {
        Item* item = bot->GetItemByEntry(itemId);
        if (!item)
            continue;

        uint32 const spellId = GetItemUseSpell(item);
        if (spellId && bot->HasSpellItemCooldown(spellId, itemId))
            return true;

        if (data.lastItemUse && GetMSTimeDiffToNow(data.lastItemUse) < QuestItemUseGrace(spellId))
            return true;

        if (bot->IsNonMeleeSpellCast(false))
            return true;

        botAI->ImbueItem(item);
        data.lastItemUse = getMSTime();
        LOG_DEBUG("playerbots", "[New RPG] {} used credit item {} for quest {} objective {}", bot->GetName(), itemId,
                  data.questId, data.objectiveIdx);
        return true;
    }
    return false;
}

// The escort NPC for the active quest, locked once found so pauses cannot hand the leash away.
Creature* NewRpgDoQuestAction::ResolveEscortee(NewRpgInfo::DoQuest& data)
{
    if (data.escortGuid)
    {
        if (Unit* unit = botAI->GetUnit(data.escortGuid))
            if (Creature* creature = unit->ToCreature())
                if (creature->IsInWorld() && creature->IsAlive())
                    return creature;

        data.escortGuid = ObjectGuid::Empty;
    }

    Creature* found = FindEscorteeForQuest(bot, data.questId, escortSearchRange);
    if (found)
        data.escortGuid = found->GetGUID();

    return found;
}

// While an escort walks, nothing else matters: stay on the leash, let combat defend the NPC, and
// never let a chase or a status timeout end the escort early.
bool NewRpgDoQuestAction::DoEscortObjective(NewRpgInfo::DoQuest& data)
{
    if (!sPlayerbotAIConfig.QuestEscorts || !data.quest)
        return false;

    Creature* escortee = ResolveEscortee(data);
    if (!escortee)
        return false;

    botAI->rpgInfo.RefreshStatusTime();

    WorldPosition const livePos(bot->GetMapId(), escortee->GetPositionX(), escortee->GetPositionY(),
                                escortee->GetPositionZ());

    float const dist = bot->GetDistance(escortee);
    if (dist > escortRecallRange)
    {
        // The SAI escort invoker check fails the quest at 60 yd: drop whatever the bot chased
        // and return before it trips.
        ClearTarget(data);
        if (MoveFarTo(livePos))
            return true;
        return MoveRandomNear(5.0f);
    }

    if (dist > escortLeashRange)
    {
        if (MoveFarTo(livePos))
            return true;
        return MoveRandomNear(2.0f);
    }

    // On the leash: hold with the escort. The combat engine handles its attackers, and the escort
    // multiplier keeps loot and grind from dragging the bot away.
    return true;
}

bool NewRpgDoQuestAction::DoCraftObjective(NewRpgInfo::DoQuest& data)
{
    if (data.objectiveIdx < QUEST_OBJECTIVES_COUNT || !data.quest)
        return false;

    uint32 const itemIdx = uint32(data.objectiveIdx) - QUEST_OBJECTIVES_COUNT;
    std::vector<QuestCraftReagent> const& craft = GetQuestCraftReagents(data.questId, itemIdx);
    if (craft.empty() || IsObjectiveCompleted(data))
        return false;

    for (QuestCraftReagent const& reagent : craft)
        if (bot->GetItemCount(reagent.item, false) < reagent.count)
            return false;

    Item* item = bot->GetItemByEntry(GetQuestCraftItem(data.questId, itemIdx));
    if (!item)
        return false;

    uint32 const spellId = GetItemUseSpell(item);
    if (!spellId || bot->HasSpellItemCooldown(spellId, item->GetEntry()))
        return false;

    if (data.lastItemUse && GetMSTimeDiffToNow(data.lastItemUse) < QuestItemUseGrace(spellId))
        return true;

    if (bot->IsNonMeleeSpellCast(false))
        return true;

    botAI->ImbueItem(item);
    data.lastItemUse = getMSTime();
    LOG_DEBUG("playerbots", "[New RPG] {} used quest item {} to craft for quest {}", bot->GetName(), item->GetEntry(),
              data.questId);
    return true;
}

WorldObject* NewRpgDoQuestAction::SelectSummonAnchor(NewRpgInfo::DoQuest const& data,
                                                     QuestSummonAnchor const& anchor) const
{
    if (anchor.focusId)
        return FindFocusAnchor(anchor.focusId);

    if (!anchor.killAnchor)
        return FindCreatureAnchor(anchor.creature, summonCreatureAnchorRange);

    Creature* best = nullptr;
    float nearest = summonKillAnchorRange;
    for (uint32 entry : GetQuestKillSources(data.questId, uint32(data.objectiveIdx)))
    {
        Creature* candidate = FindCreatureAnchor(entry, summonKillAnchorRange);
        if (!candidate || candidate->GetGUID() == data.lastSummonAnchor)
            continue;

        float const dist = bot->GetDistance(candidate);
        if (dist < nearest)
        {
            nearest = dist;
            best = candidate;
        }
    }

    return best;
}

bool NewRpgDoQuestAction::DoSummonObjective(NewRpgInfo::DoQuest& data, QuestSummonAnchor const& anchor)
{
    if (data.objectiveIdx < 0 || data.objectiveIdx >= QUEST_OBJECTIVES_COUNT || !data.quest)
        return false;

    if ((!anchor.focusId && !anchor.creature) || IsObjectiveCompleted(data))
        return false;

    Item* item = bot->GetItemByEntry(data.quest->GetSrcItemId());
    if (!item)
        return false;

    uint32 const spellId = GetItemUseSpell(item);
    if (!spellId)
        return false;

    for (QuestCraftReagent const& reagent : GetQuestItemReagents(data.questId))
        if (bot->GetItemCount(reagent.item, false) < reagent.count)
            return false;

    // Re-summoning replaces a trigger before its timer fires, but only suppress while it is
    // still near the fight - engaging a kill-anchor walks away from where it was planted.
    int32 const summoned = data.quest->RequiredNpcOrGo[data.objectiveIdx];
    if (anchor.summonsObjective && summoned > 0 && FindCreatureAnchor(uint32(summoned), summonTriggerNearRange))
        return false;

    if (data.lastItemUse && GetMSTimeDiffToNow(data.lastItemUse) < QuestItemUseGrace(spellId))
        return true;

    if (bot->IsNonMeleeSpellCast(false))
        return true;

    if (bot->HasSpellItemCooldown(spellId, item->GetEntry()))
        return false;

    WorldObject* target = SelectSummonAnchor(data, anchor);
    if (!target)
        return false;

    if (bot->IsMounted())
    {
        bot->Dismount();
        return true;
    }

    bot->SetFacingToObject(target);
    botAI->ImbueItem(item);
    data.lastItemUse = getMSTime();
    data.lastSummonAnchor = target->GetGUID();
    LOG_DEBUG("playerbots", "[New RPG] {} used quest item {} at anchor {} to summon for quest {}", bot->GetName(),
              item->GetEntry(), target->GetEntry(), data.questId);
    return true;
}

GameObject* NewRpgDoQuestAction::FindFocusAnchor(uint32 focusId) const
{
    GameObject* const focus = FindNearestFocusAnchor(focusId, summonAnchorScanRange);
    if (!focus)
        return nullptr;

    // GameObjectFocusCheck - the rule the core casts by - accepts the caster at half the
    // template's dist. A yard of margin so a cast that starts in range also finishes there.
    float const useRange = std::max(float(focus->GetGOInfo()->spellFocus.dist) / 2.0f - 1.0f, 2.0f);
    return bot->GetDistance(focus) <= useRange ? focus : nullptr;
}

GameObject* NewRpgDoQuestAction::FindNearestFocusAnchor(uint32 focusId, float range) const
{
    GameObject* found = nullptr;
    float nearest = FLT_MAX;

    std::list<GameObject*> gos;
    AnyGameObjectInObjectRangeCheck goCheck(bot, range);
    Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> goSearcher(bot, gos, goCheck);
    Cell::VisitObjects(bot, goSearcher, range);
    for (GameObject* go : gos)
    {
        if (!go->isSpawned() || go->GetGoType() != GAMEOBJECT_TYPE_SPELL_FOCUS ||
            go->GetGOInfo()->spellFocus.focusId != focusId)
            continue;

        float const dist = bot->GetDistance(go);
        if (dist < nearest)
        {
            nearest = dist;
            found = go;
        }
    }

    return found;
}

Creature* NewRpgDoQuestAction::FindCreatureAnchor(uint32 entry, float range) const
{
    Creature* found = nullptr;
    float nearest = range;

    std::list<Creature*> creatures;
    bot->GetCreatureListWithEntryInGrid(creatures, entry, std::max(range, summonAnchorScanRange));
    for (Creature* creature : creatures)
    {
        if (!creature->IsInWorld() || !creature->IsAlive())
            continue;

        float const dist = bot->GetDistance(creature);
        if (dist < nearest)
        {
            nearest = dist;
            found = creature;
        }
    }

    return found;
}

uint32 NewRpgDoQuestAction::QuestItemUseGrace(uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    return std::max<uint32>(2000, (info ? info->CalcCastTime() : 0) + 1000);
}

bool NewRpgDoQuestAction::DoExploreObjective(NewRpgInfo::DoQuest& data)
{
    Quest const* quest = data.quest;
    if (!quest || !quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT))
        return false;

    // Explored is separate from the objective counts; once set, what remains is a scripted event.
    QuestStatusData const* q_status = GetQuestStatus(data.questId);
    if (!q_status || q_status->Explored)
        return false;

    uint32 triggerId = GetQuestAreaTriggerId(data.questId);
    if (!triggerId)
        return false;

    AreaTrigger const* at = sObjectMgr->GetAreaTrigger(triggerId);
    if (!at || at->map != bot->GetMapId())
        return false;

    // HandleAreaTriggerOpcode re-validates the radius and calls AreaExploredOrEventHappens.
    if (bot->IsInAreaTriggerRadius(at, 0.0f))
    {
        WorldPacket p(CMSG_AREATRIGGER);
        p << triggerId;
        p.rpos(0);
        bot->GetSession()->HandleAreaTriggerOpcode(p);
        LOG_DEBUG("playerbots", "[New RPG] {} fired area trigger {} for quest {}", bot->GetName(), triggerId,
                  data.questId);
        return true;
    }

    // The trigger is often inside a mine/catacomb: approach the baked walkable floor point for
    // the quest's exploration POI first, then the trigger itself.
    uint32 explorationPoiId = 0;
    if (QuestPOIVector const* pois = sObjectMgr->GetQuestPOIVector(data.questId))
    {
        for (QuestPOI const& poi : *pois)
        {
            if (poi.MapId != bot->GetMapId() || poi.points.empty())
                continue;

            if (poi.ObjectiveIndex == 16)
            {
                explorationPoiId = poi.Id;
                break;
            }
            if (poi.ObjectiveIndex == 0 && !explorationPoiId)
                explorationPoiId = poi.Id;
        }
    }

    float bx = 0.0f, by = 0.0f, bz = 0.0f;
    if (explorationPoiId && QuestPoiZStore::Instance().FindNearestWalkablePoint(
                                data.questId, explorationPoiId, bot->GetMapId(), at->x, at->y, bx, by, bz))
    {
        WorldPosition const approach(bot->GetMapId(), bx, by, bz);
        if (bot->GetDistance(approach) > 8.0f)
        {
            LOG_DEBUG("playerbots",
                      "[New RPG] {} approaching exploration trigger {} via baked point ({:.0f}, {:.0f}, {:.0f})",
                      bot->GetName(), triggerId, bx, by, bz);
            if (MoveFarTo(approach))
                return true;
            if (IsMovementPending())
                return true;
            return MoveRandomNear(5.0f);
        }
    }

    // Water triggers (The Forgotten Pools and similar) sit below the surface: swim straight at
    // the trigger point when it is close and visible, reusing the underwater loot approach.
    bool const triggerInWater =
        bot->GetMap()->IsInWater(bot->GetPhaseMask(), at->x, at->y, at->z, 2.0f) ||
        bot->GetMap()->IsUnderWater(bot->GetPhaseMask(), at->x, at->y, at->z, 2.0f);
    if (triggerInWater && bot->GetDistance(at->x, at->y, at->z) < 150.0f &&
        bot->IsWithinLOS(at->x, at->y, at->z))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} swimming to exploration trigger {} for quest {}", bot->GetName(),
                  triggerId, data.questId);
        if (MoveTo(bot->GetMapId(), at->x, at->y, at->z, false, false, false, true))
            return true;
        return IsMovementPending();
    }

    WorldPosition pos(at->map, at->x, at->y, at->z);
    if (bot->GetDistance(pos) > 10.0f)
    {
        if (MoveFarTo(pos))
            return true;
        if (IsMovementPending())
            return true;
        return MoveRandomNear(10.0f);
    }
    if (MoveRandomNear(5.0f))
        return true;
    return IsMovementPending();
}

bool NewRpgDoQuestAction::DoIncompleteQuest(NewRpgInfo::DoQuest& data)
{
    uint32 questId = data.questId;

    if (data.pos != WorldPosition() && IsObjectiveCompleted(data))
    {
        botAI->DoSpecificAction("add all loot", Event(), true);
        data.triedSpawns.clear();
        data.visited.clear();
        ResetObjectiveSpawn(data);
    }

    if (HoldForLoot(data))
        return false;

    if (DoUseCreditItem(data))
        return true;

    if (DoScriptedItemObjective(data))
        return true;

    if (DoEscortObjective(data))
        return true;

    if (botAI->rpgInfo.HasStatusPersisted(questStayTime))
    {
        botAI->lowPriorityQuest.insert(questId);
        botAI->RecordQuestAbandoned(questId, "no_progression");
        LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {} (quest budget)", bot->GetName(), questId);
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }

    if (data.pos == WorldPosition())
    {
        if (!SelectObjectiveSpawn(data))
        {
            if (DoExploreObjective(data))
                return true;
            if (SelectObjectivePoi(data))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} quest {} objective {}: no spawn indexed, POI fallback",
                          bot->GetName(), questId, data.objectiveIdx);
                return true;
            }
            LOG_DEBUG("playerbots", "[New RPG] {} gave up quest {}: no spawn and no POI on map {}", bot->GetName(),
                      questId, bot->GetMapId());
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        LOG_DEBUG("playerbots", "[New RPG] {} quest {} heading to objective {} spawn at {} yd", bot->GetName(), questId,
                  data.objectiveIdx, uint32(bot->GetDistance(data.pos)));
        return true;
    }

    // Timed from when the spawn was picked, not arrival: an unpathable spawn is never reached.
    if (data.spawnSince && GetMSTimeDiffToNow(data.spawnSince) >= poiStayTime)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} quest {} objective {}: {} after {} s at this spawn, {} tried",
                  bot->GetName(), questId, data.objectiveIdx, HasObjectiveProgress(data) ? "progress" : "nothing",
                  poiStayTime / 1000, data.triedSpawns.size());

        if (!HasObjectiveProgress(data))
        {
            if (data.spawnGuid)
                data.triedSpawns.insert(data.spawnGuid);
        }
        else
        {
            data.triedSpawns.clear();
            data.visited.clear();
        }
        ResetObjectiveSpawn(data);
        return true;
    }

    QuestSummonAnchor const anchor =
        data.objectiveIdx >= 0 ? GetQuestSummonAnchor(data.questId, uint32(data.objectiveIdx)) : QuestSummonAnchor{};

    // A kill-anchor gets the item before the fight, or it is fought un-primed.
    if (anchor.killAnchor && DoSummonObjective(data, anchor))
        return true;

    if (data.targetGuid)
        return EngageTarget(data);

    if (!data.lastScan || GetMSTimeDiffToNow(data.lastScan) >= scanInterval)
    {
        data.lastScan = getMSTime();
        if (ScanForObjectiveTarget(data, objectiveScanRange))
            return true;
    }

    if (DoCraftObjective(data))
        return true;

    if (DoSummonObjective(data, anchor))
        return true;

    if (bot->GetDistance(data.pos) > 10.0f)
    {
        if (MoveFarTo(data.pos))
            return true;
        return MoveRandomNear(10.0f);
    }

    // One scan window after arrival: the cell may only just have loaded.
    if (!data.lastReachPOI)
    {
        data.lastReachPOI = getMSTime();
        return true;
    }

    if (GetMSTimeDiffToNow(data.lastReachPOI) < scanInterval)
        return true;

    if (data.poiFallback)
        return MoveRandomNear(8.0f);

    LOG_DEBUG("playerbots", "[New RPG] {} quest {} objective {}: arrived, nothing in range - next spawn",
              bot->GetName(), questId, data.objectiveIdx);
    data.triedSpawns.insert(data.spawnGuid);
    ResetObjectiveSpawn(data);
    return true;
}

bool NewRpgDoQuestAction::DoCompletedQuest(NewRpgInfo::DoQuest& data)
{
    uint32 questId = data.questId;
    Quest const* quest = data.quest;

    if (data.objectiveIdx != -1)
    {
        // The turn-in walk would outrun the random "add all loot" trigger.
        botAI->DoSpecificAction("add all loot", Event(), true);
        ClearTarget(data);
        // "attack anything" outranks this action and would interrupt the turn-in walk.
        data.poiFallback = false;
        BroadcastHelper::BroadcastQuestUpdateComplete(botAI, bot, quest);
        data.lastReachPOI = 0;
        data.objectiveIdx = -1;
        data.spawnGuid = 0;
        data.pos = WorldPosition();
        data.triedSpawns.clear();
    }

    if (data.pos == WorldPosition())
    {
        if (SelectQuestEnder(data))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} quest {} complete, turn-in at ender spawn {} yd away ({} tried)",
                      bot->GetName(), questId, uint32(bot->GetDistance(data.pos)), data.triedSpawns.size());
        }
        else
        {
            std::vector<POIInfo> poiInfo;
            if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo, true))
            {
                LOG_DEBUG("playerbots",
                          "[New RPG] {} quest {} complete but no turn-in on map {}: "
                          "{} ender entries indexed ({} spawns tried), no POI either",
                          bot->GetName(), questId, bot->GetMapId(), GetQuestEnderEntries(questId).size(),
                          data.triedSpawns.size());
                botAI->rpgInfo.ChangeToIdle();
                return false;
            }
            assert(poiInfo.size() > 0);
            data.pos = WorldPosition(bot->GetMapId(), poiInfo[0].pos.x, poiInfo[0].pos.y, poiInfo[0].z);
            LOG_DEBUG("playerbots",
                      "[New RPG] {} quest {} complete, turn-in POI {} yd away ({} ender entries, "
                      "{} spawns tried)",
                      bot->GetName(), questId, uint32(bot->GetDistance(data.pos)), GetQuestEnderEntries(questId).size(),
                      data.triedSpawns.size());
        }
    }

    if (HoldForLoot(data))
        return false;

    if (bot->GetDistance(data.pos) > 10.0f && !data.lastReachPOI)
    {
        if (MoveFarTo(data.pos))
            return true;
        return MoveRandomNear(10.0f);
    }

    // The hand-in itself happens in SearchQuestGiverAndAcceptOrReward.
    if (!data.lastReachPOI)
    {
        data.lastReachPOI = getMSTime();
        LOG_DEBUG("playerbots", "[New RPG] {} reached turn-in for quest {}, waiting on the hand-in", bot->GetName(),
                  questId);
        return true;
    }
    if (data.spawnGuid && GetMSTimeDiffToNow(data.lastReachPOI) >= enderWaitTime)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} nobody took quest {} at this ender spawn, trying the next",
                  bot->GetName(), questId);
        data.triedSpawns.insert(data.spawnGuid);
        data.spawnGuid = 0;
        data.pos = WorldPosition();
        data.lastReachPOI = 0;
        return true;
    }

    if (GetMSTimeDiffToNow(data.lastReachPOI) >= poiStayTime)
    {
        // e.g. Can not reward quest to gameobjects
        /// @TODO: It may be better to make lowPriorityQuest a global set shared by all bots (or saved in db)
        botAI->lowPriorityQuest.insert(questId);
        botAI->RecordQuestAbandoned(questId, "reward_issue");
        LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }
    return false;
}
