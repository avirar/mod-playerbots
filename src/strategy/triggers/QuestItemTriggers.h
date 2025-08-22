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

// Trigger that activates when the bot has a quest item with a spell that can be used on a valid target
class QuestItemUsableTrigger : public Trigger
{
public:
    QuestItemUsableTrigger(PlayerbotAI* botAI) : Trigger(botAI, "quest item usable") {}

    bool IsActive() override;

private:
    // Helper methods for quest item validation
    bool HasQuestItemWithSpell(Item** outItem, uint32* outSpellId) const;
    bool HasValidTargetForQuestItem(uint32 spellId) const;
    bool IsTargetValidForSpell(Unit* target, uint32 spellId) const;
    bool CheckSpellConditions(uint32 spellId, Unit* target) const;
};

// Trigger that activates when the bot is too far from a valid quest item target
class FarFromQuestItemTargetTrigger : public Trigger
{
public:
    FarFromQuestItemTargetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "far from quest item target") {}

    bool IsActive() override;

private:
    Unit* FindBestQuestItemTarget() const;
    bool HasQuestItemWithSpell(Item** outItem, uint32* outSpellId) const;
    bool IsTargetValidForSpell(Unit* target, uint32 spellId) const;
    bool CheckSpellConditions(uint32 spellId, Unit* target) const;
};

// Trigger that activates when there are valid quest item targets available nearby
class QuestItemTargetAvailableTrigger : public Trigger
{
public:
    QuestItemTargetAvailableTrigger(PlayerbotAI* botAI) : Trigger(botAI, "quest item target available") {}

    bool IsActive() override;

private:
    bool HasQuestItemWithSpell() const;
    bool HasValidTargetsNearby() const;
};

#endif