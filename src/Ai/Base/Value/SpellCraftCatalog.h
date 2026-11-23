/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SPELLCRAFTCATALOG_H
#define PLAYERBOTS_SPELLCRAFTCATALOG_H

#include <array>
#include <unordered_map>
#include <vector>

#include "Common.h"
#include "DBCStructure.h"
#include "SharedDefines.h"

class SpellInfo;

struct SpellCraftAbility
{
    uint32 skillLine{0};
    uint32 minSkillLineRank{0};
    uint32 trivialSkillLineRankLow{0};
    uint32 trivialSkillLineRankHigh{0};
};

// Immutable spell metadata read from Spell.dbc and SkillLineAbility.dbc once at
// startup. Both the item usage value and the profession planner need the same
// effect, reagent and skill-line data; rebuilding it from SpellInfo on every
// evaluation is the dominant cost of the profession scan.
struct SpellCraftInfo
{
    uint32 spellId{0};
    uint32 outputItemId{0};
    uint32 outputCount{1};
    std::vector<uint32> outputItemIds;
    uint32 scrollItemId{0};
    uint32 requiresSpellFocus{0};
    uint32 recoveryTime{0};
    uint32 categoryRecoveryTime{0};
    uint32 reagentItems{0};
    std::array<uint32, MAX_SPELL_REAGENTS> reagents{};
    std::array<uint8, MAX_SPELL_REAGENTS> reagentCounts{};
    bool hasCraftEffect{false};
    bool isEnchant{false};
    bool isLootCrafting{false};
    std::vector<SpellCraftAbility> abilities;
};

class SpellCraftCatalog
{
public:
    static SpellCraftCatalog& Instance();

    // Builds the catalog. Called after Spell.dbc and SkillLineAbility.dbc are
    // loaded; safe to call more than once.
    void Warm();

    SpellCraftInfo const* Get(uint32 spellId) const;
    // Spells that consume the item as a reagent and are valid for item usage:
    // class spells always, generic profession spells only when they create or
    // enchant an item.
    std::vector<uint32> const& SpellsUsingItem(uint32 itemId) const;

private:
    SpellCraftCatalog();
    void Build();

    std::unordered_map<uint32, SpellCraftInfo> spells;
    std::unordered_map<uint32, std::vector<uint32>> reagentSpells;
    bool built{false};
};

#endif
