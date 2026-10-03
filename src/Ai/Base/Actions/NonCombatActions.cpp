/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NonCombatActions.h"
#include "Event.h"
#include "Playerbots.h"

namespace
{
constexpr uint32 BG_WS_SPELL_WARSONG_FLAG = 23333;
constexpr uint32 BG_WS_SPELL_SILVERWING_FLAG = 23335;
constexpr uint32 BG_EY_NETHERSTORM_FLAG_SPELL = 34976;

bool IsDisallowedShapeshiftForm(Player* bot)
{
    if (bot->getClass() == CLASS_DRUID)
    {
        ShapeshiftForm form = bot->GetShapeshiftForm();
        return form == FORM_TRAVEL || form == FORM_AQUA ||
               form == FORM_FLIGHT || form == FORM_FLIGHT_EPIC ||
               form == FORM_BEAR || form == FORM_DIREBEAR ||
               form == FORM_CAT;
    }
    else if (bot->getClass() == CLASS_PRIEST)
    {
        return bot->GetShapeshiftForm() == FORM_SPIRITOFREDEMPTION;
    }

    return false;
}

// Overrides for the eat/drink usefulness gates: a non-zero value for the bot's type wins over the
// global threshold. The override is checked first so the bot-type lookups are skipped entirely
// when no per-type value is configured.
uint32 ResolveConsumeThreshold(uint32 randomBotValue, uint32 altBotValue, uint32 globalValue, Player* bot,
                               PlayerbotAI* botAI)
{
    if (randomBotValue && sRandomPlayerbotMgr.IsRandomBot(bot))
        return randomBotValue;

    if (altBotValue && botAI->IsAltBot())
        return altBotValue;

    return globalValue;
}
}

bool DrinkAction::Execute(Event event)
{
    if (botAI->HasCheat(BotCheatMask::food))
    {
        // if (bot->IsNonMeleeSpellCast(true))
        //     return false;

        bot->ClearUnitState(UNIT_STATE_CHASE);
        bot->ClearUnitState(UNIT_STATE_FOLLOW);

        if (bot->isMoving())
        {
            bot->StopMoving();
            // botAI->SetNextCheckDelay(sPlayerbotAIConfig->globalCoolDown);
            // return false;
        }
        bot->SetStandState(UNIT_STAND_STATE_SIT);
        bot->CastStop();

        // float hp = bot->GetHealthPercent();
        float mp = bot->GetPowerPct(POWER_MANA);
        float p = mp;
        float delay;

        if (!bot->InBattleground())
            delay = 18.0f * IN_MILLISECONDS * (100 - p) / 100.0f;
        else
            delay = 12.0f * IN_MILLISECONDS * (100 - p) / 100.0f;

        botAI->SetNextCheckDelay(delay);

        bot->AddAura(25990, bot);
        return true;
        // return botAI->CastSpell(24707, bot);
    }

    return UseItemAction::Execute(event);
}

bool DrinkAction::isUseful()
{
    return UseItemAction::isUseful() && AI_VALUE2(bool, "has mana", "self target") &&
           AI_VALUE2(uint8, "mana", "self target") <
               ResolveConsumeThreshold(sPlayerbotAIConfig.randomBotDrinkManaThreshold,
                                       sPlayerbotAIConfig.altBotDrinkManaThreshold, sPlayerbotAIConfig.highMana, bot,
                                       botAI);
}

bool DrinkAction::isPossible()
{
    if (bot->IsInCombat() || bot->IsMounted() || IsDisallowedShapeshiftForm(bot))
        return false;

    if (bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) || bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG) ||
        bot->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL))
    {
        return false;
    }

    return botAI->HasCheat(BotCheatMask::food) || UseItemAction::isPossible();
}

bool EatAction::Execute(Event event)
{
    if (botAI->HasCheat(BotCheatMask::food))
    {
        // if (bot->IsNonMeleeSpellCast(true))
        //     return false;

        bot->ClearUnitState(UNIT_STATE_CHASE);
        bot->ClearUnitState(UNIT_STATE_FOLLOW);

        if (bot->isMoving())
        {
            bot->StopMoving();
            // botAI->SetNextCheckDelay(sPlayerbotAIConfig.globalCoolDown);
            // return false;
        }

        bot->SetStandState(UNIT_STAND_STATE_SIT);
        bot->CastStop();

        float hp = bot->GetHealthPct();
        // float mp = bot->HasMana() ? bot->GetPowerPercent() : 0.f;
        float p = hp;
        float delay;

        if (!bot->InBattleground())
            delay = 18.0f * IN_MILLISECONDS * (100 - p) / 100.0f;
        else
            delay = 12.0f * IN_MILLISECONDS * (100 - p) / 100.0f;

        botAI->SetNextCheckDelay(delay);

        bot->AddAura(25990, bot);
        return true;
    }

    return UseItemAction::Execute(event);
}

bool EatAction::isUseful()
{
    return UseItemAction::isUseful() &&
           AI_VALUE2(uint8, "health", "self target") <
               ResolveConsumeThreshold(sPlayerbotAIConfig.randomBotEatHealthThreshold,
                                       sPlayerbotAIConfig.altBotEatHealthThreshold, sPlayerbotAIConfig.mediumHealth,
                                       bot, botAI);
}

bool EatAction::isPossible()
{
    if (bot->IsInCombat() || bot->IsMounted() || IsDisallowedShapeshiftForm(bot))
        return false;

    if (bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) || bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG) ||
        bot->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL))
    {
        return false;
    }

    return botAI->HasCheat(BotCheatMask::food) || UseItemAction::isPossible();
}
