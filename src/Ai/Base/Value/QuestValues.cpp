/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "QuestValues.h"
#include "DatabaseEnv.h"
#include "GameObjectData.h"
#include "LootValues.h"
#include "MapMgr.h"
#include "Playerbots.h"
#include "QuestAction.h"
#include "SharedValueContext.h"
#include "SmartScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// What kind of a relation does this entry have with this quest.
entryQuestRelationMap EntryQuestRelationMapValue::Calculate()
{
    entryQuestRelationMap rMap;

    for (auto relation : *sObjectMgr->GetCreatureQuestRelationMap())
        rMap[relation.first][relation.second] |= (int)QuestRelationFlag::questGiver;

    for (auto relation : *sObjectMgr->GetCreatureQuestInvolvedRelationMap())
        rMap[relation.first][relation.second] |= (int)QuestRelationFlag::questTaker;

    for (auto relation : *sObjectMgr->GetGOQuestRelationMap())
        rMap[-(int32)relation.first][relation.second] |= (int)QuestRelationFlag::questGiver;

    for (auto relation : *sObjectMgr->GetGOQuestInvolvedRelationMap())
        rMap[-(int32)relation.first][relation.second] |= (int)QuestRelationFlag::questGiver;

    // Quest objectives
    ObjectMgr::QuestMap const& questMap = sObjectMgr->GetQuestTemplates();

    for (auto& questItr : questMap)
    {
        uint32 questId = questItr.first;
        Quest* quest = questItr.second;

        for (uint32 objective = 0; objective < QUEST_OBJECTIVES_COUNT; objective++)
        {
            uint32 relationFlag = 1 << objective;

            // Kill objective
            if (quest->RequiredNpcOrGo[objective])
                rMap[quest->RequiredNpcOrGo[objective]][questId] |= relationFlag;

            // Loot objective
            if (quest->RequiredItemId[objective])
            {
                for (auto& entry : GAI_VALUE2(std::vector<int32>, "item drop list", quest->RequiredItemId[objective]))
                    rMap[entry][questId] |= relationFlag;
            }
        }
    }

    return rMap;
}

// Get all the objective entries for a specific quest.
void FindQuestObjectData::GetObjectiveEntries()
{
    relationMap = GAI_VALUE(entryQuestRelationMap, "entry quest relation");
}

// Every entry a spawn point can hold: the base from `creature`, plus up to two
// `creature_multispawn` alternates rolled between on respawn. 0 = no alternate.
static std::array<uint32, 3> SpawnEntries(CreatureData const& creData)
{
    return { creData.id, creData.id2, creData.id3 };
}

// Data worker. Checks for a specific creature what quest they are needed for and puts them in the proper place in the
// quest map.
void FindQuestObjectData::operator()(CreatureData const& creData)
{
    // The GuidPosition keeps the spawn's base entry, which can disagree with the alternate it
    // is filed under. It resolves by spawnId, and consumers read the map key.
    for (uint32 entry : SpawnEntries(creData))
    {
        if (!entry)
            continue;

        auto relations = relationMap.find(int32(entry));
        if (relations == relationMap.end())
            continue;

        for (auto& relation : relations->second)
        {
            uint32 questId = relation.first;
            uint32 flag = relation.second;
            data[questId][flag][entry].push_back(GuidPosition(creData));
        }
    }
}

// GameObject data worker. Checks for a specific gameObject what quest they are needed for and puts them in the proper
// place in the quest map.
void FindQuestObjectData::operator()(GameObjectData const& goData)
{
    int32 entry = goData.id * -1;

    for (auto& relation : relationMap[entry])
    {
        uint32 questId = relation.first;
        uint32 flag = relation.second;
        data[questId][flag][entry].push_back(GuidPosition(goData));
    }
}

// Goes past all creatures and gameobjects and creatures the full quest guid map.
questGuidpMap QuestGuidpMapValue::Calculate()
{
    FindQuestObjectData worker;
    for (auto const& itr : sObjectMgr->GetAllCreatureData())
        worker(itr.second);
    for (auto const& itr : sObjectMgr->GetAllGOData())
        worker(itr.second);

    return worker.GetResult();
}

// Entry keys: negative = gameobject.
static std::unordered_map<int32, std::unordered_map<uint32, std::vector<GuidPosition>>> g_entrySpawns;

static std::unordered_map<uint32, std::unordered_map<uint32, std::vector<int32>>> g_questObjectiveEntries;

static std::unordered_map<uint32, std::vector<int32>> g_questEnderEntries;

// Quest starters (givers), for escortee candidates and future resolution needs. Negative = GO.
static std::unordered_map<uint32, std::vector<int32>> g_questStarterEntries;

// Escort quests: quest -> the SmartAI-escorted NPC (SMART_ACTION_ESCORT_START owner).
static std::unordered_map<uint32, uint32> g_questEscortEntries;

// Above this an item objective is a world drop, with no useful spawn to walk to.
static constexpr size_t MAX_ITEM_OBJECTIVE_SPAWNS = 500;

static std::unordered_map<uint32, std::unordered_set<uint32>> g_questTransformSources;

static std::unordered_map<uint32, std::array<QuestSummonAnchor, QUEST_OBJECTIVES_COUNT>> g_questSummonAnchors;

static std::unordered_map<uint32, std::array<std::vector<uint32>, QUEST_OBJECTIVES_COUNT>> g_questKillSources;

// Spellhit credit markers: objective entry -> trigger spells that make the source cast the credit.
static std::unordered_map<uint32, std::array<std::vector<uint32>, QUEST_OBJECTIVES_COUNT>> g_questAbilitySpells;

// Objective creature -> items whose use-spell carries the KILL_CREDIT for it (feed-style quests).
static std::unordered_map<uint32, std::array<std::vector<uint32>, QUEST_OBJECTIVES_COUNT>> g_questCreditItems;

// Objective item -> the item whose use-spell creates it, the corpse entries its spell demands,
// and the openable container that holds the creator item (vial-style quests).
static std::unordered_map<uint32, std::array<uint32, QUEST_ITEM_OBJECTIVES_COUNT>> g_questCreationItems;
static std::unordered_map<uint32, std::array<std::vector<int32>, QUEST_ITEM_OBJECTIVES_COUNT>> g_questCreationSources;
static std::unordered_map<uint32, std::array<uint32, QUEST_ITEM_OBJECTIVES_COUNT>> g_questCreationContainers;
// Objective item -> the spell focus its creating use-spell demands (moonwell-style quests).
static std::unordered_map<uint32, std::array<uint32, QUEST_ITEM_OBJECTIVES_COUNT>> g_questCreationFocus;

// Quests whose start item completes the quest by being used on a specific (dead) creature.
static std::unordered_map<uint32, QuestScriptedItemTarget> g_questScriptedItemTargets;

