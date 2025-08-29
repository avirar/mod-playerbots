/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "QuestItemTriggers.h"

#include "Item.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "QuestItemHelper.h"
#include "SpellInfo.h"
#include "Unit.h"

// Use the same range as grinding distance for quest item target detection

bool QuestItemUsableTrigger::IsActive()
{
    Item* questItem = nullptr;
    uint32 spellId = 0;
    
    // Check if we have a quest item with a spell
    questItem = QuestItemHelper::FindBestQuestItem(bot, &spellId);
    if (!questItem)
    {
        return false;
    }

    // Check if there are valid targets for this quest item
    Unit* target = QuestItemHelper::FindBestTargetForQuestItem(botAI, spellId);
    bool hasValidTarget = (target != nullptr);
    
    
    return hasValidTarget;
}


bool FarFromQuestItemTargetTrigger::IsActive()
{
    // Check if we need to move to a spell focus object first
    ObjectGuid spellFocusGuid = AI_VALUE(ObjectGuid, "spell focus target");
    if (!spellFocusGuid.IsEmpty())
    {
        GameObject* spellFocus = botAI->GetGameObject(spellFocusGuid);
        if (spellFocus && spellFocus->isSpawned())
        {
            float dist = (float)((spellFocus->GetGOInfo()->spellFocus.dist) / 2);
            float requiredRange = dist - 2.0f; // Add -2.0f buffer
            if (requiredRange <= 0.0f)
                requiredRange = 0.5f;
                
            return bot->GetDistance(spellFocus) > requiredRange;
        }
    }
    
    Item* questItem = nullptr;
    uint32 spellId = 0;
    
    // Check if we have a quest item with a spell
    questItem = QuestItemHelper::FindBestQuestItem(bot, &spellId);
    if (!questItem)
        return false;

    // Find the best available target
    Unit* target = FindBestQuestItemTarget();
    if (!target)
        return false;

    // Check if we're too far from the target (use spell's actual range minus buffer)
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    float range = spellInfo ? (spellInfo->GetMaxRange() - 2.0f) : (INTERACTION_DISTANCE - 2.0f); // -2.0f buffer for reliable casting
    
    // Ensure minimum distance is INTERACTION_DISTANCE minus buffer for quest item interactions
    if (range <= 0.0f || range < (INTERACTION_DISTANCE - 2.0f))
        range = INTERACTION_DISTANCE - 2.0f; // -2.0f buffer for reliable interaction
        
    return bot->GetDistance(target) > range;
}

Unit* FarFromQuestItemTargetTrigger::FindBestQuestItemTarget() const
{
    uint32 spellId = 0;
    Item* questItem = QuestItemHelper::FindBestQuestItem(bot, &spellId);
    if (!questItem)
        return nullptr;

    return QuestItemHelper::FindBestTargetForQuestItem(botAI, spellId);
}


bool QuestItemTargetAvailableTrigger::IsActive()
{
    // Check if we have quest items with spells
    uint32 spellId = 0;
    Item* questItem = QuestItemHelper::FindBestQuestItem(bot, &spellId);
    if (!questItem)
        return false;

    // Check if there are valid targets nearby
    Unit* target = QuestItemHelper::FindBestTargetForQuestItem(botAI, spellId);
    return (target != nullptr);
}


