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
     * @return true if target meets all spell conditions
     */
    static bool IsTargetValidForSpell(Unit* target, uint32 spellId);

    /**
     * @brief Check spell-specific conditions from the conditions table
     * @param spellId Spell ID to check conditions for
     * @param target Target to validate conditions against
     * @return true if all conditions are met
     */
    static bool CheckSpellConditions(uint32 spellId, Unit* target);
};