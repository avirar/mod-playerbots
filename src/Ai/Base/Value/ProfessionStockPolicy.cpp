/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ProfessionStockPolicy.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DBCStores.h"
#include "ItemTemplate.h"
#include "ItemUsageValue.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotSpellRepository.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace
{
constexpr uint32 VENDOR_STOCK_STACKS = 2;
constexpr uint32 TRADE_GOODS_COAL = 3857;
constexpr int32 STOCK_TIER_WINDOW = 25;

// Enchanting scroll recipes target a vellum instead of listing it as a reagent.
// The vellum tiers follow the scroll levels and are crafted by Inscription, so
// the factory supplies the level-appropriate pair directly.
constexpr uint32 VELLUM_ARMOR_I = 38682;
constexpr uint32 VELLUM_WEAPON_I = 39349;
constexpr uint32 VELLUM_ARMOR_II = 37602;
constexpr uint32 VELLUM_WEAPON_II = 39350;
constexpr uint32 VELLUM_ARMOR_III = 43145;
constexpr uint32 VELLUM_WEAPON_III = 43146;
constexpr uint32 VELLUM_TIER_II_SKILL = 200;
constexpr uint32 VELLUM_TIER_III_SKILL = 350;

std::array<uint16, 14> const PROFESSION_SKILLS = {
    SKILL_ALCHEMY,     SKILL_BLACKSMITHING, SKILL_ENCHANTING,     SKILL_ENGINEERING, SKILL_HERBALISM,
    SKILL_INSCRIPTION, SKILL_JEWELCRAFTING, SKILL_LEATHERWORKING, SKILL_MINING,      SKILL_SKINNING,
    SKILL_TAILORING,   SKILL_COOKING,       SKILL_FIRST_AID,      SKILL_FISHING};

struct SpellCandidate
{
    int32 trivialLow{0};
    bool skillUp{false};
    std::vector<uint32> reagents;
};

struct CacheEntry
{
    uint64_t fingerprint{0};
    std::vector<uint32> items;
    std::unordered_set<uint32> knownReagents;
};

thread_local std::unordered_map<uint64_t, CacheEntry> stockCache;

bool IsProfessionSkill(uint32 skillLine)
{
    return std::find(PROFESSION_SKILLS.begin(), PROFESSION_SKILLS.end(), uint16(skillLine)) != PROFESSION_SKILLS.end();
}

uint64_t BuildFingerprint(Player* bot)
{
    uint64_t hash = 1469598103934665603ULL;
    auto mix = [&hash](uint64_t value)
    {
        hash ^= value;
        hash *= 1099511628211ULL;
    };

    mix(bot->GetSpellMap().size());
    for (uint16 skill : PROFESSION_SKILLS)
    {
        mix(bot->GetPureSkillValue(skill));
        mix(bot->GetPureMaxSkillValue(skill));
    }
    return hash;
}

std::vector<uint32> ScanProfessionStockItems(Player* bot, std::unordered_set<uint32>& knownReagents)
{
    std::unordered_map<uint32, std::vector<SpellCandidate>> bySkill;
    std::unordered_set<uint32> craftable;

    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            continue;

        for (uint8 effect = 0; effect < MAX_SPELL_EFFECTS; ++effect)
        {
            if ((spellInfo->Effects[effect].Effect == SPELL_EFFECT_CREATE_ITEM ||
                 spellInfo->Effects[effect].Effect == SPELL_EFFECT_CREATE_ITEM_2) &&
                spellInfo->Effects[effect].ItemType)
            {
                craftable.insert(spellInfo->Effects[effect].ItemType);
            }
        }

        std::vector<uint32> reagents;
        for (uint8 reagent = 0; reagent < MAX_SPELL_REAGENTS; ++reagent)
        {
            uint32 const itemId = spellInfo->Reagent[reagent];
            if (!spellInfo->ReagentCount[reagent] || !itemId)
                continue;

            if (IsProfessionVendorStockCandidate(sObjectMgr->GetItemTemplate(itemId)))
                reagents.push_back(itemId);
        }
        if (reagents.empty())
            continue;

        bool const skillUp = ItemUsageValue::CraftSkillGainChance(spellId, bot) > 0;
        SkillLineAbilityMapBounds const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineAbilityEntry const* ability = itr->second;
            if (!ability || !IsProfessionSkill(ability->SkillLine))
                continue;

            bySkill[ability->SkillLine].push_back({int32(ability->TrivialSkillLineRankLow), skillUp, reagents});
            knownReagents.insert(reagents.begin(), reagents.end());
        }
    }

    std::unordered_set<uint32> result;
    for (auto const& [skillLine, spells] : bySkill)
    {
        if (!bot->HasSkill(skillLine) || spells.empty())
            continue;

        bool hasSkillUp = false;
        int32 highestTrivial = 0;
        for (SpellCandidate const& spell : spells)
        {
            hasSkillUp = hasSkillUp || spell.skillUp;
            highestTrivial = std::max(highestTrivial, spell.trivialLow);
        }

        for (SpellCandidate const& spell : spells)
        {
            if (hasSkillUp && !spell.skillUp)
                continue;
            if (!hasSkillUp && spell.trivialLow < highestTrivial - STOCK_TIER_WINDOW)
                continue;

            for (uint32 itemId : spell.reagents)
            {
                if (!craftable.count(itemId))
                    result.insert(itemId);
            }
        }
    }

    return std::vector<uint32>(result.begin(), result.end());
}

