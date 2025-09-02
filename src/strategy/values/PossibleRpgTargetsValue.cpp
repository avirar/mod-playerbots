/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "PossibleRpgTargetsValue.h"

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "ServerFacade.h"
#include "SharedDefines.h"
#include "NearestGameObjects.h"
#include "DBCStores.h"
#include "SpellMgr.h"
#include "CreatureData.h"

// Static member initialization
std::vector<uint32> RpgNpcFlags::standardFlags;

const std::vector<uint32>& RpgNpcFlags::GetStandardRpgFlags()
{
    if (standardFlags.empty())
        InitializeFlags();
    return standardFlags;
}

void RpgNpcFlags::InitializeFlags()
{
    standardFlags = {
        static_cast<uint32>(RpgNpcType::INNKEEPER),
        // Removed GOSSIP - prevents targeting guards and other flavor NPCs
        static_cast<uint32>(RpgNpcType::QUESTGIVER),
        static_cast<uint32>(RpgNpcType::FLIGHTMASTER),
        static_cast<uint32>(RpgNpcType::BANKER),
        static_cast<uint32>(RpgNpcType::GUILD_BANKER),
        static_cast<uint32>(RpgNpcType::TRAINER_CLASS),
        static_cast<uint32>(RpgNpcType::TRAINER_PROFESSION),
        static_cast<uint32>(RpgNpcType::VENDOR_AMMO),
        static_cast<uint32>(RpgNpcType::VENDOR_FOOD),
        static_cast<uint32>(RpgNpcType::VENDOR_POISON),
        static_cast<uint32>(RpgNpcType::VENDOR_REAGENT),
        static_cast<uint32>(RpgNpcType::AUCTIONEER),
        static_cast<uint32>(RpgNpcType::STABLEMASTER),
        static_cast<uint32>(RpgNpcType::PETITIONER),
        static_cast<uint32>(RpgNpcType::TABARDDESIGNER),
        static_cast<uint32>(RpgNpcType::BATTLEMASTER),
        static_cast<uint32>(RpgNpcType::TRAINER),
        static_cast<uint32>(RpgNpcType::VENDOR),
        static_cast<uint32>(RpgNpcType::REPAIR)
    };
}

// TrainerClassifier implementation
bool TrainerClassifier::IsValidSecondaryTrainer(Player* bot, Creature* trainer)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!trainer->IsValidTrainerForPlayer(bot)) {
        if (botAI && botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[TrainerClassifier] {} - Trainer {} not valid for player", 
                      bot->GetName(), trainer->GetName());
        }
        return false;
    }
    
    TrainerSpellData const* trainer_spells = trainer->GetTrainerSpells();
    if (!trainer_spells) {
        if (botAI && botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[TrainerClassifier] {} - Trainer {} has no spells", 
                      bot->GetName(), trainer->GetName());
        }
        return false;
    }
    
    bool hasLearnableSpells = false;
    uint32 greenSpellCount = 0;
    uint32 primaryProfessionSpellCount = 0;
    
    for (TrainerSpellMap::const_iterator itr = trainer_spells->spellList.begin();
         itr != trainer_spells->spellList.end(); ++itr) {
        
        TrainerSpell const* tSpell = &itr->second;
        if (!tSpell) continue;
        
        TrainerSpellState state = bot->GetTrainerSpellState(tSpell);
        if (state == TRAINER_SPELL_GREEN) {
            hasLearnableSpells = true;
            greenSpellCount++;
            
            // Check if this GREEN spell teaches primary profession skills
            if (TeachesPrimaryProfession(tSpell, botAI)) {
                primaryProfessionSpellCount++;
                if (botAI && botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[TrainerClassifier] {} - Trainer {} teaches primary profession spell {} (skill: {})", 
                              bot->GetName(), trainer->GetName(), tSpell->spell, tSpell->reqSkill);
                }
                return false; // Immediate exclusion
            }
        }
    }
    
    bool result = hasLearnableSpells;
    if (botAI && botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[TrainerClassifier] {} - Trainer {} validation result: {} (GREEN spells: {}, primary profession spells: {})", 
                  bot->GetName(), trainer->GetName(), result ? "VALID" : "INVALID", greenSpellCount, primaryProfessionSpellCount);
    }
    
    return result;
}