// Spell-focus id -> gameobject entries (used to walk to focus objects inside interiors).
static std::unordered_map<uint32, std::vector<uint32>> g_focusObjectEntries;

static std::unordered_map<uint32, std::vector<QuestCraftReagent>> g_questItemReagents;

static constexpr uint32 REAGENT_STOCK_CASTS = 5;

static std::unordered_map<uint32, std::array<std::vector<QuestCraftReagent>, QUEST_ITEM_OBJECTIVES_COUNT>>
    g_questCraftReagents;

static std::unordered_map<uint32, std::array<uint32, QUEST_ITEM_OBJECTIVES_COUNT>> g_questCraftItems;

static uint32 GetItemSpell(uint32 itemId)
{
    ItemTemplate const* proto = itemId ? sObjectMgr->GetItemTemplate(itemId) : nullptr;
    if (!proto)
        return 0;

    for (auto const& spell : proto->Spells)
        if (spell.SpellId > 0)
            return spell.SpellId;

    return 0;
}

static uint32 GetSrcItemSpell(Quest const* quest) { return GetItemSpell(quest->GetSrcItemId()); }

static uint32 SpellKillCreditEntry(uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info)
        return 0;

    for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
        if ((info->Effects[e].Effect == SPELL_EFFECT_KILL_CREDIT ||
             info->Effects[e].Effect == SPELL_EFFECT_KILL_CREDIT2) &&
            info->Effects[e].MiscValue > 0)
            return uint32(info->Effects[e].MiscValue);

    return 0;
}

static uint32 SpellSummonEntry(uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info)
        return 0;

    for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
        if (info->Effects[e].Effect == SPELL_EFFECT_SUMMON && info->Effects[e].MiscValue > 0)
            return uint32(info->Effects[e].MiscValue);

    return 0;
}

// Everything the quest indexes need from SmartAI, in one pass over the scripts the core already
// validated and loaded: spell triggers, credit sources, transforms, anchors and escorts.
struct SmartQuestIndex
{
    std::unordered_map<uint32, std::vector<std::pair<uint32, uint32>>> spellTransforms;  // spell -> (pre, post)
    std::unordered_map<uint32, std::vector<uint32>> anchorHints;   // closest_target pairs, both ways
    std::unordered_map<uint32, std::vector<uint32>> deathAnchors;  // credit entry -> reactors acting on death
    std::unordered_map<uint32, std::vector<uint32>> killSources;   // credited entry -> creatures serving it
    std::unordered_map<uint32, std::vector<uint32>> spellhitTriggers;  // credited entry -> trigger spells
    std::unordered_map<uint32, uint32> escortEntries;              // quest -> escorted creature
};

static SmartQuestIndex BuildSmartQuestIndex()
{
    struct EscortList
    {
        uint32 quest;
        uint32 listId;
        uint32 owner;
    };

    SmartQuestIndex index;
    std::vector<EscortList> escortLists;

    if (CreatureTemplateContainer const* creatures = sObjectMgr->GetCreatureTemplates())
        for (auto const& [entry, tmpl] : *creatures)
        {
            if (tmpl.AIName != "SmartAI")
                continue;

            for (SmartScriptHolder const& holder : sSmartScriptMgr->GetScript(int32(entry), SMART_SCRIPT_TYPE_CREATURE))
            {
                uint32 const actionType = holder.GetActionType();
                uint32 const actionParam = holder.action.raw.param1;

                if (actionType == SMART_ACTION_UPDATE_TEMPLATE && holder.GetEventType() == SMART_EVENT_SPELLHIT &&
                    holder.event.raw.param1 && actionParam)
                {
                    index.spellTransforms[holder.event.raw.param1].emplace_back(entry, actionParam);
                }
                else if (actionType == SMART_ACTION_CAST && actionParam)
                {
                    uint32 const credited = SpellKillCreditEntry(actionParam);
                    uint32 const wanted = credited ? credited : SpellSummonEntry(actionParam);
                    if (wanted && wanted != entry)
                        index.killSources[wanted].push_back(entry);

                    // event 8 = SMART_EVENT_SPELLHIT: the trigger spell landing on the owner makes
                    // it cast the credit spell.
                    if (credited && holder.GetEventType() == SMART_EVENT_SPELLHIT && holder.event.raw.param1)
                    {
                        auto& triggers = index.spellhitTriggers[credited];
                        if (std::find(triggers.begin(), triggers.end(), holder.event.raw.param1) == triggers.end())
                            triggers.push_back(holder.event.raw.param1);
                    }
                }
                else if (actionType == SMART_ACTION_CALL_KILLEDMONSTER && actionParam && actionParam != entry)
                {
                    index.killSources[actionParam].push_back(entry);
                }
                else if (actionType == SMART_ACTION_ESCORT_START)
                {
                    // Quest-accept rows bind the quest in the event, other triggers through the action.
                    if (holder.GetEventType() == SMART_EVENT_ACCEPTED_QUEST && holder.event.raw.param1)
                        index.escortEntries.emplace(holder.event.raw.param1, entry);
                    if (holder.action.raw.param4)
                        index.escortEntries.emplace(holder.action.raw.param4, entry);
                }
                else if (actionType == SMART_ACTION_CALL_TIMED_ACTIONLIST &&
                         holder.GetEventType() == SMART_EVENT_ACCEPTED_QUEST && holder.event.raw.param1 && actionParam)
                {
                    escortLists.push_back({holder.event.raw.param1, actionParam, entry});
                }

                // target_type 19 = SMART_TARGET_CLOSEST_CREATURE links a summoned trigger and the
                // NPC reacting to it; either side is a creature the item has to be used next to.
                uint32 const closest =
                    holder.GetTargetType() == SMART_TARGET_CLOSEST_CREATURE ? holder.target.raw.param1 : 0;
                if (closest && closest != uint32(entry))
                {
                    index.anchorHints[entry].push_back(closest);
                    index.anchorHints[closest].push_back(entry);
                    if (holder.GetEventType() == SMART_EVENT_DEATH)
                        index.deathAnchors[closest].push_back(entry);
                }
            }
        }

    // Timed action lists: the accept row hands control to a list whose script starts the escort;
    // the quest binds through the caller and the escorted creature is the owner.
    for (EscortList const& call : escortLists)
    {
        if (index.escortEntries.count(call.quest))
            continue;

        for (SmartScriptHolder const& holder :
             sSmartScriptMgr->GetScript(int32(call.listId), SMART_SCRIPT_TYPE_TIMED_ACTIONLIST))
            if (holder.GetActionType() == SMART_ACTION_ESCORT_START)
            {
                index.escortEntries.emplace(call.quest, call.owner);
                break;
            }
    }

    return index;
}