CacheEntry const& GetCachedEntry(Player* bot)
{
    uint64_t const key = bot->GetGUID().GetCounter();
    uint64_t const fingerprint = BuildFingerprint(bot);
    CacheEntry& entry = stockCache[key];
    if (entry.fingerprint != fingerprint)
    {
        entry.fingerprint = fingerprint;
        entry.knownReagents.clear();
        entry.items = ScanProfessionStockItems(bot, entry.knownReagents);
    }
    return entry;
}

bool GetVellumStockPair(PlayerbotAI* botAI, uint32& armorVellum, uint32& weaponVellum)
{
    if (!botAI || !botAI->GetBot() || !botAI->HasSkill(SKILL_ENCHANTING))
        return false;

    uint32 const skill = botAI->GetBot()->GetPureSkillValue(SKILL_ENCHANTING);
    if (skill >= VELLUM_TIER_III_SKILL)
    {
        armorVellum = VELLUM_ARMOR_III;
        weaponVellum = VELLUM_WEAPON_III;
    }
    else if (skill >= VELLUM_TIER_II_SKILL)
    {
        armorVellum = VELLUM_ARMOR_II;
        weaponVellum = VELLUM_WEAPON_II;
    }
    else
    {
        armorVellum = VELLUM_ARMOR_I;
        weaponVellum = VELLUM_WEAPON_I;
    }
    return true;
}
}  // namespace

bool IsProfessionVendorStockCandidate(ItemTemplate const* proto)
{
    if (!proto || proto->RequiredSkill || proto->RequiredSkillRank || !proto->BuyPrice)
        return false;

    // Unlimited-stock vendor items are bought reagents even when they are not
    // trade goods (e.g. Refreshing Spring Water for Cooking). Limited-stock
    // listings are often gathered materials, which stay stack-unlimited under
    // the material rule.
    if (PlayerbotSpellRepository::Instance().IsItemBuyable(proto->ItemId))
        return true;

    if (proto->Class != ITEM_CLASS_TRADE_GOODS)
        return false;

    std::string const& name = proto->Name1;
    if (name.find("Vial") != std::string::npos || name.find("Thread") != std::string::npos ||
        name.find("Flux") != std::string::npos || name.find("Parchment") != std::string::npos ||
        name.find("Spice") != std::string::npos || name.find("Dye") != std::string::npos)
        return true;

    return name == "Bleach" || name == "Coal" || name == "Simple Flour" || name == "Simple Wood" ||
           name == "Flint and Tinder" || name == "Salt" || proto->ItemId == TRADE_GOODS_COAL;
}

uint32 GetProfessionVendorStockTarget(PlayerbotAI* botAI, ItemTemplate const* proto)
{
    if (!botAI || !proto || !botAI->GetBot())
        return 0;

    std::vector<uint32> const& items = GetCachedEntry(botAI->GetBot()).items;
    if (std::find(items.begin(), items.end(), proto->ItemId) == items.end())
        return 0;

    return VENDOR_STOCK_STACKS * std::max<uint32>(1, proto->GetMaxStackSize());
}

bool IsKnownProfessionVendorReagent(PlayerbotAI* botAI, ItemTemplate const* proto)
{
    if (!botAI || !proto || !botAI->GetBot())
        return false;

    return GetCachedEntry(botAI->GetBot()).knownReagents.count(proto->ItemId) != 0;
}

std::vector<uint32> GetProfessionVendorStockItems(PlayerbotAI* botAI)
{
    if (!botAI || !botAI->GetBot())
        return {};

    return GetCachedEntry(botAI->GetBot()).items;
}

uint32 GetProfessionCraftedStockTarget(PlayerbotAI* botAI, ItemTemplate const* proto)
{
    uint32 armorVellum = 0;
    uint32 weaponVellum = 0;
    if (!proto || !GetVellumStockPair(botAI, armorVellum, weaponVellum))
        return 0;

    if (proto->ItemId != armorVellum && proto->ItemId != weaponVellum)
        return 0;

    return VENDOR_STOCK_STACKS * std::max<uint32>(1, proto->GetMaxStackSize());
}

std::vector<uint32> GetProfessionCraftedStockItems(PlayerbotAI* botAI)
{
    uint32 armorVellum = 0;
    uint32 weaponVellum = 0;
    if (!GetVellumStockPair(botAI, armorVellum, weaponVellum))
        return {};

    return {armorVellum, weaponVellum};
}