bool TrainerClassifier::TeachesPrimaryProfession(TrainerSpell const* tSpell, PlayerbotAI* botAI)
{
    // Check ReqSkillLine category
    if (tSpell->reqSkill > 0) {
        // Riding skill (762) should not be treated as a primary profession
        if (tSpell->reqSkill == 762) { // SKILL_RIDING
            if (botAI && botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[TrainerClassifier] Spell {} requires riding skill (762), NOT a primary profession", 
                          tSpell->spell);
            }
            return false;
        }
        
        uint32 category = GetSkillCategory(tSpell->reqSkill);
        if (category == SKILL_CATEGORY_PROFESSION) {
            if (botAI && botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[TrainerClassifier] Spell {} requires skill {} (category {}=PROFESSION)", 
                          tSpell->spell, tSpell->reqSkill, category);
            }
            return true;
        }
    }
    
    // Check spell effects for apprentice spells
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(tSpell->spell);
    if (spellInfo) {
        for (uint8 j = 0; j < 3; ++j) {
            if (spellInfo->Effects[j].Effect == SPELL_EFFECT_SKILL_STEP || 
                spellInfo->Effects[j].Effect == SPELL_EFFECT_SKILL) {
                
                uint32 skillId = spellInfo->Effects[j].MiscValue;
                
                // Riding skill (762) should not be treated as a primary profession
                if (skillId == 762) { // SKILL_RIDING
                    if (botAI && botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
                    {
                        LOG_DEBUG("playerbots", "[TrainerClassifier] Spell {} teaches riding skill (762) via effect, NOT a primary profession", 
                                  tSpell->spell);
                    }
                    return false;
                }
                
                uint32 category = GetSkillCategory(skillId);
                if (category == SKILL_CATEGORY_PROFESSION) {
                    if (botAI && botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
                    {
                        LOG_DEBUG("playerbots", "[TrainerClassifier] Spell {} teaches skill {} (category {}=PROFESSION) via effect", 
                                  tSpell->spell, skillId, category);
                    }
                    return true;
                }
            }
        }
    }
    
    return false;
}

uint32 TrainerClassifier::GetSkillCategory(uint32 skillId)
{
    // Use the cached DBC store - elegant and fast
    SkillLineEntry const* skillInfo = sSkillLineStore.LookupEntry(skillId);
    return skillInfo ? skillInfo->categoryId : 0;
}

PossibleRpgTargetsValue::PossibleRpgTargetsValue(PlayerbotAI* botAI, float range)
    : NearestUnitsValue(botAI, "possible rpg targets", range, true)
{
    // No initialization needed - using centralized RpgNpcFlags helper
}

void PossibleRpgTargetsValue::FindUnits(std::list<Unit*>& targets)
{
    Acore::AnyUnitInObjectRangeCheck u_check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);
}

