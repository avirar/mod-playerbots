/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_POSSIBLERPGTARGETSVALUE_H
#define _PLAYERBOT_POSSIBLERPGTARGETSVALUE_H

#include "NearestGameObjects.h"
#include "NearestUnitsValue.h"
#include "PlayerbotAIConfig.h"
#include "SharedDefines.h"

class PlayerbotAI;
class Player;
class Creature;
struct TrainerSpell;

/**
 * @brief Trainer classification helper for RPG target filtering
 * 
 * Uses AzerothCore's cached DBC stores to determine if a trainer teaches
 * primary profession skills (SKILL_CATEGORY_PROFESSION) which should be
 * excluded from "new RPG" strategy interactions.
 */
class TrainerClassifier
{
public:
    /**
     * @brief Determine if a trainer is valid for secondary skill learning
     * 
     * @param bot The player bot
     * @param trainer The trainer creature to evaluate
     * @return true if trainer teaches secondary skills and has learnable spells
     * @return false if trainer teaches primary professions or has no learnable spells
     */
    bool IsValidSecondaryTrainer(Player* bot, Creature* trainer);

private:
    /**
     * @brief Check if a trainer spell teaches primary profession skills
     * 
     * @param tSpell The trainer spell to check
     * @param botAI The PlayerbotAI for debug strategy checking
     * @return true if spell teaches SKILL_CATEGORY_PROFESSION skills
     */
    bool TeachesPrimaryProfession(TrainerSpell const* tSpell, PlayerbotAI* botAI);
    
    /**
     * @brief Get skill category using cached DBC store
     * 
     * @param skillId The skill ID to look up
     * @return Skill category ID, or 0 if not found
     */
    uint32 GetSkillCategory(uint32 skillId);
};

/**
 * @brief Standard RPG NPC types that bots should interact with
 * 
 * This enum defines the common NPC types that are considered valid
 * RPG targets for bot interaction, eliminating code duplication across
 * multiple RPG target value classes.
 */
enum class RpgNpcType : uint32
{
    INNKEEPER = UNIT_NPC_FLAG_INNKEEPER,
    GOSSIP = UNIT_NPC_FLAG_GOSSIP,
    QUESTGIVER = UNIT_NPC_FLAG_QUESTGIVER,
    FLIGHTMASTER = UNIT_NPC_FLAG_FLIGHTMASTER,
    BANKER = UNIT_NPC_FLAG_BANKER,
    GUILD_BANKER = UNIT_NPC_FLAG_GUILD_BANKER,
    TRAINER_CLASS = UNIT_NPC_FLAG_TRAINER_CLASS,
    TRAINER_PROFESSION = UNIT_NPC_FLAG_TRAINER_PROFESSION,
    VENDOR_AMMO = UNIT_NPC_FLAG_VENDOR_AMMO,
    VENDOR_FOOD = UNIT_NPC_FLAG_VENDOR_FOOD,
    VENDOR_POISON = UNIT_NPC_FLAG_VENDOR_POISON,
    VENDOR_REAGENT = UNIT_NPC_FLAG_VENDOR_REAGENT,
    AUCTIONEER = UNIT_NPC_FLAG_AUCTIONEER,
    STABLEMASTER = UNIT_NPC_FLAG_STABLEMASTER,
    PETITIONER = UNIT_NPC_FLAG_PETITIONER,
    TABARDDESIGNER = UNIT_NPC_FLAG_TABARDDESIGNER,
    BATTLEMASTER = UNIT_NPC_FLAG_BATTLEMASTER,
    TRAINER = UNIT_NPC_FLAG_TRAINER,
    VENDOR = UNIT_NPC_FLAG_VENDOR,
    REPAIR = UNIT_NPC_FLAG_REPAIR
};

/**
 * @brief Helper class for managing RPG NPC flags
 * 
 * Provides static methods to get the standard set of NPC flags that
 * should be considered valid RPG targets, eliminating code duplication.
 */
class RpgNpcFlags
{
public:
    /**
     * @brief Get the standard set of RPG NPC flags
     * @return Vector of NPC flags that are considered valid RPG targets
     */
    static const std::vector<uint32>& GetStandardRpgFlags();

private:
    static std::vector<uint32> standardFlags;
    static void InitializeFlags();
};

class PossibleRpgTargetsValue : public NearestUnitsValue
{
public:
    PossibleRpgTargetsValue(PlayerbotAI* botAI, float range = 70.0f);

protected:
    void FindUnits(std::list<Unit*>& targets) override;
    bool AcceptUnit(Unit* unit) override;
};

class PossibleNewRpgTargetsValue : public NearestUnitsValue
{
public:
    PossibleNewRpgTargetsValue(PlayerbotAI* botAI, float range = 150.0f);
    GuidVector Calculate() override;
protected:
    void FindUnits(std::list<Unit*>& targets) override;
    bool AcceptUnit(Unit* unit) override;
};

class PossibleNewRpgTargetsNoLosValue : public NearestUnitsValue
{
public:
    PossibleNewRpgTargetsNoLosValue(PlayerbotAI* botAI, float range = 200.0f);
    GuidVector Calculate() override;
protected:
    void FindUnits(std::list<Unit*>& targets) override;
    bool AcceptUnit(Unit* unit) override;
};

class PossibleNewRpgGameObjectsValue : public ObjectGuidListCalculatedValue
{
public:
    PossibleNewRpgGameObjectsValue(PlayerbotAI* botAI, float range = 150.0f, bool ignoreLos = true)
        : ObjectGuidListCalculatedValue(botAI, "possible new rpg game objects"), range(range), ignoreLos(ignoreLos)
    {
        if (allowedGOFlags.empty())
        {
            allowedGOFlags.push_back(GAMEOBJECT_TYPE_QUESTGIVER);
            allowedGOFlags.push_back(GAMEOBJECT_TYPE_GOOBER);
        }
    }

    static std::vector<GameobjectTypes> allowedGOFlags;
    GuidVector Calculate() override;

private:
    float range;
    bool ignoreLos;
};

#endif