static bool SpellCraftsItem(uint32 spellId, uint32 itemId, std::vector<QuestCraftReagent>& reagents)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info)
        return false;

    bool creates = false;
    for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
        if (info->Effects[e].Effect == SPELL_EFFECT_CREATE_ITEM && info->Effects[e].ItemType == itemId)
            creates = true;

    if (!creates)
        return false;

    for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
        if (info->Reagent[r] > 0 && info->ReagentCount[r] > 0)
            reagents.push_back({uint32(info->Reagent[r]), info->ReagentCount[r]});

    return !reagents.empty();
}

static uint32 FindCraftReagents(Quest const* quest, uint32 itemId, std::vector<QuestCraftReagent>& reagents)
{
    if (SpellCraftsItem(GetItemSpell(quest->GetSrcItemId()), itemId, reagents))
        return quest->GetSrcItemId();

    for (uint8 i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
        if (SpellCraftsItem(GetItemSpell(quest->ItemDrop[i]), itemId, reagents))
            return quest->ItemDrop[i];

    return 0;
}

// 17 = CONDITION_SOURCE_TYPE_SPELL, 29 = CONDITION_NEAR_CREATURE.
static std::unordered_map<uint32, uint32> LoadSpellNearCreature()
{
    std::unordered_map<uint32, uint32> nearCreature;
    QueryResult result = WorldDatabase.Query(
        "SELECT SourceEntry, ConditionValue1 FROM conditions "
        "WHERE SourceTypeOrReferenceId = 17 AND ConditionTypeOrReference = 29 "
        "AND NegativeCondition = 0 AND ConditionValue1 > 0");
    if (!result)
        return nearCreature;

    do
    {
        Field* fields = result->Fetch();
        nearCreature[fields[0].Get<uint32>()] = fields[1].Get<uint32>();
    } while (result->NextRow());

    return nearCreature;
}

// 31 = CONDITION_OBJECT_ENTRY_GUID on the cast target (Value1 = type id, Value2 = entry),
// 36 = CONDITION_ALIVE; the vial-style spells demand specific corpse entries this way.
struct SpellTargetEntries
{
    std::vector<uint32> entries;
    bool requireDead{false};
};

static std::unordered_map<uint32, SpellTargetEntries> LoadSpellTargetEntries()
{
    std::unordered_map<uint32, SpellTargetEntries> targets;
    QueryResult result = WorldDatabase.Query(
        "SELECT SourceEntry, ConditionTypeOrReference, ConditionValue1, ConditionValue2, NegativeCondition "
        "FROM conditions WHERE SourceTypeOrReferenceId = 17 AND ConditionTypeOrReference IN (31, 36)");
    if (!result)
        return targets;

    do
    {
        Field* fields = result->Fetch();
        SpellTargetEntries& target = targets[fields[0].Get<uint32>()];
        if (fields[1].Get<uint32>() == 31)
        {
            if (fields[2].Get<uint32>() == uint32(TYPEID_UNIT) && fields[3].Get<uint32>() > 0)
                target.entries.push_back(fields[3].Get<uint32>());
        }
        else if (fields[4].Get<uint8>())
            target.requireDead = true;
    } while (result->NextRow());

    return targets;
}

// Items usable from the bag, split by what their use-spell does: plain spell -> items, and
// KILL_CREDIT spells -> the creature entry they credit (feed-style quest items).
static std::unordered_map<uint32, std::vector<uint32>> LoadItemUseSpells(
    std::unordered_map<uint32, std::vector<uint32>>& creditItems)
{
    std::unordered_map<uint32, std::vector<uint32>> useSpells;
    if (ItemTemplateContainer const* items = sObjectMgr->GetItemTemplateStore())
        for (auto const& [entry, proto] : *items)
            for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                if (proto.Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE || proto.Spells[i].SpellId <= 0)
                    continue;

                uint32 const spellId = proto.Spells[i].SpellId;
                auto& casters = useSpells[spellId];
                if (std::find(casters.begin(), casters.end(), entry) == casters.end())
                    casters.push_back(entry);

                if (uint32 credited = SpellKillCreditEntry(spellId))
                {
                    auto& carriers = creditItems[credited];
                    if (std::find(carriers.begin(), carriers.end(), entry) == carriers.end())
                        carriers.push_back(entry);
                }
            }

    return useSpells;
}

static void CollectWantedDrops(LootTemplateAccess const* loot, std::unordered_set<uint32> const& wanted, int32 entry,
                               std::unordered_map<uint32, std::vector<int32>>& dropSources, uint32 depth = 0)
{
    // References nest a level or two in practice; the guard is only against a cyclic table.
    if (!loot || depth > 3)
        return;

    for (auto const& item : loot->Entries)
    {
        // Negative references are ordinary in the shipped tables; the core uses the absolute value.
        if (item->reference)
        {
            CollectWantedDrops(reinterpret_cast<LootTemplateAccess const*>(
                                   LootTemplates_Reference.GetLootFor(uint32(std::abs(item->reference)))),
                               wanted, entry, dropSources, depth + 1);
            continue;
        }

        if (wanted.count(item->itemid))
            dropSources[item->itemid].push_back(entry);
    }
}

static void FileSpawnPositions(std::unordered_map<int32, std::vector<std::pair<uint32, uint32>>> const& entryMap)
{
    for (auto const& [spawnId, creData] : sObjectMgr->GetAllCreatureData())
    {
        // Filed under each wanted alternate entry: the respawn decides which one appears.
        for (uint32 entry : SpawnEntries(creData))
        {
            if (entry && entryMap.count(int32(entry)))
                g_entrySpawns[int32(entry)][creData.mapid].push_back(GuidPosition(creData));
        }
    }

    for (auto const& [spawnId, goData] : sObjectMgr->GetAllGOData())
    {
        if (entryMap.count(-int32(goData.id)))
            g_entrySpawns[-int32(goData.id)][goData.mapid].push_back(GuidPosition(goData));
    }
}

static size_t GetEntrySpawnCount(int32 entry)
{
    auto it = g_entrySpawns.find(entry);
    if (it == g_entrySpawns.end())
        return 0;

    size_t total = 0;
    for (auto const& [mapId, positions] : it->second)
        total += positions.size();

    return total;
}