bool PossibleRpgTargetsValue::AcceptUnit(Unit* unit)
{
    if (unit->IsHostileTo(bot) || unit->GetTypeId() == TYPEID_PLAYER)
        return false;

    if (sServerFacade->GetDistance2d(bot, unit) <= sPlayerbotAIConfig->tooCloseDistance)
        return false;

    if (unit->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITHEALER))
        return false;

    for (uint32 npcFlag : RpgNpcFlags::GetStandardRpgFlags())
    {
        if (unit->HasFlag(UNIT_NPC_FLAGS, npcFlag))
        {
            // Additional filtering for profession trainers
            if (npcFlag == UNIT_NPC_FLAG_TRAINER_PROFESSION && unit->GetTypeId() == TYPEID_UNIT)
            {
                Creature* trainer = unit->ToCreature();
                if (trainer && trainer->IsTrainer())
                {
                    static TrainerClassifier classifier;
                    if (!classifier.IsValidSecondaryTrainer(bot, trainer))
                    {
                        if (botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
                            LOG_DEBUG("playerbots", "[RPG Targets] {} - Rejecting primary profession trainer: {}", 
                                      bot->GetName(), trainer->GetName());
                        return false; // Reject this trainer entirely
                    }
                    else
                    {
                        if (botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
                            LOG_DEBUG("playerbots", "[RPG Targets] {} - Accepting secondary trainer: {}", 
                                      bot->GetName(), trainer->GetName());
                    }
                }
            }
            return true;
        }
    }

    TravelTarget* travelTarget = context->GetValue<TravelTarget*>("travel target")->Get();
    if (travelTarget->getDestination() && travelTarget->getDestination()->getEntry() == unit->GetEntry())
        return true;

    if (urand(1, 100) < 25 && unit->IsFriendlyTo(bot))
        return true;

    if (urand(1, 100) < 5)
        return true;

    return false;
}


PossibleNewRpgTargetsValue::PossibleNewRpgTargetsValue(PlayerbotAI* botAI, float range)
    : NearestUnitsValue(botAI, "possible new rpg targets", range, true)
{
    // No initialization needed - using centralized RpgNpcFlags helper
}

GuidVector PossibleNewRpgTargetsValue::Calculate()
{
    std::list<Unit*> targets;
    FindUnits(targets);

    GuidVector results;
    std::vector<std::pair<ObjectGuid, float>> guidDistancePairs;
    for (Unit* unit : targets)
    {
        if (AcceptUnit(unit) && (ignoreLos || bot->IsWithinLOSInMap(unit)))
            guidDistancePairs.push_back({unit->GetGUID(), bot->GetExactDist(unit)});
    }
    // Override to sort by distance
    std::sort(guidDistancePairs.begin(), guidDistancePairs.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });
    
    for (const auto& pair : guidDistancePairs) {
        results.push_back(pair.first);
    }
    return results;
}

void PossibleNewRpgTargetsValue::FindUnits(std::list<Unit*>& targets)
{
    Acore::AnyUnitInObjectRangeCheck u_check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);
}

