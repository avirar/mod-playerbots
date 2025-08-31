/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "MoveToQuestItemTargetAction.h"

#include "Event.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "QuestItemHelper.h"
#include "SpellInfo.h"
#include "Unit.h"
#include "WorldObject.h"

// Use the same range as grinding distance for quest target search

bool MoveToQuestItemTargetAction::Execute(Event event)
{
    // Check if we need to move to a spell focus object first
    ObjectGuid spellFocusGuid = AI_VALUE(ObjectGuid, "spell focus target");
    if (!spellFocusGuid.IsEmpty())
    {
        GameObject* spellFocus = botAI->GetGameObject(spellFocusGuid);
        if (spellFocus && spellFocus->isSpawned())
        {
            // Calculate the required range based on spell focus distance
            float dist = (float)((spellFocus->GetGOInfo()->spellFocus.dist) / 2);
            float requiredRange = dist - 2.0f; // Add -2.0f buffer for reliable casting
            if (requiredRange <= 0.0f)
                requiredRange = 0.5f; // Minimum safe distance
                
            float distance = bot->GetDistance(spellFocus);
            if (distance > requiredRange)
            {
                // Move to the spell focus object
                return MoveTo(spellFocus->GetMapId(), spellFocus->GetPosition().GetPositionX(),
                             spellFocus->GetPosition().GetPositionY(), spellFocus->GetPosition().GetPositionZ());
            }
            else
            {
                // We're close enough to the spell focus, clear the target
                context->GetValue<ObjectGuid>("spell focus target")->Set(ObjectGuid::Empty);
            }
        }
        else
        {
            // Spell focus object no longer exists, clear the target
            context->GetValue<ObjectGuid>("spell focus target")->Set(ObjectGuid::Empty);
        }
    }
    
    uint32 spellId = 0;
    
    // Find the best quest item that needs a target
    Item* questItem = QuestItemHelper::FindBestQuestItem(bot, &spellId);
    if (!questItem)
    {
        return false;
    }
    

    // Find the best target for this quest item
    WorldObject* target = QuestItemHelper::FindBestTargetForQuestItem(botAI, spellId, questItem);
    if (!target)
    {
        return false;
    }

    // Check if we're already in range (use the spell's actual range minus buffer)
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    float range = spellInfo ? (spellInfo->GetMaxRange() - 2.0f) : (INTERACTION_DISTANCE - 2.0f); // -2.0f buffer for reliable casting
    
    // Ensure minimum distance is INTERACTION_DISTANCE minus buffer for quest item interactions
    if (range <= 0.0f || range < (INTERACTION_DISTANCE - 2.0f))
        range = INTERACTION_DISTANCE - 2.0f; // -2.0f buffer for reliable interaction
        
    // Cache distance calculation to avoid redundant calls
    float distance = bot->GetDistance(target);
    if (distance <= range)
    {
        // We're already in range, no need to move
        return false;
    }
    

    // Use the MovementAction's move functionality
    return MoveTo(target->GetMapId(), target->GetPosition().GetPositionX(), 
                  target->GetPosition().GetPositionY(), target->GetPosition().GetPositionZ());
}

bool MoveToQuestItemTargetAction::isUseful()
{
    // Check if we need to move to a spell focus object
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
                
            if (bot->GetDistance(spellFocus) > requiredRange)
            {
                return true; // We need to move to spell focus
            }
        }
    }
    
    uint32 spellId = 0;
    
    // Check if we have any usable quest items
    Item* questItem = QuestItemHelper::FindBestQuestItem(bot, &spellId);
    if (!questItem)
        return false;

    // Find the best target for this quest item
    WorldObject* target = QuestItemHelper::FindBestTargetForQuestItem(botAI, spellId, questItem);
    if (!target)
        return false;

    // Check if we need to move (are we out of range? use spell's actual range minus buffer)
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    float range = spellInfo ? (spellInfo->GetMaxRange() - 2.0f) : (INTERACTION_DISTANCE - 2.0f); // -2.0f buffer for reliable casting
    
    // Ensure minimum distance is INTERACTION_DISTANCE minus buffer for quest item interactions
    if (range <= 0.0f || range < (INTERACTION_DISTANCE - 2.0f))
        range = INTERACTION_DISTANCE - 2.0f; // -2.0f buffer for reliable interaction
        
    return bot->GetDistance(target) > range;
}