static void FileObjectiveEntries(std::unordered_map<int32, std::vector<std::pair<uint32, uint32>>> const& entryMap)
{
    uint32 constexpr firstItemFlag = 1u << QUEST_OBJECTIVES_COUNT;

    std::unordered_map<int32, std::vector<std::pair<uint32, uint32>>> spawned;
    for (auto const& [entry, wants] : entryMap)
    {
        if (!g_entrySpawns.count(entry))
            continue;

        std::vector<std::pair<uint32, uint32>> objectives = wants;
        std::sort(objectives.begin(), objectives.end());
        objectives.erase(std::unique(objectives.begin(), objectives.end()), objectives.end());
        spawned.emplace(entry, std::move(objectives));
    }

    // The world-drop cap applies to the objective's total spawns, not to each dropper.
    std::unordered_map<uint32, std::unordered_map<uint32, size_t>> spawnTotals;
    for (auto const& [entry, wants] : spawned)
        for (auto const& [questId, flag] : wants)
            spawnTotals[questId][flag] += GetEntrySpawnCount(entry);

    for (auto const& [entry, wants] : spawned)
    {
        for (auto const& [questId, flag] : wants)
        {
            if (flag >= firstItemFlag && spawnTotals[questId][flag] > MAX_ITEM_OBJECTIVE_SPAWNS)
                continue;

            g_questObjectiveEntries[questId][flag].push_back(entry);
        }
    }
}