bool PossibleNewRpgTargetsValue::AcceptUnit(Unit* unit)
{
    if (unit->IsHostileTo(bot) || unit->GetTypeId() == TYPEID_PLAYER)
        return false;

    if (unit->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITHEALER))
        return false;

    for (uint32 npcFlag : RpgNpcFlags::GetStandardRpgFlags())
    {
        if (unit->HasFlag(UNIT_NPC_FLAGS, npcFlag))
        {
            // Additional filtering for profession trainers
            if (npcFlag == UNIT_NPC_FLAG_TRAINER_PROFESSION && unit->GetTypeId() == TYPEID_UNIT)
            {
                Creature* trainer = unit->ToCreature();
                if (trainer && trainer->IsTrainer())
                {
                    static TrainerClassifier classifier;
                    if (!classifier.IsValidSecondaryTrainer(bot, trainer))
                    {
                        if (botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
                            LOG_DEBUG("playerbots", "[RPG Targets] {} - Rejecting primary profession trainer: {}", 
                                      bot->GetName(), trainer->GetName());
                        return false; // Reject this trainer entirely
                    }
                    else
                    {
                        if (botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
                            LOG_DEBUG("playerbots", "[RPG Targets] {} - Accepting secondary trainer: {}", 
                                      bot->GetName(), trainer->GetName());
                    }
                }
            }
            return true;
        }
    }

    // Check if this creature is a quest objective NPC that needs to be talked to
    if (unit->GetTypeId() == TYPEID_UNIT)
    {
        Creature* creature = unit->ToCreature();
        if (creature && !creature->IsHostileTo(bot))
        {
            uint32 creatureEntry = creature->GetEntry();
            
            // Check all active quests for this creature as a talk objective
            for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 questId = bot->GetQuestSlotQuestId(slot);
                if (!questId)
                    continue;
                    
                Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                if (!quest || bot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
                    continue;
                    
                // Check if this quest has SPEAKTO flag
                if (!quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_SPEAKTO))
                    continue;
                    
                // Check if this creature is a required objective
                for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                {
                    int32 requiredNpcOrGo = quest->RequiredNpcOrGo[i];
                    if (requiredNpcOrGo > 0 && requiredNpcOrGo == (int32)creatureEntry)
                    {
                        // Check if we still need this objective
                        const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
                        if (q_status.CreatureOrGOCount[i] < quest->RequiredNpcOrGoCount[i])
                        {
                            if (botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
                                LOG_DEBUG("playerbots", "[RPG Targets] {} - Accepting quest objective NPC {} for SPEAKTO quest {}", 
                                         bot->GetName(), creature->GetName(), questId);
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

PossibleNewRpgTargetsNoLosValue::PossibleNewRpgTargetsNoLosValue(PlayerbotAI* botAI, float range)
    : NearestUnitsValue(botAI, "possible new rpg targets no los", range, true)
{
    // No initialization needed - using centralized RpgNpcFlags helper
}

GuidVector PossibleNewRpgTargetsNoLosValue::Calculate()
{
    std::list<Unit*> targets;
    FindUnits(targets);

    GuidVector results;
    std::vector<std::pair<ObjectGuid, float>> guidDistancePairs;
    for (Unit* unit : targets)
    {
        // No LOS check - accept all units that pass AcceptUnit test
        if (AcceptUnit(unit))
        {
            float distance = bot->GetExactDist(unit);
            guidDistancePairs.push_back({unit->GetGUID(), distance});
        }
    }
    
    // Sort by 3D distance to prioritize same-elevation NPCs
    std::sort(guidDistancePairs.begin(), guidDistancePairs.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });
    
    for (const auto& pair : guidDistancePairs) {
        results.push_back(pair.first);
    }
    return results;
}

void PossibleNewRpgTargetsNoLosValue::FindUnits(std::list<Unit*>& targets)
{
    Acore::AnyUnitInObjectRangeCheck u_check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);
}

bool PossibleNewRpgTargetsNoLosValue::AcceptUnit(Unit* unit)
{
    if (unit->IsHostileTo(bot) || unit->GetTypeId() == TYPEID_PLAYER)
        return false;

    if (unit->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITHEALER))
        return false;

    for (uint32 npcFlag : RpgNpcFlags::GetStandardRpgFlags())
    {
        if (unit->HasFlag(UNIT_NPC_FLAGS, npcFlag))
        {
            // Additional filtering for profession trainers
            if (npcFlag == UNIT_NPC_FLAG_TRAINER_PROFESSION && unit->GetTypeId() == TYPEID_UNIT)
            {
                Creature* trainer = unit->ToCreature();
                if (trainer && trainer->IsTrainer())
                {
                    static TrainerClassifier classifier;
                    if (!classifier.IsValidSecondaryTrainer(bot, trainer))
                    {
                        if (botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
                            LOG_DEBUG("playerbots", "[RPG Targets] {} - Rejecting primary profession trainer: {}", 
                                      bot->GetName(), trainer->GetName());
                        return false; // Reject this trainer entirely
                    }
                    else
                    {
                        if (botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
                            LOG_DEBUG("playerbots", "[RPG Targets] {} - Accepting secondary trainer: {}", 
                                      bot->GetName(), trainer->GetName());
                    }
                }
            }
            return true;
        }
    }

    // Check if this creature is a quest objective NPC that needs to be talked to
    if (unit->GetTypeId() == TYPEID_UNIT)
    {
        Creature* creature = unit->ToCreature();
        if (creature && !creature->IsHostileTo(bot))
        {
            uint32 creatureEntry = creature->GetEntry();
            
            // Check all active quests for this creature as a talk objective
            for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 questId = bot->GetQuestSlotQuestId(slot);
                if (!questId)
                    continue;
                    
                Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                if (!quest || bot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
                    continue;
                    
                // Check if this quest has SPEAKTO flag
                if (!quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_SPEAKTO))
                    continue;
                    
                // Check if this creature is a required objective
                for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                {
                    int32 requiredNpcOrGo = quest->RequiredNpcOrGo[i];
                    if (requiredNpcOrGo > 0 && requiredNpcOrGo == (int32)creatureEntry)
                    {
                        // Check if we still need this objective
                        const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
                        if (q_status.CreatureOrGOCount[i] < quest->RequiredNpcOrGoCount[i])
                        {
                            if (botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
                                LOG_DEBUG("playerbots", "[RPG Targets] {} - Accepting quest objective NPC {} for SPEAKTO quest {} (no LOS)", 
                                         bot->GetName(), creature->GetName(), questId);
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

std::vector<GameobjectTypes> PossibleNewRpgGameObjectsValue::allowedGOFlags;

GuidVector PossibleNewRpgGameObjectsValue::Calculate()
{
    std::list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck u_check(bot, range);
    Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (botAI && botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
    {
        LOG_DEBUG("playerbots", "[Debug RPG GO] {} Found {} gameobjects in range {}", 
                  bot->GetName(), targets.size(), range);
    }

    
    std::vector<std::pair<ObjectGuid, float>> guidDistancePairs;
    for (GameObject* go : targets)
    {
        if (botAI && botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
        {
            LOG_DEBUG("playerbots", "[Debug RPG GO] {} Checking gameobject {} (entry: {}, type: {})", 
                      bot->GetName(), go->GetName(), go->GetEntry(), go->GetGoType());
        }
        
        bool flagCheck = false;
        for (uint32 goFlag : allowedGOFlags)
        {
            if (go->GetGoType() == goFlag)
            {
                flagCheck = true;
                break;
            }
        }
        if (!flagCheck)
        {
            if (botAI && botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[Debug RPG GO] {} Gameobject {} failed type check (type: {})", 
                          bot->GetName(), go->GetName(), go->GetGoType());
            }
            continue;
        }
        
        if (!ignoreLos && !bot->IsWithinLOSInMap(go))
            continue;
        
        // For GOOBER type gameobjects, check if they're required by an active quest
        if (go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
        {
            bool isQuestObjective = false;
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
                    // Handle negative gameobject IDs (quest->RequiredNpcOrGo[i] < 0 means gameobject)
                    int32 requiredEntry = quest->RequiredNpcOrGo[i];
                    if ((requiredEntry < 0 && -requiredEntry == goEntry) && quest->RequiredNpcOrGoCount[i] > 0)
                    {
                        // Check if this objective is not yet completed
                        QuestStatusData const& q_status = bot->getQuestStatusMap().at(questId);
                        if (q_status.CreatureOrGOCount[i] < quest->RequiredNpcOrGoCount[i])
                        {
                            isQuestObjective = true;
                            break;
                        }
                    }
                }
                if (isQuestObjective)
                    break;
            }
            
            if (!isQuestObjective)
            {
                if (botAI && botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
                {
                    LOG_DEBUG("playerbots", "[Debug RPG GO] {} Gameobject {} not a quest objective", 
                              bot->GetName(), go->GetName());
                }
                continue;
            }
            else if (botAI && botAI->HasStrategy("debug targets", BOT_STATE_NON_COMBAT))
            {
                LOG_DEBUG("playerbots", "[Debug RPG GO] {} Gameobject {} is a valid quest objective", 
                          bot->GetName(), go->GetName());
            }
        }
        
        guidDistancePairs.push_back({go->GetGUID(), bot->GetExactDist(go)});
    }
    GuidVector results;

    // Sort by distance
    std::sort(guidDistancePairs.begin(), guidDistancePairs.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });
    
    for (const auto& pair : guidDistancePairs) {
        results.push_back(pair.first);
    }
    return results;
}
