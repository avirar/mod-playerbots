/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "PaladinTriggers.h"

#include "PaladinActions.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

bool SealTrigger::IsActive()
{
    Unit* target = GetTarget();
    return !botAI->HasAura("seal of justice", target) && !botAI->HasAura("seal of command", target) &&
           !botAI->HasAura("seal of vengeance", target) && !botAI->HasAura("seal of corruption", target) &&
           !botAI->HasAura("seal of righteousness", target) && !botAI->HasAura("seal of light", target) &&
           (!botAI->HasAura("seal of wisdom", target) || AI_VALUE2(uint8, "mana", "self target") > 70);
}

bool CrusaderAuraTrigger::IsActive()
{
    Unit* target = GetTarget();
    return AI_VALUE2(bool, "mounted", "self target") && !botAI->HasAura("crusader aura", target);
}

bool BlessingTrigger::IsActive()
{
    Unit* target = GetTarget();
    return SpellTrigger::IsActive() && !botAI->HasAnyAuraOf(target, "blessing of might", "blessing of wisdom",
                                                            "blessing of kings", "blessing of sanctuary", nullptr);
}

bool GreaterBlessingOfMightNeededTrigger::IsActive()
{
    // Check if any mage, priest, or warlock lacks Greater Blessing of Might
    std::vector<Unit*> targets = AI_VALUE2(std::vector<Unit*>, "raid members", "warrior, rogue, death knight, hunter"); // Adjust classes as needed
    for (Unit* target : targets)
    {
        if (!botAI->HasAura("greater blessing of might", target))
            return true;
    }
    return false;
}

bool GreaterBlessingOfWisdomNeededTrigger::IsActive()
{
    // Check if any warrior, rogue, death knight, or hunter lacks Greater Blessing of Wisdom
    std::vector<Unit*> targets = AI_VALUE2(std::vector<Unit*>, "raid members", "mage, priest, warlock"); // Adjust classes as needed
    for (Unit* target : targets)
    {
        if (!botAI->HasAura("greater blessing of wisdom", target))
            return true;
    }
    return false;
}

bool GreaterBlessingOfKingsNeededTrigger::IsActive()
{
    // Check if any class lacks Greater Blessing of Kings
    std::vector<Unit*> targets = AI_VALUE2(std::vector<Unit*>, "raid members", "all classes"); // Typically applies to all classes
    for (Unit* target : targets)
    {
        if (!botAI->HasAura("greater blessing of kings", target))
            return true;
    }
    return false;
}

bool GreaterBlessingOfSanctuaryNeededTrigger::IsActive()
{
    // Check if any tank lacks Greater Blessing of Sanctuary
    std::vector<Unit*> targets = AI_VALUE2(std::vector<Unit*>, "raid members", "tank"); // Adjust role as needed
    for (Unit* target : targets)
    {
        if (!botAI->HasAura("greater blessing of sanctuary", target))
            return true;
    }
    return false;
}
