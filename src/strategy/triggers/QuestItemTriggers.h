/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_QUESTITEMTRIGGERS_H
#define _PLAYERBOT_QUESTITEMTRIGGERS_H

#include "Trigger.h"

class PlayerbotAI;
class Unit;
class Item;

/**
 * @brief Trigger that activates when quest items can be used on nearby valid targets
 * 
 * This trigger checks if the bot has quest items with spells and if there are
 * valid targets within range that meet the spell's conditions. It's designed to
 * work with UseQuestItemOnTargetAction.
 */
class QuestItemUsableTrigger : public Trigger
{
public:
    QuestItemUsableTrigger(PlayerbotAI* botAI) : Trigger(botAI, "quest item usable") {}

    bool IsActive() override;

private:
};

/**
 * @brief Trigger that activates when the bot is too far from quest item targets
 * 
 * This trigger works with MoveToQuestItemTargetAction to ensure the bot gets
 * within range of valid quest targets before attempting to use quest items.
 */
class FarFromQuestItemTargetTrigger : public Trigger
{
public:
    FarFromQuestItemTargetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "far from quest item target") {}

    bool IsActive() override;

private:
    Unit* FindBestQuestItemTarget() const;
};

/**
 * @brief Trigger that activates when valid quest item targets are available nearby
 * 
 * This is a general availability trigger that can be used to enable quest item
 * strategies when appropriate targets are detected in the area.
 */
class QuestItemTargetAvailableTrigger : public Trigger
{
public:
    QuestItemTargetAvailableTrigger(PlayerbotAI* botAI) : Trigger(botAI, "quest item target available") {}

    bool IsActive() override;

private:
};

#endif