void BuildQuestObjectiveSpawns()
{
    // Appends to the globals below, so a second call would double every spawn vector.
    if (!g_entrySpawns.empty())
        return;

    // Built unconditionally: the runtime flags gate behaviour, not data, so a config reload can
    // enable a feature without a restart.
    SmartQuestIndex const smartIndex = BuildSmartQuestIndex();

    std::unordered_map<int32, std::vector<std::pair<uint32, uint32>>> entryMap;
    std::unordered_set<uint32> questItems;

    // Enders get an empty want-list: spawn positions only, no objective.
    for (auto const& [entry, questId] : *sObjectMgr->GetCreatureQuestInvolvedRelationMap())
    {
        g_questEnderEntries[questId].push_back(int32(entry));
        entryMap[int32(entry)];
    }

    for (auto const& [entry, questId] : *sObjectMgr->GetGOQuestInvolvedRelationMap())
    {
        g_questEnderEntries[questId].push_back(-int32(entry));
        entryMap[-int32(entry)];
    }

    // Starters get no spawn indexing here: they are only an escortee candidate source.
    for (auto const& [entry, questId] : *sObjectMgr->GetCreatureQuestRelationMap())
        g_questStarterEntries[questId].push_back(int32(entry));

    for (auto const& [entry, questId] : *sObjectMgr->GetGOQuestRelationMap())
        g_questStarterEntries[questId].push_back(-int32(entry));

    g_questEscortEntries = smartIndex.escortEntries;
    for (auto const& escort : g_questEscortEntries)
        if (escort.second)
            entryMap[int32(escort.second)];

    // Quest-item linkers: player-cast credit spells carried by items, and item-spells that create
    // objective items on demand (with the corpse entries their conditions demand).
    std::unordered_map<uint32, std::vector<uint32>> creditSpellItems;
    auto const itemUseSpells = LoadItemUseSpells(creditSpellItems);
    auto const spellTargetEntries = LoadSpellTargetEntries();

    std::unordered_map<uint32, std::vector<uint32>> creationItems;
    for (auto const& [spellId, casters] : itemUseSpells)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info)
            continue;

        for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
            if (info->Effects[e].Effect == SPELL_EFFECT_CREATE_ITEM && info->Effects[e].ItemType > 0)
            {
                auto& creators = creationItems[info->Effects[e].ItemType];
                creators.insert(creators.end(), casters.begin(), casters.end());
            }
    }

    for (auto const& [questId, quest] : sObjectMgr->GetQuestTemplates())
    {
        for (uint32 idx = 0; idx < QUEST_OBJECTIVES_COUNT; ++idx)
        {
            int32 const entry = quest->RequiredNpcOrGo[idx];
            if (entry && quest->RequiredNpcOrGoCount[idx])
            {
                entryMap[entry].emplace_back(questId, 1u << idx);

                // A player-cast spell carried by an item credits this objective: index the item's
                // droppers as objective sources and remember the item for the use step.
                if (auto it = creditSpellItems.find(uint32(entry)); it != creditSpellItems.end())
                {
                    for (uint32 item : it->second)
                    {
                        questItems.insert(item);
                        g_questCreditItems[questId][idx].push_back(item);
                    }
                }
            }
        }

        uint32 const srcSpellId = GetSrcItemSpell(quest);

        if (SpellInfo const* srcSpell = sSpellMgr->GetSpellInfo(srcSpellId))
            for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
                if (srcSpell->Reagent[r] > 0 && srcSpell->ReagentCount[r] > 0)
                    g_questItemReagents[questId].push_back({uint32(srcSpell->Reagent[r]), srcSpell->ReagentCount[r]});

        for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            if (!quest->RequiredItemId[i] || !quest->RequiredItemCount[i])
                continue;

            questItems.insert(quest->RequiredItemId[i]);

            std::vector<QuestCraftReagent> reagents;
            uint32 const crafter = FindCraftReagents(quest, quest->RequiredItemId[i], reagents);
            if (reagents.empty())
                continue;

            for (QuestCraftReagent const& reagent : reagents)
                questItems.insert(reagent.item);

            questItems.insert(crafter);

            g_questCraftReagents[questId][i] = std::move(reagents);
            g_questCraftItems[questId][i] = crafter;
        }

        // An item objective that no mob drops but an item-spell creates: index the corpse entries
        // its spell's conditions demand so the objective is served by casting there.
        for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            if (!quest->RequiredItemId[i] || !quest->RequiredItemCount[i])
                continue;

            auto it = creationItems.find(quest->RequiredItemId[i]);
            if (it == creationItems.end())
                continue;

            for (uint32 creator : it->second)
            {
                uint32 const useSpell = GetItemSpell(creator);
                SpellInfo const* useInfo = sSpellMgr->GetSpellInfo(useSpell);
                if (!useInfo)
                    continue;

                // A focus-requiring creation spell has no corpse target: the bot must stand next
                // to the spell focus object instead.
                if (useInfo->RequiresSpellFocus)
                {
                    g_questCreationItems[questId][i] = creator;
                    g_questCreationFocus[questId][i] = useInfo->RequiresSpellFocus;
                    break;
                }

                auto const& targets = spellTargetEntries.find(useSpell);
                if (targets == spellTargetEntries.end() || targets->second.entries.empty())
                    continue;

                g_questCreationItems[questId][i] = creator;
                g_questCreationSources[questId][i].assign(targets->second.entries.begin(),
                                                          targets->second.entries.end());

                uint32 const flag = 1u << (QUEST_OBJECTIVES_COUNT + i);
                for (uint32 sourceEntry : targets->second.entries)
                    entryMap[int32(sourceEntry)].emplace_back(questId, flag);
                break;
            }
        }
    }

    std::unordered_map<uint32, std::vector<int32>> dropSources;
    if (!questItems.empty())
    {
        if (CreatureTemplateContainer const* creatures = sObjectMgr->GetCreatureTemplates())
        {
            for (auto const& [entry, tmpl] : *creatures)
                CollectWantedDrops(
                    DropMapValue::GetLootTemplate(ObjectGuid::Create<HighGuid::Unit>(entry, uint32(1)), LOOT_CORPSE),
                    questItems, int32(entry), dropSources);
        }

        if (GameObjectTemplateContainer const* gos = sObjectMgr->GetGameObjectTemplates())
        {
            for (auto const& [entry, tmpl] : *gos)
                CollectWantedDrops(DropMapValue::GetLootTemplate(
                                       ObjectGuid::Create<HighGuid::GameObject>(entry, uint32(1)), LOOT_CORPSE),
                                   questItems, -int32(entry), dropSources);
        }
    }

    std::unordered_set<uint32> spawnedEntries;
    for (auto const& [spawnId, creData] : sObjectMgr->GetAllCreatureData())
        for (uint32 entry : SpawnEntries(creData))
            if (entry)
                spawnedEntries.insert(entry);

    auto const& spellTransforms = smartIndex.spellTransforms;

    std::unordered_map<uint32, std::vector<uint32>> focusObjects;
    if (GameObjectTemplateContainer const* gos = sObjectMgr->GetGameObjectTemplates())
        for (auto const& [goEntry, goInfo] : *gos)
            if (goInfo.type == GAMEOBJECT_TYPE_SPELL_FOCUS && goInfo.spellFocus.focusId)
                focusObjects[goInfo.spellFocus.focusId].push_back(goEntry);

    // File the focus object spawns as well, so interiors can be approached without relying on
    // the bot's loaded visibility (3883 Gorishi Hive, 921 Shadowglen Moonwell, ...).
    for (auto const& [focusId, entries] : focusObjects)
    {
        g_focusObjectEntries[focusId] = entries;
        for (uint32 goEntry : entries)
            entryMap[-int32(goEntry)];
    }

    auto const& deathAnchors = smartIndex.deathAnchors;
    auto const& smartAnchorHints = smartIndex.anchorHints;
    auto const& objectiveKillSources = smartIndex.killSources;
    auto const& spellhitTriggers = smartIndex.spellhitTriggers;
    auto const spellNearCreature = LoadSpellNearCreature();

    for (auto const& [questId, quest] : sObjectMgr->GetQuestTemplates())
    {
        uint32 const srcSpellId = GetSrcItemSpell(quest);
        SpellInfo const* srcSpell = sSpellMgr->GetSpellInfo(srcSpellId);
        auto const craftIt = g_questCraftReagents.find(questId);

        for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            if (!quest->RequiredItemId[i] || !quest->RequiredItemCount[i])
                continue;

            std::vector<uint32> wanted{quest->RequiredItemId[i]};
            if (craftIt != g_questCraftReagents.end() && !craftIt->second[i].empty())
            {
                wanted.clear();
                for (QuestCraftReagent const& reagent : craftIt->second[i])
                    wanted.push_back(reagent.item);

                if (uint32 const crafter = GetQuestCraftItem(questId, i); crafter && crafter != quest->GetSrcItemId())
                    wanted.push_back(crafter);
            }

            uint32 const flag = 1u << (QUEST_OBJECTIVES_COUNT + i);
            for (uint32 wantedItem : wanted)
            {
                auto src = dropSources.find(wantedItem);
                if (src == dropSources.end())
                    continue;

                for (int32 entry : src->second)
                {
                    // An unspawned dropper only exists after a SmartAI transform by the quest's item, so index the
                    // spawned pre-transform creature instead.
                    if (entry > 0 && !spawnedEntries.count(uint32(entry)))
                    {
                        auto tf = spellTransforms.find(srcSpellId);
                        if (tf == spellTransforms.end())
                            continue;

                        for (auto const& [pre, post] : tf->second)
                        {
                            if (post != uint32(entry) || !spawnedEntries.count(pre))
                                continue;

                            entryMap[int32(pre)].emplace_back(questId, flag);
                            g_questTransformSources[questId].insert(pre);
                        }
                        continue;
                    }

                    entryMap[entry].emplace_back(questId, flag);
                }
            }
        }

        for (uint32 idx = 0; idx < QUEST_OBJECTIVES_COUNT; ++idx)
        {
            int32 const entry = quest->RequiredNpcOrGo[idx];
            if (entry <= 0 || !quest->RequiredNpcOrGoCount[idx])
                continue;

            uint32 const flag = 1u << idx;
            bool const objectiveSpawned = spawnedEntries.count(uint32(entry)) != 0;
            // A spawned marker (non-selectable or player-immune) cannot be engaged; its kill
            // sources serve the objective exactly as they do for an unspawned one.
            bool const objectiveEngageable = IsEngageableCreatureEntry(uint32(entry));
            bool const resolveCredits = !objectiveSpawned || !objectiveEngageable;

            std::vector<uint32> credits;
            if (resolveCredits)
            {
                auto addCredit = [&credits, &spawnedEntries](uint32 source)
                {
                    if (spawnedEntries.count(source) &&
                        std::find(credits.begin(), credits.end(), source) == credits.end())
                        credits.push_back(source);
                };

                if (auto it = objectiveKillSources.find(uint32(entry)); it != objectiveKillSources.end())
                    for (uint32 source : it->second)
                        addCredit(source);

                if (auto it = deathAnchors.find(uint32(entry)); it != deathAnchors.end())
                    for (uint32 source : it->second)
                        addCredit(source);

                for (uint32 source : credits)
                    entryMap[int32(source)].emplace_back(questId, flag);

                if (!credits.empty())
                    g_questKillSources[questId][idx] = credits;
            }

            if (!objectiveEngageable)
                if (auto it = spellhitTriggers.find(uint32(entry)); it != spellhitTriggers.end())
                    g_questAbilitySpells[questId][idx] = it->second;

            if (!srcSpell)
                continue;

            bool summoned = false;
            for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
                if (srcSpell->Effects[e].Effect == SPELL_EFFECT_SUMMON && srcSpell->Effects[e].MiscValue == entry)
                    summoned = true;

            // Spell focus wins: the core enforces it, even for an objective that is already spawned.
            if (srcSpell->RequiresSpellFocus)
            {
                g_questSummonAnchors[questId][idx].focusId = srcSpell->RequiresSpellFocus;
                g_questSummonAnchors[questId][idx].summonsObjective = summoned;
                if (!objectiveSpawned)
                    for (uint32 goEntry : focusObjects[srcSpell->RequiresSpellFocus])
                        entryMap[-int32(goEntry)].emplace_back(questId, flag);
                continue;
            }

            if (!summoned || objectiveSpawned)
                continue;

            uint32 anchor = 0;
            if (auto it = spellNearCreature.find(srcSpellId);
                it != spellNearCreature.end() && spawnedEntries.count(it->second))
                anchor = it->second;

            if (!anchor)
                if (auto it = smartAnchorHints.find(uint32(entry)); it != smartAnchorHints.end())
                    for (uint32 hint : it->second)
                        if (spawnedEntries.count(hint))
                        {
                            anchor = hint;
                            break;
                        }

            if (!anchor && !credits.empty())
                anchor = credits.front();

            if (!anchor)
                continue;

            bool const killAnchor = std::find(credits.begin(), credits.end(), anchor) != credits.end();
            g_questSummonAnchors[questId][idx].creature = anchor;
            g_questSummonAnchors[questId][idx].killAnchor = killAnchor;
            g_questSummonAnchors[questId][idx].summonsObjective = true;
            if (!killAnchor)
                entryMap[int32(anchor)].emplace_back(questId, flag);
        }

        // Scripted event quests (no NPC/item objectives) whose start item completes them on a
        // specific dead creature, e.g. 2561 "Druid of the Claw" (Voodoo Charm on Rageclaw).
        if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT))
        {
            bool hasObjective = false;
            for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                if (quest->RequiredNpcOrGo[i] && quest->RequiredNpcOrGoCount[i])
                    hasObjective = true;
            for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
                if (quest->RequiredItemId[i] && quest->RequiredItemCount[i])
                    hasObjective = true;

            if (!hasObjective)
            {
                uint32 const itemId = quest->GetSrcItemId();
                uint32 const spellId = GetItemSpell(itemId);
                SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId);
                if (spell)
                {
                    bool completesQuest = false;
                    for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
                        if (spell->Effects[e].Effect == SPELL_EFFECT_QUEST_COMPLETE &&
                            uint32(spell->Effects[e].MiscValue) == questId)
                        {
                            completesQuest = true;
                            break;
                        }

                    auto targetIt = spellTargetEntries.find(spellId);
                    if (completesQuest && targetIt != spellTargetEntries.end() && !targetIt->second.entries.empty())
                    {
                        QuestScriptedItemTarget target;
                        target.itemId = itemId;
                        target.creatureEntry = targetIt->second.entries.front();
                        target.requireDead = targetIt->second.requireDead;
                        g_questScriptedItemTargets[questId] = target;
                        entryMap[int32(target.creatureEntry)];
                    }
                }
            }
        }
    }

    FileSpawnPositions(entryMap);
    FileObjectiveEntries(entryMap);
}

