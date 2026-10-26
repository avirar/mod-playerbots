/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "LootTriggers.h"
#include <algorithm>

#include "LootObjectStack.h"
#include "Map.h"
#include "Playerbots.h"
#include "ServerFacade.h"

bool LootAvailableTrigger::IsActive()
{
    if (!AI_VALUE(bool, "has available loot"))
        return false;

    LootObject lootTarget = AI_VALUE(LootObject, "loot target");
    if (lootTarget.IsEmpty())
        return true;

    // A different nearest entry may be worth switching to; LootAction::isUseful applies the margin.
    if (AI_VALUE(LootObject, "nearest loot").guid != lootTarget.guid)
        return true;

    bool const inRange = botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT)
                             ? ServerFacade::instance().IsDistanceLessOrEqualThan(
                                   AI_VALUE2(float, "distance", "loot target"), CONTACT_DISTANCE)
                             : AI_VALUE(bool, "can loot");
    if (inRange || !AI_VALUE(LootObjectStack*, "available loot")->CanAttemptLoot(lootTarget.guid) ||
        !lootTarget.IsLootPossible(bot))
        return true;

    // No hostile targets to deal with first. Last because "all targets" is an uncached grid search.
    return AI_VALUE(GuidVector, "all targets").empty();
}

bool FarFromCurrentLootTrigger::IsActive()
{
    LootObject loot = AI_VALUE(LootObject, "loot target");
    if (loot.IsEmpty() || AI_VALUE(bool, "can loot") ||
        !AI_VALUE(LootObjectStack*, "available loot")->CanAttemptLoot(loot.guid))
        return false;

    bool const possible = loot.IsLootPossible(bot);
    if (!possible)
        LOG_DEBUG("playerbots", "swim-trace {}: far-trigger: target {} not loot-possible", bot->GetName(),
                  loot.guid.ToString());
    return possible;
}

bool CanLootTrigger::IsActive() { return AI_VALUE(bool, "can loot"); }

bool UnderwaterSafetyTrigger::IsActive()
{
    // Fast path for dry bots that have never tracked a dive (INT32_MAX = full/untracked).
    if (!bot->IsUnderWater() && !botAI->diveSubmergedSince && botAI->diveAirMs == INT32_MAX &&
        !botAI->diveSurfacing && !botAI->diveHoldSurface)
    {
        return false;
    }

    // Bot-only concern; players drive themselves.
    if (!bot->IsAlive() || bot->HasWaterBreathingAura())
        return false;

    uint32 const now = getMSTime();
    uint32 elapsed = 0;
    if (botAI->diveLastTickMs)
        elapsed = std::min<uint32>(now - botAI->diveLastTickMs, 10000);  // ticks can be 1-3 s
    botAI->diveLastTickMs = now;

    // WaterBreath.Timer is already in milliseconds and the core consumes it raw.
    int32 const maxAir = int32(sWorld->getIntConfig(CONFIG_WATER_BREATH_TIMER))
                         * bot->GetTotalAuraMultiplier(SPELL_AURA_MOD_WATER_BREATHING);

    if (bot->IsUnderWater())
    {
        // Clamp the carried value instead of resetting it: a brief surface dip must not refill
        // the model while the core's breath timer is still nearly empty.
        if (botAI->diveAirMs > maxAir)
            botAI->diveAirMs = maxAir;
        if (!botAI->diveSubmergedSince)
            botAI->diveSubmergedSince = now;
        botAI->diveAirMs = std::max<int32>(0, botAI->diveAirMs - int32(elapsed));

        // Surface with a quarter of the breath left (15s floor): "low air" has to fire on every
        // realm config, including long-breath ones like WaterBreathTimer = 180000 (ms).
        int32 const diveAirSafetyMs = std::max(int32(15 * IN_MILLISECONDS), maxAir / 4);
        // "Idle" means nothing is being done under water: a bot grinding, fighting or swimming
        // to a destination must not be dragged up every tick (it yoyos with the grind skipper).
        bool const stationary = !bot->IsInCombat() && !bot->GetVictim() && !bot->isMoving();
        if (!stationary || !AI_VALUE(LootObject, "loot target").IsEmpty())
        {
            botAI->diveIdleSince = 0;
        }
        else if (!botAI->diveIdleSince)
        {
            botAI->diveIdleSince = now;
        }
        constexpr uint32 diveIdleGraceMs = 3000;
        bool const idle = botAI->diveIdleSince && now - botAI->diveIdleSince >= diveIdleGraceMs;

        // After an emergency surface the bot holds at the surface until air is back to half;
        // dips below that bounce straight back up via this trigger.
        bool const holding = botAI->diveHoldSurface && botAI->diveAirMs < maxAir / 2;
        bool const active = idle || holding || botAI->diveAirMs <= diveAirSafetyMs;
        if (active && !idle)
            LOG_DEBUG("playerbots", "swim-trace {}: dive safety (air {:.0f}s, {})", bot->GetName(),
                      float(botAI->diveAirMs) / IN_MILLISECONDS, holding ? "holding at surface" : "low air");
        return active;
    }

    // At the surface / on land: air regenerates at the mirror timer's 10x rate.
    botAI->diveSubmergedSince = 0;
    if (botAI->diveSurfacing)
    {
        botAI->diveSurfacing = false;
        botAI->diveHoldSurface = true;
    }
    botAI->diveIdleSince = 0;
    // Cap the surface regen per AI tick: the core updates the breath timer continuously, but the
    // bot only samples on AI ticks. A slow tick spanning both underwater and surface time must not
    // refill the model faster than the bot actually surfaced.
    uint32 const regenElapsed = std::min<uint32>(elapsed, 3000);
    if (botAI->diveAirMs < maxAir)
        botAI->diveAirMs = std::min(maxAir, botAI->diveAirMs + int32(10 * regenElapsed));

    // "Dry" means genuinely out of the water, not merely the surface band where the engine
    // reports IN_WATER while IsInWater() is false. Resetting the air model or dropping the hold
    // there makes a surfaced bot re-dive with a still-low core breath timer.
    auto const liquid = bot->GetMap()->GetLiquidData(bot->GetPhaseMask(), bot->GetPositionX(),
                                                     bot->GetPositionY(), bot->GetPositionZ(),
                                                     bot->GetCollisionHeight(), {});
    bool const onDryLand = !bot->IsInWater() && !bot->IsUnderWater() &&
                           (liquid.Level <= INVALID_HEIGHT || bot->GetPositionZ() >= liquid.Level + 0.5f);

    if (botAI->diveHoldSurface && (botAI->diveAirMs >= maxAir / 2 || onDryLand))
    {
        botAI->diveHoldSurface = false;
        LOG_DEBUG("playerbots", "swim-trace {}: hold released (air {:.0f}s, dry={})", bot->GetName(),
                  float(botAI->diveAirMs) / IN_MILLISECONDS, onDryLand ? 1 : 0);
    }

    // Only a genuinely dry bot has a full breath bar again; the surface band must keep carrying
    // the real deficit into the next dive.
    if (onDryLand)
        botAI->diveAirMs = INT32_MAX;

    return false;
}
