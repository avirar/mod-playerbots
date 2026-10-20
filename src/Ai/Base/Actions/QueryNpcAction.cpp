/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "QueryNpcAction.h"
#include "Creature.h"
#include "Event.h"
#include "Log.h"
#include "MotionMaster.h"
#include "NewRpgBaseAction.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "QuestValues.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "World.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

namespace
{
    using QnpcPlaceholders = std::map<std::string, std::string>;

    std::string ToLower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
        return text;
    }

    std::string QnpcText(char const* key, char const* fallback, QnpcPlaceholders const& placeholders)
    {
        return PlayerbotTextMgr::instance().GetBotTextOrDefault(key, fallback, placeholders);
    }

    std::string QnpcBool(bool value)
    {
        return PlayerbotTextMgr::instance().GetBotTextOrDefault(value ? "qnpc_yes" : "qnpc_no", value ? "yes" : "no",
                                                                {});
    }

    std::string QnpcFloat(float value) { return Acore::StringFormat("{:.3f}", value); }

    std::string QnpcHex(uint32 value) { return Acore::StringFormat("0x{:X}", value); }
}

bool QueryNpcAction::Execute(Event event)
{
    Creature* npc = FindTarget(event.getParam());
    if (!npc)
    {
        botAI->TellError(QnpcText("qnpc_no_npc_found", "No NPC found", {}));
        return false;
    }

    DumpNpc(npc);
    return true;
}

Creature* QueryNpcAction::FindTarget(std::string const& param)
{
    GuidVector nearest = AI_VALUE(GuidVector, "nearest npcs");

    if (!param.empty())
    {
        bool const numeric = param.find_first_not_of("0123456789") == std::string::npos;
        if (numeric)
        {
            uint32 const entry = uint32(atoi(param.c_str()));
            for (ObjectGuid const guid : nearest)
                if (Creature* creature = botAI->GetCreature(guid))
                    if (creature->GetEntry() == entry)
                        return creature;
        }
        else
        {
            std::string const needle = ToLower(param);
            for (ObjectGuid const guid : nearest)
            {
                if (Creature* creature = botAI->GetCreature(guid))
                {
                    std::string const name = ToLower(creature->GetNameForLocaleIdx(sWorld->GetDefaultDbcLocale()));
                    if (name.find(needle) != std::string::npos)
                        return creature;
                }
            }
        }

        return nullptr;
    }

    for (ObjectGuid const guid : nearest)
        if (Creature* creature = botAI->GetCreature(guid))
            return creature;

    return nullptr;
}