std::vector<int32> const& GetQuestObjectiveEntries(uint32 questId, uint32 objectiveFlag)
{
    static std::vector<int32> const empty;
    auto quest = g_questObjectiveEntries.find(questId);
    if (quest == g_questObjectiveEntries.end())
        return empty;

    auto flag = quest->second.find(objectiveFlag);
    return flag != quest->second.end() ? flag->second : empty;
}

std::vector<GuidPosition> const& GetEntrySpawns(int32 entry, uint32 mapId)
{
    static std::vector<GuidPosition> const empty;
    auto it = g_entrySpawns.find(entry);
    if (it == g_entrySpawns.end())
        return empty;

    auto onMap = it->second.find(mapId);
    return onMap != it->second.end() ? onMap->second : empty;
}

std::vector<int32> const& GetQuestEnderEntries(uint32 questId)
{
    static std::vector<int32> const empty;
    auto it = g_questEnderEntries.find(questId);
    return it != g_questEnderEntries.end() ? it->second : empty;
}

std::vector<int32> const& GetQuestStarterEntries(uint32 questId)
{
    static std::vector<int32> const empty;
    auto it = g_questStarterEntries.find(questId);
    return it != g_questStarterEntries.end() ? it->second : empty;
}

uint32 GetQuestEscortEntry(uint32 questId)
{
    auto it = g_questEscortEntries.find(questId);
    return it != g_questEscortEntries.end() ? it->second : 0;
}

std::unordered_set<uint32> const& GetQuestTransformSources(uint32 questId)
{
    static std::unordered_set<uint32> const empty;
    auto it = g_questTransformSources.find(questId);
    return it != g_questTransformSources.end() ? it->second : empty;
}

QuestSummonAnchor GetQuestSummonAnchor(uint32 questId, uint32 objectiveIdx)
{
    if (objectiveIdx >= QUEST_OBJECTIVES_COUNT)
        return {};

    auto it = g_questSummonAnchors.find(questId);
    return it != g_questSummonAnchors.end() ? it->second[objectiveIdx] : QuestSummonAnchor{};
}

std::vector<QuestCraftReagent> const& GetQuestItemReagents(uint32 questId)
{
    static std::vector<QuestCraftReagent> const empty;
    auto it = g_questItemReagents.find(questId);
    return it != g_questItemReagents.end() ? it->second : empty;
}

