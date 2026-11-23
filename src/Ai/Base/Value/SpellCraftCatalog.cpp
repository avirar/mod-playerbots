/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SpellCraftCatalog.h"

#include <algorithm>

#include "DBCStores.h"
#include "DBCStructure.h"
#include "Log.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

SpellCraftCatalog::SpellCraftCatalog() { Build(); }

SpellCraftCatalog& SpellCraftCatalog::Instance()
{
    static SpellCraftCatalog instance;
    return instance;
}

void SpellCraftCatalog::Warm()
{
    SpellCraftCatalog& catalog = Instance();
    catalog.Build();
}

void SpellCraftCatalog::Build()
{
    if (built)
        return;

    if (!sSpellMgr || !sSpellMgr->GetSpellInfoStoreSize())
    {
        LOG_WARN("playerbots", "Spell craft catalog built before spell data was loaded");
        return;
    }

    built = true;

    for (uint32 spellId = 0; spellId < sSpellMgr->GetSpellInfoStoreSize(); ++spellId)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            continue;

        SpellCraftInfo info;
        info.spellId = spellId;
        info.requiresSpellFocus = spellInfo->RequiresSpellFocus;
        info.recoveryTime = spellInfo->RecoveryTime;
        info.categoryRecoveryTime = spellInfo->CategoryRecoveryTime;

        for (uint8 effect = 0; effect < MAX_SPELL_EFFECTS; ++effect)
        {
            uint32 const effectType = spellInfo->Effects[effect].Effect;
            if (effectType == SPELL_EFFECT_CREATE_ITEM || effectType == SPELL_EFFECT_CREATE_ITEM_2)
            {
                // A create effect with no fixed item is still a reagent consumer
                // (random loot crafting); keep it out of the planner but not out
                // of the item usage reagent index.
                info.hasCraftEffect = true;

                uint32 const outputItemId = spellInfo->Effects[effect].ItemType;
                if (!outputItemId)
                    continue;

                info.outputItemId = outputItemId;
                info.outputCount = uint32(std::max(1, spellInfo->Effects[effect].BasePoints + 1));
                info.isLootCrafting = spellInfo->IsLootCrafting();
                break;
            }

            if (effectType == SPELL_EFFECT_ENCHANT_ITEM || effectType == SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY ||
                effectType == SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC)
            {
                info.isEnchant = true;
                info.scrollItemId = spellInfo->Effects[effect].ItemType;
                info.hasCraftEffect = true;
                break;
            }
        }

        // The planner uses the first output; CanCraftItem checks every create
        // effect so a multi-output craft is not mistaken for a missing recipe.
        for (uint8 effect = 0; effect < MAX_SPELL_EFFECTS; ++effect)
        {
            uint32 const effectType = spellInfo->Effects[effect].Effect;
            if (effectType != SPELL_EFFECT_CREATE_ITEM && effectType != SPELL_EFFECT_CREATE_ITEM_2)
                continue;

            uint32 const outputItemId = spellInfo->Effects[effect].ItemType;
            if (!outputItemId)
                continue;

            if (std::find(info.outputItemIds.begin(), info.outputItemIds.end(), outputItemId) ==
                info.outputItemIds.end())
            {
                info.outputItemIds.push_back(outputItemId);
            }
        }

        for (uint8 reagent = 0; reagent < MAX_SPELL_REAGENTS; ++reagent)
        {
            uint32 const itemId = uint32(spellInfo->Reagent[reagent]);
            if (!spellInfo->ReagentCount[reagent] || !itemId)
                continue;

            info.reagents[reagent] = itemId;
            info.reagentCounts[reagent] = uint8(spellInfo->ReagentCount[reagent]);
            info.reagentItems += spellInfo->ReagentCount[reagent];

            // Mirror ItemUsageValue::SpellsUsingItem: class spells consume
            // reagents without creating an item; generic profession spells
            // must create or enchant something to be a real reagent consumer.
            if (!info.hasCraftEffect && spellInfo->SpellFamilyName == SPELLFAMILY_GENERIC)
                continue;

            std::vector<uint32>& reagentSpellList = reagentSpells[itemId];
            if (std::find(reagentSpellList.begin(), reagentSpellList.end(), spellId) == reagentSpellList.end())
                reagentSpellList.push_back(spellId);
        }

        SkillLineAbilityMapBounds const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineAbilityEntry const* ability = itr->second;
            if (!ability)
                continue;

            SpellCraftAbility craftAbility;
            craftAbility.skillLine = ability->SkillLine;
            craftAbility.minSkillLineRank = ability->MinSkillLineRank;
            craftAbility.trivialSkillLineRankLow = ability->TrivialSkillLineRankLow;
            craftAbility.trivialSkillLineRankHigh = ability->TrivialSkillLineRankHigh;
            info.abilities.push_back(craftAbility);
        }

        if (info.hasCraftEffect || info.reagentItems || !info.abilities.empty())
            spells.emplace(spellId, std::move(info));
    }
}

SpellCraftInfo const* SpellCraftCatalog::Get(uint32 spellId) const
{
    auto const itr = spells.find(spellId);
    return itr == spells.end() ? nullptr : &itr->second;
}

std::vector<uint32> const& SpellCraftCatalog::SpellsUsingItem(uint32 itemId) const
{
    static std::vector<uint32> const empty;
    auto const itr = reagentSpells.find(itemId);
    return itr == reagentSpells.end() ? empty : itr->second;
}
