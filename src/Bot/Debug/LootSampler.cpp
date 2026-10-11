/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// TEST-ONLY (A/B harness): see LootSampler.h.

#include "LootSampler.h"

#include "Engine.h"
#include "LootObjectStack.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include <cstdio>
#include <ctime>

namespace
{
bool g_enabled = false;
std::FILE* g_file = nullptr;
time_t g_lastSample = 0;

char const* StateName(BotState state)
{
    switch (state)
    {
        case BOT_STATE_COMBAT:
            return "combat";
        case BOT_STATE_DEAD:
            return "dead";
        default:
            return "noncombat";
    }
}
}  // namespace

void LootSampler::SetEnabled(bool enabled)
{
    g_enabled = enabled;
    if (g_file)
    {
        std::fclose(g_file);
        g_file = nullptr;
    }

    if (enabled)
        g_file = std::fopen("playerbots_lootsample.log", "w");
}

bool LootSampler::IsEnabled() { return g_enabled; }

void LootSampler::Update()
{
    if (!g_enabled || !g_file)
        return;

    time_t const now = time(nullptr);
    if (now - g_lastSample < 5)
        return;
    g_lastSample = now;

    // Deterministic 1/40 subset of the online random bots; the copy is cheap at a 5s cadence.
    PlayerBotMap const bots = sRandomPlayerbotMgr.GetAllBots();
    for (auto const& entry : bots)
    {
        Player* bot = entry.second;
        if (!bot || !bot->IsInWorld() || bot->GetGUID().GetCounter() % 40 != 0)
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            continue;

        LootObject loot = botAI->GetAiObjectContext()->GetValue<LootObject>("loot target")->Get();
        float tdist = -1.0f;
        bool possible = false;
        uint32 tguid = 0;
        if (!loot.IsEmpty())
        {
            tguid = uint32(loot.guid.GetCounter());
            if (WorldObject* obj = loot.GetWorldObject(bot))
                tdist = bot->GetDistance(obj);
            possible = tdist >= 0.0f && loot.IsLootPossible(bot);
        }

        bool const canLoot = botAI->GetAiObjectContext()->GetValue<bool>("can loot")->Get();
        bool const avail = botAI->GetAiObjectContext()->GetValue<bool>("has available loot")->Get();
        LootObjectStack* stack = botAI->GetAiObjectContext()->GetValue<LootObjectStack*>("available loot")->Get();
        uint32 const stackSize = stack ? uint32(stack->Size()) : 0;
        Engine* engine = botAI->GetCurrentEngine();

        char const* tag = "";
        if (bot->IsInCombat() && possible && tdist <= 6.0f)
            tag = "combat_with_loot";
        else if (bot->IsMounted() && !canLoot && (possible || stackSize > 0))
            tag = "mounted_with_loot";
        else if (possible && tdist > 50.0f)
            tag = "stale_target";

        std::fprintf(g_file,
                     "ts=%ld bot=%s lvl=%u state=%s action=%s mounted=%d combat=%d moving=%d tguid=%u tdist=%.1f "
                     "tposs=%d canloot=%d stack=%u avail=%d tag=%s\n",
                     long(now), bot->GetName().c_str(), bot->GetLevel(), StateName(botAI->GetState()),
                     engine ? engine->GetLastAction().c_str() : "-", bot->IsMounted() ? 1 : 0,
                     bot->IsInCombat() ? 1 : 0, bot->isMoving() ? 1 : 0, tguid, tdist, possible ? 1 : 0,
                     canLoot ? 1 : 0, stackSize, avail ? 1 : 0, tag);
    }

    std::fflush(g_file);
}