bool IsQuestExtraItemWanted(Player* bot, Quest const* quest, uint32 itemId)
{
    if (!bot || !quest || !itemId)
        return false;

    for (uint8 i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
    {
        if (quest->ItemDrop[i] != itemId)
            continue;

        uint32 const need = quest->ItemDropQuantity[i];
        return !need || bot->GetItemCount(itemId, false) < need;
    }

    for (QuestCraftReagent const& reagent : GetQuestItemReagents(quest->GetQuestId()))
    {
        if (reagent.item != itemId)
            continue;

        return bot->GetItemCount(itemId, false) < reagent.count * REAGENT_STOCK_CASTS;
    }

    return false;
}

std::vector<QuestCraftReagent> const& GetQuestCraftReagents(uint32 questId, uint32 itemObjectiveIdx)
{
    static std::vector<QuestCraftReagent> const empty;
    if (itemObjectiveIdx >= QUEST_ITEM_OBJECTIVES_COUNT)
        return empty;

    auto it = g_questCraftReagents.find(questId);
    return it != g_questCraftReagents.end() ? it->second[itemObjectiveIdx] : empty;
}

uint32 GetQuestCraftItem(uint32 questId, uint32 itemObjectiveIdx)
{
    if (itemObjectiveIdx >= QUEST_ITEM_OBJECTIVES_COUNT)
        return 0;

    auto it = g_questCraftItems.find(questId);
    return it != g_questCraftItems.end() ? it->second[itemObjectiveIdx] : 0;
}

std::vector<uint32> const& GetQuestKillSources(uint32 questId, uint32 objectiveIdx)
{
    static std::vector<uint32> const empty;
    if (objectiveIdx >= QUEST_OBJECTIVES_COUNT)
        return empty;

    auto it = g_questKillSources.find(questId);
    return it != g_questKillSources.end() ? it->second[objectiveIdx] : empty;
}

std::vector<uint32> const& GetQuestAbilitySpells(uint32 questId, uint32 objectiveIdx)
{
    static std::vector<uint32> const empty;
    if (objectiveIdx >= QUEST_OBJECTIVES_COUNT)
        return empty;

    auto it = g_questAbilitySpells.find(questId);
    return it != g_questAbilitySpells.end() ? it->second[objectiveIdx] : empty;
}

// A creature players cannot select or attack is a credit marker: never a destination, never a
// scan target - its objective is served by its kill sources instead.
bool IsEngageableCreatureEntry(uint32 entry)
{
    CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(entry);
    if (!tmpl)
        return false;

    return (tmpl->unit_flags & (UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PC)) == 0;
}

std::vector<uint32> const& GetQuestCreditItems(uint32 questId, uint32 objectiveIdx)
{
    static std::vector<uint32> const empty;
    if (objectiveIdx >= QUEST_OBJECTIVES_COUNT)
        return empty;

    auto it = g_questCreditItems.find(questId);
    return it != g_questCreditItems.end() ? it->second[objectiveIdx] : empty;
}

uint32 GetQuestCreationItem(uint32 questId, uint32 itemObjectiveIdx)
{
    if (itemObjectiveIdx >= QUEST_ITEM_OBJECTIVES_COUNT)
        return 0;

    auto it = g_questCreationItems.find(questId);
    return it != g_questCreationItems.end() ? it->second[itemObjectiveIdx] : 0;
}

uint32 GetQuestCreationFocus(uint32 questId, uint32 itemObjectiveIdx)
{
    if (itemObjectiveIdx >= QUEST_ITEM_OBJECTIVES_COUNT)
        return 0;

    auto it = g_questCreationFocus.find(questId);
    return it != g_questCreationFocus.end() ? it->second[itemObjectiveIdx] : 0;
}

std::vector<uint32> const& GetFocusObjectEntries(uint32 focusId)
{
    static std::vector<uint32> const empty;
    auto it = g_focusObjectEntries.find(focusId);
    return it != g_focusObjectEntries.end() ? it->second : empty;
}

QuestScriptedItemTarget const& GetQuestScriptedItemTarget(uint32 questId)
{
    static QuestScriptedItemTarget const empty;
    auto it = g_questScriptedItemTargets.find(questId);
    return it != g_questScriptedItemTargets.end() ? it->second : empty;
}

uint32 GetQuestAreaTriggerId(uint32 questId)
{
    // ObjectMgr indexes trigger -> quest only, so read the reverse straight off the table it
    // loads from. DoExploreObjective re-checks the trigger and the quest flag at use time.
    static std::unordered_map<uint32, uint32> const questToTrigger = []
    {
        std::unordered_map<uint32, uint32> m;
        if (QueryResult result = WorldDatabase.Query("SELECT id, quest FROM areatrigger_involvedrelation ORDER BY id"))
            do
            {
                m[(*result)[1].Get<uint32>()] = (*result)[0].Get<uint32>();
            } while (result->NextRow());
        return m;
    }();

    auto it = questToTrigger.find(questId);
    return it == questToTrigger.end() ? 0 : it->second;
}

std::vector<int32> const& GetQuestCreationSources(uint32 questId, uint32 itemObjectiveIdx)
{
    static std::vector<int32> const empty;
    if (itemObjectiveIdx >= QUEST_ITEM_OBJECTIVES_COUNT)
        return empty;

    auto it = g_questCreationSources.find(questId);
    return it != g_questCreationSources.end() ? it->second[itemObjectiveIdx] : empty;
}

// Selects all questgivers for a specific level (range).
questGiverMap QuestGiversValue::Calculate()
{
    uint32 level = 0;
    std::string const q = getQualifier();
    bool hasQualifier = !q.empty();

    if (hasQualifier)
        level = stoi(q);

    questGuidpMap questMap = GAI_VALUE(questGuidpMap, "quest guidp map");

    questGiverMap guidps;

    for (auto& qPair : questMap)
    {
        for (auto& entry : qPair.second[(int)QuestRelationFlag::questGiver])
        {
            for (auto& guidp : entry.second)
            {
                uint32 questId = qPair.first;

                if (hasQualifier)
                {
                    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);

                    if (quest && (level < quest->GetMinLevel() ||
                                  (int)level > quest->GetQuestLevel() + QuestAction::GREY_QUEST_LEVEL_BAND))
                        continue;
                }

                guidps[questId].push_back(guidp);
            }
        }
    }

    return guidps;
}

std::vector<GuidPosition> ActiveQuestGiversValue::Calculate()
{
    questGiverMap qGivers = GAI_VALUE2(questGiverMap, "quest givers", bot->GetLevel());

    std::vector<GuidPosition> retQuestGivers;

    for (auto& qGiver : qGivers)
    {
        uint32 questId = qGiver.first;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
        {
            continue;
        }

        if (!bot->CanTakeQuest(quest, false))
            continue;

        QuestStatus status = bot->GetQuestStatus(questId);

        if (status != QUEST_STATUS_NONE)
            continue;

        for (auto& guidp : qGiver.second)
        {
            CreatureTemplate const* creatureTemplate = guidp.GetCreatureTemplate();

            if (creatureTemplate)
            {
                if (bot->GetFactionReactionTo(bot->GetFactionTemplateEntry(),
                                              sFactionTemplateStore.LookupEntry(creatureTemplate->faction)) <
                    REP_FRIENDLY)
                    continue;
            }

            if (!guidp.IsCreatureOrGOAccessible())
                continue;

            retQuestGivers.push_back(guidp);
        }
    }

    return retQuestGivers;
}

std::vector<GuidPosition> ActiveQuestTakersValue::Calculate()
{
    questGuidpMap questMap = GAI_VALUE(questGuidpMap, "quest guidp map");

    std::vector<GuidPosition> retQuestTakers;

    QuestStatusMap& questStatusMap = bot->getQuestStatusMap();

    for (auto& questStatus : questStatusMap)
    {
        uint32 questId = questStatus.first;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);

        if (!quest)
        {
            continue;
        }

        QuestStatus status = questStatus.second.Status;
        if ((status != QUEST_STATUS_COMPLETE || bot->GetQuestRewardStatus(questId)) &&
            (!quest->IsAutoComplete() || !bot->CanTakeQuest(quest, false)))
            continue;

        auto q = questMap.find(questId);

        if (q == questMap.end())
            continue;

        auto qt = q->second.find((int)QuestRelationFlag::questTaker);

        if (qt == q->second.end())
            continue;

        for (auto& entry : qt->second)
        {
            if (entry.first > 0)
            {
                if (CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(entry.first))
                {
                    if (bot->GetFactionReactionTo(bot->GetFactionTemplateEntry(),
                                                  sFactionTemplateStore.LookupEntry(info->faction)) < REP_FRIENDLY)
                        continue;
                }
            }

            for (auto& guidp : entry.second)
            {
                if (!guidp.IsCreatureOrGOAccessible())
                    continue;

                retQuestTakers.push_back(guidp);
            }
        }
    }

    return retQuestTakers;
}

