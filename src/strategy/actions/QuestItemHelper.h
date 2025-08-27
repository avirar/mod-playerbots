/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#pragma once

#include "Define.h"

class Item;
class Player;
class PlayerbotAI;
class Unit;

/**
 * @brief Utility class for quest item operations to eliminate code duplication
 * 
 * This class centralizes common quest item functionality used across multiple
 * actions and triggers, including item validation, target finding, and spell
 * condition checking.
 */
class QuestItemHelper
{
public:
    /**
     * @brief Find the best quest item with an associated spell
     * @param bot Player to search inventory
     * @param outSpellId Optional output parameter for the spell ID
     * @return Valid quest item or nullptr if none found
     */
    static Item* FindBestQuestItem(Player* bot, uint32* outSpellId = nullptr);

    /**
     * @brief Check if an item is a valid quest item with player-castable spell
     * @param item Item to validate
     * @param outSpellId Optional output parameter for the spell ID
     * @return true if item is a valid quest item
     */
    static bool IsValidQuestItem(Item* item, uint32* outSpellId = nullptr);

    /**
     * @brief Find the best target for a quest item spell within grind distance
     * @param botAI Bot AI instance for target searching
     * @param spellId Spell ID to validate targets against
     * @return Best target unit or nullptr if none found
     */
    static Unit* FindBestTargetForQuestItem(PlayerbotAI* botAI, uint32 spellId);

    /**
     * @brief Check if a target is valid for a specific quest spell
     * @param target Target unit to validate
     * @param spellId Spell ID to check conditions for
     * @param caster Player casting the spell (for location/proximity checks)
     * @param botAI Bot AI instance for proximity checks (optional)
     * @return true if target meets all spell conditions
     */
    static bool IsTargetValidForSpell(Unit* target, uint32 spellId, Player* caster = nullptr, PlayerbotAI* botAI = nullptr);

    /**
     * @brief Check spell-specific conditions from the conditions table
     * @param spellId Spell ID to check conditions for
     * @param target Target to validate conditions against
     * @param caster Player casting the spell (for location/proximity checks)
     * @param botAI Bot AI instance for proximity checks (optional)
     * @return true if all conditions are met
     */
    static bool CheckSpellConditions(uint32 spellId, Unit* target, Player* caster = nullptr, PlayerbotAI* botAI = nullptr);

private:
    /**
     * @brief Check if player is near a specific creature type
     * @param botAI Bot AI instance for accessing nearby creatures
     * @param creatureEntry Creature entry ID to search for
     * @param maxDistance Maximum distance in yards
     * @param requireAlive Whether creature must be alive (true) or dead (false)
     * @return true if matching creature found within distance
     */
    static bool IsNearCreature(PlayerbotAI* botAI, uint32 creatureEntry, float maxDistance, bool requireAlive);

    /**
     * @brief Validate spell area/zone requirements from spell data
     * @param player Player to validate location for
     * @param spellId Spell ID to check requirements for
     * @return true if player is in required area/zone
     */
    static bool CheckSpellLocationRequirements(Player* player, uint32 spellId);

    /**
     * @brief Check if a quest spell can target dead units
     * @param spellId Spell ID to check
     * @return true if spell can target dead units (like corpse burning spells)
     */
    static bool CanQuestSpellTargetDead(uint32 spellId);

    /**
     * @brief Check if quest item should be used (prevent spam casting)
     * @param botAI Bot AI instance for debug output
     * @param player Player to check spell effects for
     * @param spellId Spell ID to check for existing effects
     * @return true if quest item can be used, false if should be prevented
     */
    static bool CanUseQuestItem(PlayerbotAI* botAI, Player* player, uint32 spellId);

    /**
     * @brief Check if quest item is needed for active quests
     * @param player Player to check active quests for
     * @param item Quest item to check
     * @param spellId Spell ID of the quest item
     * @return true if item is needed for active quests, false otherwise
     */
    static bool IsQuestItemNeeded(Player* player, Item* item, uint32 spellId);

    /**
     * @brief Check if using quest item on target would provide quest progress
     * @param player Player to check quest progress for
     * @param target Target to check quest credit for
     * @param spellId Spell ID of the quest item
     * @return true if target would provide quest progress, false otherwise
     */
    static bool WouldProvideQuestCredit(Player* player, Unit* target, uint32 spellId);
};
