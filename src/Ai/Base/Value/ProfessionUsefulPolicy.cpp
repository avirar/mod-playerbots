/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ProfessionUsefulPolicy.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "BagPolicy.h"
#include "DBCStructure.h"
#include "ItemTemplate.h"
#include "ItemUsageValue.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace
{
std::array<uint16, 14> const PROFESSION_SKILLS = {
    SKILL_ALCHEMY,     SKILL_BLACKSMITHING, SKILL_ENCHANTING,     SKILL_ENGINEERING, SKILL_HERBALISM,
    SKILL_INSCRIPTION, SKILL_JEWELCRAFTING, SKILL_LEATHERWORKING, SKILL_MINING,      SKILL_SKINNING,
    SKILL_TAILORING,   SKILL_COOKING,       SKILL_FIRST_AID,      SKILL_FISHING};

bool IsProfessionSkill(uint32 skillLine)
{
    return std::find(PROFESSION_SKILLS.begin(), PROFESSION_SKILLS.end(), uint16(skillLine)) != PROFESSION_SKILLS.end();
}

struct ToolRequirementCache
{
    size_t spellCount{0};
    std::unordered_set<uint32> items;
    std::unordered_set<uint32> categories;
};

thread_local std::unordered_map<uint64_t, ToolRequirementCache> toolCache;

ToolRequirementCache const& GetToolRequirements(Player* bot)
{
    uint64_t const key = bot->GetGUID().GetCounter();
    size_t const spellCount = bot->GetSpellMap().size();

    ToolRequirementCache& cached = toolCache[key];
    if (cached.spellCount == spellCount)
        return cached;

    cached.spellCount = spellCount;
    cached.items.clear();
    cached.categories.clear();

    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            continue;

        bool professionSpell = false;
        SkillLineAbilityMapBounds const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineAbilityEntry const* ability = itr->second;
            if (ability && IsProfessionSkill(ability->SkillLine))
            {
                professionSpell = true;
                break;
            }
        }
        if (!professionSpell)
            continue;

        for (uint8 t = 0; t < 2; ++t)
        {
            if (spellInfo->Totem[t])
                cached.items.insert(spellInfo->Totem[t]);
            if (spellInfo->TotemCategory[t])
                cached.categories.insert(spellInfo->TotemCategory[t]);
        }
    }

    return cached;
}
}  // namespace

namespace ProfessionUsefulPolicy
{
bool IsMissingToolForBot(PlayerbotAI* botAI, ItemTemplate const* proto)
{
    if (!botAI || !proto)
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    if (bot->HasItemCount(proto->ItemId, 1, false))
        return false;

    if (proto->TotemCategory && bot->HasItemTotemCategory(proto->TotemCategory))
        return false;

    ToolRequirementCache const& requirements = GetToolRequirements(bot);
    if (requirements.items.count(proto->ItemId))
        return true;

    return proto->TotemCategory && requirements.categories.count(proto->TotemCategory) != 0;
}

uint8 GetUsefulRank(PlayerbotAI* botAI, ItemTemplate const* proto, ItemUsage usage)
{
    if (!botAI || !proto)
        return 255;

    if (IsMissingToolForBot(botAI, proto))
        return 0;

    Player* bot = botAI->GetBot();
    if (bot && proto->Class == ITEM_CLASS_CONTAINER && BagPolicy::IsBagUpgradeForBot(bot, proto))
        return 2;

    switch (usage)
    {
        case ITEM_USAGE_EQUIP:
        case ITEM_USAGE_REPLACE:
        case ITEM_USAGE_BAD_EQUIP:
        case ITEM_USAGE_BROKEN_EQUIP:
            return 1;
        case ITEM_USAGE_USE:
        case ITEM_USAGE_QUEST:
        case ITEM_USAGE_GUILD_TASK:
        case ITEM_USAGE_AMMO:
            return 3;
        case ITEM_USAGE_KEEP:
            // Best weapon stone/oil for the bot's weapons. Other KEEP items
            // are intermediates and go through the craft planner instead.
            if (!ItemUsageValue(botAI).IsTemporaryWeaponEnchantment(proto))
                return 255;
            return bot && !bot->HasItemCount(proto->ItemId, 1, false) ? 4 : 255;
        default:
            return 255;
    }
}

bool IsFinalUseful(PlayerbotAI* botAI, ItemTemplate const* proto, ItemUsage usage)
{
    return GetUsefulRank(botAI, proto, usage) != 255;
}
}  // namespace ProfessionUsefulPolicy