std::vector<GuidPosition> ActiveQuestObjectivesValue::Calculate()
{
    questGuidpMap questMap = GAI_VALUE(questGuidpMap, "quest guidp map");

    std::vector<GuidPosition> retQuestObjectives;

    QuestStatusMap& questStatusMap = bot->getQuestStatusMap();

    for (auto& questStatus : questStatusMap)
    {
        uint32 questId = questStatus.first;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
        {
            continue;
        }

        QuestStatusData statusData = questStatus.second;
        if (statusData.Status != QUEST_STATUS_INCOMPLETE)
            continue;

        for (uint32 objective = 0; objective < QUEST_OBJECTIVES_COUNT; objective++)
        {
            if (quest->RequiredItemCount[objective])
            {
                uint32 reqCount = quest->RequiredItemCount[objective];
                uint32 hasCount = statusData.ItemCount[objective];

                if (!reqCount || hasCount >= reqCount)
                    continue;
            }

            if (quest->RequiredNpcOrGoCount[objective])
            {
                uint32 reqCount = quest->RequiredNpcOrGoCount[objective];
                uint32 hasCount = statusData.CreatureOrGOCount[objective];

                if (!reqCount || hasCount >= reqCount)
                    continue;
            }

            auto q = questMap.find(questId);

            if (q == questMap.end())
                continue;

            auto qt = q->second.find((int)QuestRelationFlag(1 << objective));

            if (qt == q->second.end())
                continue;

            for (auto& entry : qt->second)
            {
                for (auto& guidp : entry.second)
                {
                    if (!guidp.IsCreatureOrGOAccessible())
                        continue;

                    retQuestObjectives.push_back(guidp);
                }
            }
        }
    }

    return retQuestObjectives;
}

uint8 FreeQuestLogSlotValue::Calculate()
{
    uint8 numQuest = 0;
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);

        if (!questId)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        numQuest++;
    }

    return MAX_QUEST_LOG_SIZE - numQuest;
}

uint32 DialogStatusValue::getDialogStatus(Player* bot, int32 questgiver, uint32 questId)
{
    uint32 dialogStatus = DIALOG_STATUS_NONE;

    QuestRelationBounds rbounds;   // QuestRelations (quest-giver)
    QuestRelationBounds irbounds;  // InvolvedRelations (quest-finisher)

    if (questgiver > 0)
    {
        rbounds = sObjectMgr->GetCreatureQuestRelationBounds(questgiver);
        irbounds = sObjectMgr->GetCreatureQuestInvolvedRelationBounds(questgiver);
    }
    else
    {
        rbounds = sObjectMgr->GetGOQuestRelationBounds(questgiver * -1);
        irbounds = sObjectMgr->GetGOQuestInvolvedRelationBounds(questgiver * -1);
    }

    // Check markings for quest-finisher
    for (QuestRelations::const_iterator itr = irbounds.first; itr != irbounds.second; ++itr)
    {
        if (questId && itr->second != questId)
            continue;

        Quest const* pQuest = sObjectMgr->GetQuestTemplate(itr->second);
        if (!pQuest)
        {
            continue;
        }

        uint32 dialogStatusNew = DIALOG_STATUS_NONE;

        QuestStatus status = bot->GetQuestStatus(itr->second);

        if ((status == QUEST_STATUS_COMPLETE && !bot->GetQuestRewardStatus(itr->second)) ||
            (pQuest->IsAutoComplete() && bot->CanTakeQuest(pQuest, false)))
        {
            if (pQuest->IsAutoComplete() && pQuest->IsRepeatable())
            {
                dialogStatusNew = DIALOG_STATUS_REWARD_REP;
            }
            else
            {
                dialogStatusNew = DIALOG_STATUS_REWARD2;
            }
        }
        else if (status == QUEST_STATUS_INCOMPLETE)
        {
            dialogStatusNew = DIALOG_STATUS_INCOMPLETE;
        }

        if (dialogStatusNew > dialogStatus)
        {
            dialogStatus = dialogStatusNew;
        }
    }

    // check markings for quest-giver
    for (QuestRelations::const_iterator itr = rbounds.first; itr != rbounds.second; ++itr)
    {
        if (questId && itr->second != questId)
            continue;

        Quest const* pQuest = sObjectMgr->GetQuestTemplate(itr->second);
        if (!pQuest)
        {
            continue;
        }

        uint32 dialogStatusNew = DIALOG_STATUS_NONE;

        QuestStatus status = bot->GetQuestStatus(itr->second);

        if (status == QUEST_STATUS_NONE)  // For all other cases the mark is handled either at some place else, or with
                                          // involved-relations already
        {
            if (bot->CanSeeStartQuest(pQuest))
            {
                if (bot->SatisfyQuestLevel(pQuest, false))
                {
                    int32 lowLevelDiff = sWorld->getIntConfig(CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF);
                    if (pQuest->IsAutoComplete() ||
                        (pQuest->IsRepeatable() &&
                         bot->getQuestStatusMap()[itr->second].Status == QUEST_STATUS_REWARDED))
                    {
                        dialogStatusNew = DIALOG_STATUS_REWARD_REP;
                    }
                    else if (lowLevelDiff < 0 || bot->GetLevel() <= bot->GetQuestLevel(pQuest) + uint32(lowLevelDiff))
                    {
                        dialogStatusNew = DIALOG_STATUS_AVAILABLE;
                    }
                    else
                    {
                        dialogStatusNew = DIALOG_STATUS_LOW_LEVEL_AVAILABLE;
                    }
                }
                else
                {
                    dialogStatusNew = DIALOG_STATUS_UNAVAILABLE;
                }
            }
        }

        if (dialogStatusNew > dialogStatus)
        {
            dialogStatus = dialogStatusNew;
        }
    }

    return dialogStatus;
}

uint32 DialogStatusValue::Calculate() { return getDialogStatus(bot, stoi(getQualifier())); }

uint32 DialogStatusQuestValue::Calculate()
{
    return getDialogStatus(bot, getMultiQualifier(getQualifier(), 0), getMultiQualifier(getQualifier(), 1));
}

bool CanAcceptQuestValue::Calculate()
{
    return AI_VALUE2(uint32, "dialog status", getQualifier()) == DIALOG_STATUS_AVAILABLE;
};

bool CanAcceptQuestLowLevelValue::Calculate()
{
    uint32 dialogStatus = AI_VALUE2(uint32, "dialog status", getQualifier());
    return dialogStatus == DIALOG_STATUS_LOW_LEVEL_AVAILABLE;
};

bool CanTurnInQuestValue::Calculate()
{
    uint32 dialogStatus = AI_VALUE2(uint32, "dialog status", getQualifier());
    return dialogStatus == DIALOG_STATUS_REWARD2 || dialogStatus == DIALOG_STATUS_REWARD ||
           dialogStatus == DIALOG_STATUS_REWARD_REP;
};
