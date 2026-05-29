/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#pragma once

#include "Define.h"
#include <vector>

class Player;
class PlayerbotAI;
class Unit;
class WorldObject;
struct Quest;

/**
 * @brief Utility class for quest spell operations (spells cast on targets for quest credit)
 *
 * This class centralizes functionality for quests that require casting spells on creatures,
 * such as "Gift of the Naaru" on Draenei Survivors or similar quest mechanics.
 */
class QuestSpellHelper
{
public:
    /**
     * @brief Find active quests that require casting spells on creatures
     * @param player Player to check active quests for
     * @return Vector of quests requiring spell casts on targets
     */
    static std::vector<Quest const*> FindQuestsRequiringSpellCast(Player* player);

    /**
     * @brief Find the best target creature for a quest spell within range
     * @param botAI Bot AI instance for accessing nearby creatures
     * @param quest Quest to find targets for
     * @param outSpellId Output parameter for the spell ID to cast
     * @return Best target unit or nullptr if none found
     */
    static Unit* FindTargetForQuestSpell(PlayerbotAI* botAI, Quest const* quest, uint32* outSpellId = nullptr);

    /**
     * @brief Get the appropriate spell to cast for a quest objective
     * @param player Player to check spells for
     * @param quest Quest to determine spell for
     * @param creatureEntry Target creature entry (for quest validation)
     * @return Spell ID to cast, or 0 if none found
     */
    static uint32 GetSpellForQuestObjective(Player* player, Quest const* quest, uint32 creatureEntry);

    /**
     * @brief Check if a target can be used for quest spell (cooldown check)
     * @param botAI Bot AI instance for accessing context
     * @param target Target to check usage for
     * @param spellId Spell ID to check cooldown for
     * @return true if target can be used, false if recently used
     */
    static bool CanUseQuestSpellOnTarget(PlayerbotAI* botAI, WorldObject* target, uint32 spellId);

    /**
     * @brief Record that quest spell was used on target (start cooldown)
     * @param botAI Bot AI instance for debug output
     * @param target Target that was used
     * @param spellId Spell ID that was used
     */
    static void RecordQuestSpellUsage(PlayerbotAI* botAI, WorldObject* target, uint32 spellId);

private:
    /**
     * @brief Check if quest objective requires spell cast on creature
     * @param quest Quest to check
     * @return true if quest has spell-cast objectives
     */
    static bool QuestRequiresSpellCast(Quest const* quest);

    /**
     * @brief Find racial spell matching quest requirements
     * @param player Player to check racial spells for
     * @param questId Quest ID for spell determination
     * @return Spell ID of racial spell, or 0 if none found
     */
    static uint32 FindRacialSpellForQuest(Player* player, uint32 questId);
};
