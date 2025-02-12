/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "PriestActions.h"

#include "Event.h"
#include "Playerbots.h"

bool CastRemoveShadowformAction::isUseful() { return botAI->HasAura("shadowform", AI_VALUE(Unit*, "self target")); }

bool CastRemoveShadowformAction::isPossible() { return true; }

bool CastRemoveShadowformAction::Execute(Event event)
{
    botAI->RemoveAura("shadowform");
    return true;
}

Unit* CastPowerWordShieldOnAlmostFullHealthBelowAction::GetTarget()
{
    Group* group = bot->GetGroup();
    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* player = gref->GetSource();
        if (!player)
            continue;
        if (player->isDead())
        {
            continue;
        }
        if (player->GetHealthPct() > sPlayerbotAIConfig->almostFullHealth)
        {
            continue;
        }
        if (player->GetDistance2d(bot) > sPlayerbotAIConfig->spellDistance)
        {
            continue;
        }
        if (botAI->HasAnyAuraOf(player, "weakened soul", "power word: shield", nullptr))
        {
            continue;
        }
        return player;
    }
    return nullptr;
}

bool CastPowerWordShieldOnAlmostFullHealthBelowAction::isUseful()
{
    Group* group = bot->GetGroup();
    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* player = gref->GetSource();
        if (!player)
            continue;
        if (player->isDead())
        {
            continue;
        }
        if (player->GetHealthPct() > sPlayerbotAIConfig->almostFullHealth)
        {
            continue;
        }
        if (player->GetDistance2d(bot) > sPlayerbotAIConfig->spellDistance)
        {
            continue;
        }
        if (botAI->HasAnyAuraOf(player, "weakened soul", "power word: shield", nullptr))
        {
            continue;
        }
        return true;
    }
    return false;
}

Unit* CastPowerWordShieldOnNotFullAction::GetTarget()
{
    Group* group = bot->GetGroup();
    MinValueCalculator calc(100);
    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* player = gref->GetSource();
        if (!player)
            continue;
        if (player->isDead() || player->IsFullHealth())
        {
            continue;
        }
        if (player->GetDistance2d(bot) > sPlayerbotAIConfig->spellDistance)
        {
            continue;
        }
        if (botAI->HasAnyAuraOf(player, "weakened soul", "power word: shield", nullptr))
        {
            continue;
        }
        calc.probe(player->GetHealthPct(), player);
    }
    return (Unit*)calc.param;
}

bool CastPowerWordShieldOnNotFullAction::isUseful()
{
    return GetTarget();
}

bool CastPowerWordFortitudeOnPartyAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    // If in a group, try Prayer first
    Group* group = botAI->GetBot()->GetGroup();
    if (group && target->GetLevel() >= 38 && bot->HasSpell(21562)/* && botAI->CanCastSpell("prayer of fortitude", target) */)
    {
        return botAI->CastSpell("prayer of fortitude", target);
    }

    // Otherwise do normal single-target
    return botAI->CastSpell("power word: fortitude", target);
}

Unit* CastPowerWordFortitudeOnPartyAction::GetTarget()
{
    Group* group = bot->GetGroup();
    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* player = gref->GetSource();
        if (!player)
            continue;
        if (player->isDead())
        {
            continue;
        }
        if (player->GetDistance2d(bot) > sPlayerbotAIConfig->spellDistance)
        {
            continue;
        }
        if (botAI->HasAnyAuraOf(player, "power word: fortitude", "prayer of fortitude", nullptr))
        {
            continue;
        }
        return player->ToUnit();
    }
    return nullptr;
}

bool CastPowerWordFortitudeOnPartyAction::isUseful()
{
    Group* group = bot->GetGroup();

    if (!group)
        return false;

    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* player = gref->GetSource();
        if (!player)
            continue;
        if (player->isDead())
        {
            continue;
        }
        if (player->GetDistance2d(bot) > sPlayerbotAIConfig->spellDistance)
        {
            continue;
        }
        if (botAI->HasAnyAuraOf(player, "power word: fortitude", "prayer of fortitude", nullptr))
        {
            continue;
        }
        return true;
    }
    return false;
}

bool CastDivineSpiritOnPartyAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    // If in a group, try Prayer first
    Group* group = botAI->GetBot()->GetGroup();
    if (group && target->GetLevel() >= 50 && bot->HasSpell(27681)/* && botAI->CanCastSpell("prayer of spirit", target) */)
    {
        return botAI->CastSpell("prayer of spirit", target);
    }

    // Otherwise do normal single-target
    return botAI->CastSpell("divine spirit", target);
}

Unit* CastDivineSpiritOnPartyAction::GetTarget()
{
    Group* group = bot->GetGroup();
    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* player = gref->GetSource();
        if (!player)
            continue;
        if (player->isDead())
        {
            continue;
        }
        if (player->GetDistance2d(bot) > sPlayerbotAIConfig->spellDistance)
        {
            continue;
        }
        if (botAI->HasAnyAuraOf(player, "divine spirit", "prayer of spirit", nullptr))
        {
            continue;
        }
        return player->ToUnit();
    }
    return nullptr;
}

bool CastDivineSpiritOnPartyAction::isUseful()
{
    Group* group = bot->GetGroup();

    if (!group)
        return false;

    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* player = gref->GetSource();
        if (!player)
            continue;
        if (player->isDead())
        {
            continue;
        }
        if (player->GetDistance2d(bot) > sPlayerbotAIConfig->spellDistance)
        {
            continue;
        }
        if (botAI->HasAnyAuraOf(player, "divine spirit", "prayer of spirit", nullptr))
        {
            continue;
        }
        return true;
    }
    return false;
}