void QueryNpcAction::DumpNpc(Creature* npc)
{
    CreatureTemplate const* info = npc->GetCreatureTemplate();
    CreatureData const* spawn = npc->GetCreatureData();

    auto tell = [&](std::string const& text)
    {
        botAI->TellMasterNoFacing(text);
        LOG_DEBUG("playerbots", "QNPC {}", text);
    };

    tell(QnpcText("qnpc_header", "QNPC %entry '%name' guid=%guid spawnId=%spawnId",
                  {{"%entry", std::to_string(npc->GetEntry())},
                   {"%name", npc->GetNameForLocaleIdx(sWorld->GetDefaultDbcLocale())},
                   {"%guid", npc->GetGUID().ToString()},
                   {"%spawnId", std::to_string(npc->GetSpawnId())}}));
    tell(QnpcText("qnpc_world", "QNPC world: map=%map zone=%zone area=%area x=%x y=%y z=%posZ o=%o phaseMask=%phaseMask",
                  {{"%map", std::to_string(npc->GetMapId())},
                   {"%zone", std::to_string(npc->GetZoneId())},
                   {"%area", std::to_string(npc->GetAreaId())},
                   {"%x", QnpcFloat(npc->GetPositionX())},
                   {"%y", QnpcFloat(npc->GetPositionY())},
                   {"%posZ", QnpcFloat(npc->GetPositionZ())},
                   {"%o", QnpcFloat(npc->GetOrientation())},
                   {"%phaseMask", std::to_string(npc->GetPhaseMask())}}));
    tell(QnpcText("qnpc_template",
                  "QNPC template: level=%min-%max rank=%rank faction=%faction npcflag=%npcflag unitFlags=%unitFlags "
                  "speedWalk=%speedWalk speedRun=%speedRun movementType=%movementType AI='%ai' scriptId=%scriptId "
                  "script='%script' lootId=%lootId pickpocketLootId=%pickpocketLootId skinLootId=%skinLootId",
                  {{"%min", std::to_string(info->minlevel)},
                   {"%max", std::to_string(info->maxlevel)},
                   {"%rank", std::to_string(info->rank)},
                   {"%faction", std::to_string(info->faction)},
                   {"%npcflag", QnpcHex(info->npcflag)},
                   {"%unitFlags", QnpcHex(info->unit_flags)},
                   {"%speedWalk", QnpcFloat(info->speed_walk)},
                   {"%speedRun", QnpcFloat(info->speed_run)},
                   {"%movementType", std::to_string(info->MovementType)},
                   {"%ai", info->AIName},
                   {"%scriptId", std::to_string(info->ScriptID)},
                   {"%script", sObjectMgr->GetScriptName(info->ScriptID)},
                   {"%lootId", std::to_string(info->lootid)},
                   {"%pickpocketLootId", std::to_string(info->pickpocketLootId)},
                   {"%skinLootId", std::to_string(info->SkinLootId)}}));

    if (spawn)
        tell(QnpcText("qnpc_spawn",
                      "QNPC spawn: map=%map x=%x y=%y z=%posZ o=%o respawnSecs=%respawn phaseMask=%phaseMask "
                      "spawnMask=%spawnMask movementType=%movementType npcflag=%npcflag unitFlags=%unitFlags "
                      "waypoint=%waypoint wander=%wander curHealth=%curHealth curMana=%curMana poolId=%poolId "
                      "scriptId=%scriptId",
                      {{"%map", std::to_string(spawn->mapid)},
                       {"%x", QnpcFloat(spawn->posX)},
                       {"%y", QnpcFloat(spawn->posY)},
                       {"%posZ", QnpcFloat(spawn->posZ)},
                       {"%o", QnpcFloat(spawn->orientation)},
                       {"%respawn", std::to_string(spawn->spawntimesecs)},
                       {"%phaseMask", std::to_string(spawn->phaseMask)},
                       {"%spawnMask", QnpcHex(spawn->spawnMask)},
                       {"%movementType", std::to_string(spawn->movementType)},
                       {"%npcflag", QnpcHex(spawn->npcflag)},
                       {"%unitFlags", QnpcHex(spawn->unit_flags)},
                       {"%waypoint", std::to_string(spawn->currentwaypoint)},
                       {"%wander", QnpcFloat(spawn->wander_distance)},
                       {"%curHealth", std::to_string(spawn->curhealth)},
                       {"%curMana", std::to_string(spawn->curmana)},
                       {"%poolId", std::to_string(spawn->poolId)},
                       {"%scriptId", std::to_string(spawn->ScriptId)}}));

    tell(QnpcText("qnpc_runtime",
                  "QNPC runtime: alive=%alive health=%health/%maxHealth standState=%standState npcflag=%npcflag "
                  "unitFlags=%unitFlags faction=%faction reactState=%reactState motionType=%motionType isMoving=%isMoving "
                  "victim=%victim inCombat=%inCombat",
                  {{"%alive", QnpcBool(npc->IsAlive())},
                   {"%health", std::to_string(npc->GetHealth())},
                   {"%maxHealth", std::to_string(npc->GetMaxHealth())},
                   {"%standState", std::to_string(uint32(npc->GetByteValue(UNIT_FIELD_BYTES_1, 0)))},
                   {"%npcflag", QnpcHex(uint32(npc->GetNpcFlags()))},
                   {"%unitFlags", QnpcHex(uint32(npc->GetUnitFlags()))},
                   {"%faction", std::to_string(npc->GetFaction())},
                   {"%reactState", std::to_string(uint32(npc->GetReactState()))},
                   {"%motionType", std::to_string(uint32(npc->GetMotionMaster()->GetCurrentMovementGeneratorType()))},
                   {"%isMoving", QnpcBool(npc->isMoving())},
                   {"%victim", npc->GetVictim() ? npc->GetVictim()->GetGUID().ToString() : "none"},
                   {"%inCombat", QnpcBool(npc->IsInCombat())}}));

    tell(QnpcText("qnpc_bot",
                  "QNPC bot: name=%name distance=%distance dist2d=%dist2d withinInteract=%withinInteract los=%los "
                  "samePhase=%samePhase canSee=%canSee",
                  {{"%name", bot->GetName()},
                   {"%distance", QnpcFloat(bot->GetDistance(npc))},
                   {"%dist2d", QnpcFloat(bot->GetDistance2d(npc))},
                   {"%withinInteract", QnpcBool(npc->IsWithinDistInMap(bot, INTERACTION_DISTANCE))},
                   {"%los", QnpcBool(npc->IsWithinLOSInMap(bot))},
                   {"%samePhase", QnpcBool(npc->InSamePhase(bot))},
                   {"%canSee", QnpcBool(bot->CanSeeOrDetect(npc))}}));

    NewRpgInfo& rpg = botAI->rpgInfo;
    if (auto const* doQuest = std::get_if<NewRpgInfo::DoQuest>(&rpg.data))
        tell(QnpcText("qnpc_do_quest",
                      "QNPC live doQuest: id=%id status=%status objective=%objective target=%target",
                      {{"%id", std::to_string(doQuest->questId)},
                       {"%status", std::to_string(uint32(bot->GetQuestStatus(doQuest->questId)))},
                       {"%objective", std::to_string(doQuest->objectiveIdx)},
                       {"%target", doQuest->targetGuid ? doQuest->targetGuid.ToString() : "none"}}));
    else
        tell(QnpcText("qnpc_do_quest_none", "QNPC live doQuest: none", {}));

    tell(QnpcText("qnpc_escort_state",
                  "QNPC escort state: quest=%quest guid=%guid started=%started last=%x,%y,%z map=%map",
                  {{"%quest", std::to_string(rpg.escortQuestId)},
                   {"%guid", rpg.escortGuid ? rpg.escortGuid.ToString() : "none"},
                   {"%started", QnpcBool(rpg.escortStarted)},
                   {"%x", QnpcFloat(rpg.lastEscortPos.GetPositionX())},
                   {"%y", QnpcFloat(rpg.lastEscortPos.GetPositionY())},
                   {"%z", QnpcFloat(rpg.lastEscortPos.GetPositionZ())},
                   {"%map", std::to_string(rpg.lastEscortPos.GetMapId())}}));

    bool printedEscort = false;
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;

        uint32 const escortEntry = GetQuestEscortEntry(questId);
        auto const& starters = GetQuestStarterEntries(questId);
        auto const& enders = GetQuestEnderEntries(questId);
        bool const related =
            escortEntry == npc->GetEntry() || std::find(starters.begin(), starters.end(), int32(npc->GetEntry())) != starters.end() ||
            std::find(enders.begin(), enders.end(), int32(npc->GetEntry())) != enders.end();
        if (!related)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        bool explored = false;
        auto const& statusMap = bot->getQuestStatusMap();
        auto statusIt = statusMap.find(questId);
        if (statusIt != statusMap.end())
            explored = statusIt->second.Explored;

        Creature* escort = escortEntry ? NewRpgBaseAction::FindEscorteeForQuest(bot, questId, 100.0f) : nullptr;

        tell(QnpcText("qnpc_quest",
                      "QNPC bot quest %questId '%title' status=%status explored=%explored escortEntry=%escortEntry "
                      "findEscortee=%findEscortee starter=%starter ender=%ender",
                      {{"%questId", std::to_string(questId)},
                       {"%title", quest->GetTitle()},
                       {"%status", std::to_string(uint32(bot->GetQuestStatus(questId)))},
                       {"%explored", QnpcBool(explored)},
                       {"%escortEntry", escortEntry ? std::to_string(escortEntry) : "none"},
                       {"%findEscortee", escort ? escort->GetGUID().ToString() : "none"},
                       {"%starter", std::find(starters.begin(), starters.end(), int32(npc->GetEntry())) != starters.end()
                                       ? QnpcBool(true)
                                       : QnpcBool(false)},
                       {"%ender", std::find(enders.begin(), enders.end(), int32(npc->GetEntry())) != enders.end()
                                     ? QnpcBool(true)
                                     : QnpcBool(false)}}));

        if (escortEntry && !printedEscort)
        {
            size_t spawnCount = GetEntrySpawns(int32(escortEntry), bot->GetMapId()).size();
            tell(QnpcText("qnpc_escort", "QNPC escort: entry=%entry mapSpawns=%spawns",
                          {{"%entry", std::to_string(escortEntry)}, {"%spawns", std::to_string(spawnCount)}}));
            printedEscort = true;
        }
    }
}
