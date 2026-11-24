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
#include <vector>

#include "Bag.h"
#include "BagPolicy.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "SkeletonKey.h"
#include "ItemUsageValue.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "SharedDefines.h"
#include "SpellAuraDefines.h"
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

uint32 CountBits(uint32 mask)
{
    uint32 count = 0;
    while (mask)
    {
        mask &= mask - 1;
        ++count;
    }
    return count;
}

struct ToolRequirementsState
{
    uint32 coverage = 0;
    bool rods = false;
    bool fishing = false;
    std::unordered_set<uint32> otherCategories;
    std::unordered_set<uint32> items;
};

ToolRequirementsState GetToolRequirementsState(Player* bot)
{
    ToolRequirementsState state;
    ToolRequirementCache const& cache = GetToolRequirements(bot);
    state.items = cache.items;
    state.fishing = bot->HasSkill(SKILL_FISHING);

    for (uint32 categoryId : cache.categories)
    {
        TotemCategoryEntry const* category = sTotemCategoryStore.LookupEntry(categoryId);
        if (!category)
            continue;

        if (category->categoryType == 24)
            state.coverage |= category->categoryMask;
        else if (category->categoryType == 3)
            state.rods = true;
        else
            state.otherCategories.insert(categoryId);
    }

    return state;
}

uint32 GetToolSkillBonus(ItemTemplate const* proto, uint32 skillLine)
{
    if (!proto || !skillLine)
        return 0;

    uint32 bonus = 0;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_EQUIP || !proto->Spells[i].SpellId)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(proto->Spells[i].SpellId);
        if (!spellInfo)
            continue;

        for (SpellEffectInfo const& effect : spellInfo->Effects)
        {
            if (effect.Effect == SPELL_EFFECT_APPLY_AURA && effect.ApplyAuraName == SPELL_AURA_MOD_SKILL &&
                effect.MiscValue == int32(skillLine))
                bonus += std::max(0, effect.BasePoints + 1);
        }
    }

    return bonus;
}

bool IsToolCategory(TotemCategoryEntry const* category)
{
    if (!category)
        return false;

    if (category->categoryType == 3 || category->categoryType == 22)
        return true;

    if (category->categoryType != 24)
        return false;

    // Not every type-24 category is a profession tool: 141 is Drums, a
    // consumable. List the actual tool categories explicitly.
    switch (category->ID)
    {
        case 14:   // Arclight Spanner
        case 15:   // Gyromatic Micro-Adjustor
        case 121:  // Virtuoso Inking Set
        case 161:  // Gnomish Army Knife
        case 162:  // Blacksmith Hammer
        case 165:  // Mining Pick
        case 166:  // Skinning Knife
        case 167:  // Hammer Pick
        case 168:  // Bladed Pickaxe
        case 169:  // Flint and Tinder
            return true;
        default:
            return false;
    }
}

bool IsToolItemInternal(ItemTemplate const* proto)
{
    if (!proto)
        return false;

    if (proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE)
        return true;

    // Jewelcrafting kit and grinder are SpellInfo::Totem item requirements, not
    // TotemCategory items.
    if (proto->ItemId == 20815 || proto->ItemId == 20824)
        return true;

    if (!proto->TotemCategory)
        return false;

    return IsToolCategory(sTotemCategoryStore.LookupEntry(proto->TotemCategory));
}

struct ToolProfile
{
    ItemTemplate const* proto = nullptr;
    TotemCategoryEntry const* category = nullptr;
    bool isTool = false;
    bool isRod = false;
    bool isMulti = false;
    bool isGnomish = false;
    bool isFishingRod = false;
    bool usable = false;
    uint32 skillBonus = 0;
    uint32 requiredCoverage = 0;
    uint32 coveredCount = 0;
};

ToolProfile GetToolProfile(Player* bot, ItemTemplate const* proto, ToolRequirementsState const& required)
{
    ToolProfile profile;
    profile.proto = proto;
    if (!proto)
        return profile;

    profile.isTool = IsToolItemInternal(proto);
    if (!profile.isTool)
        return profile;

    profile.isFishingRod = proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE;
    if (profile.isFishingRod)
        profile.skillBonus = GetToolSkillBonus(proto, SKILL_FISHING);

    if (proto->TotemCategory)
    {
        profile.category = sTotemCategoryStore.LookupEntry(proto->TotemCategory);
        if (profile.category)
        {
            profile.isRod = profile.category->categoryType == 3;
            profile.isMulti = profile.category->categoryType == 24 && CountBits(profile.category->categoryMask) > 1;
            profile.isGnomish = proto->ItemId == 40772;

            if (profile.category->categoryType == 24)
            {
                profile.requiredCoverage = profile.category->categoryMask & required.coverage;
                profile.coveredCount = CountBits(profile.requiredCoverage);

                if (profile.category->ID == 165)
                    profile.skillBonus = GetToolSkillBonus(proto, SKILL_MINING);
                else if (profile.category->ID == 166)
                    profile.skillBonus = GetToolSkillBonus(proto, SKILL_SKINNING);
            }
        }
    }

    profile.usable = bot->CanUseItem(proto) == EQUIP_ERR_OK;
    return profile;
}

float ToolDps(ItemTemplate const* proto)
{
    return proto->Delay ? (proto->Damage[0].DamageMin + proto->Damage[0].DamageMax) * 500.0f / proto->Delay : 0.0f;
}

bool IsBetterTool(ItemTemplate const* candidate, ItemTemplate const* current)
{
    if (!current)
        return true;

    if (candidate->Quality != current->Quality)
        return candidate->Quality > current->Quality;

    if (candidate->ItemLevel != current->ItemLevel)
        return candidate->ItemLevel > current->ItemLevel;

    float const candidateDps = ToolDps(candidate);
    float const currentDps = ToolDps(current);
    if (candidateDps != currentDps)
        return candidateDps > currentDps;

    return candidate->ItemId < current->ItemId;
}

bool IsBetterRod(ItemTemplate const* candidate, ItemTemplate const* current)
{
    if (!current)
        return true;

    if (candidate->RequiredSkillRank != current->RequiredSkillRank)
        return candidate->RequiredSkillRank > current->RequiredSkillRank;

    TotemCategoryEntry const* candidateCategory = sTotemCategoryStore.LookupEntry(candidate->TotemCategory);
    TotemCategoryEntry const* currentCategory = sTotemCategoryStore.LookupEntry(current->TotemCategory);
    uint32 const candidateBits = candidateCategory ? CountBits(candidateCategory->categoryMask) : 0;
    uint32 const currentBits = currentCategory ? CountBits(currentCategory->categoryMask) : 0;
    if (candidateBits != currentBits)
        return candidateBits > currentBits;

    return IsBetterTool(candidate, current);
}

bool IsBetterFishingRod(ItemTemplate const* candidate, ItemTemplate const* current)
{
    uint32 const candidateBonus = GetToolSkillBonus(candidate, SKILL_FISHING);
    uint32 const currentBonus = GetToolSkillBonus(current, SKILL_FISHING);
    if (candidateBonus != currentBonus)
        return candidateBonus > currentBonus;

    return IsBetterTool(candidate, current);
}

std::vector<ItemTemplate const*> GetOwnedToolTemplates(Player* bot)
{
    std::vector<ItemTemplate const*> result;

    auto add = [&result](Item* item)
    {
        if (!item)
            return;

        ItemTemplate const* proto = item->GetTemplate();
        if (IsToolItemInternal(proto))
            result.push_back(proto);
    };

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        add(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

    for (uint8 slot = KEYRING_SLOT_START; slot < CURRENCYTOKEN_SLOT_END; ++slot)
        add(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                add(bot->GetItemByPos(bagSlot, slot));
    }

    return result;
}

bool IsUsefulMultiTool(PlayerbotAI* botAI, ToolProfile const& profile)
{
    if (!profile.isMulti)
        return false;

    if (profile.isGnomish && botAI->HasSkill(SKILL_ENGINEERING))
        return true;

    return profile.coveredCount >= 2;
}

bool IsSelectedToolInternal(PlayerbotAI* botAI, ItemTemplate const* proto)
{
    Player* bot = botAI->GetBot();
    ToolRequirementsState const required = GetToolRequirementsState(bot);
    ToolProfile const profile = GetToolProfile(bot, proto, required);
    if (!profile.isTool)
        return false;

    std::vector<ItemTemplate const*> const owned = GetOwnedToolTemplates(bot);

    if (profile.isFishingRod)
    {
        if (!required.fishing || !profile.usable)
            return false;

        ItemTemplate const* best = nullptr;
        for (ItemTemplate const* candidate : owned)
        {
            ToolProfile const candidateProfile = GetToolProfile(bot, candidate, required);
            if (!candidateProfile.isFishingRod || !candidateProfile.usable)
                continue;

            if (!best || IsBetterFishingRod(candidate, best))
                best = candidate;
        }

        return best && best->ItemId == proto->ItemId;
    }

    if (!profile.category)
        return required.items.count(proto->ItemId) != 0;

    if (profile.isRod)
    {
        if (!required.rods)
            return false;

        ItemTemplate const* bestUsable = nullptr;
        ItemTemplate const* bestAny = nullptr;
        for (ItemTemplate const* candidate : owned)
        {
            TotemCategoryEntry const* category = sTotemCategoryStore.LookupEntry(candidate->TotemCategory);
            if (!category || category->categoryType != 3)
                continue;

            if (!bestAny || IsBetterRod(candidate, bestAny))
                bestAny = candidate;

            if (bot->CanUseItem(candidate) == EQUIP_ERR_OK && (!bestUsable || IsBetterRod(candidate, bestUsable)))
                bestUsable = candidate;
        }

        ItemTemplate const* best = bestUsable ? bestUsable : bestAny;
        return best && best->ItemId == proto->ItemId;
    }

    if (profile.isMulti)
    {
        if (!IsUsefulMultiTool(botAI, profile))
            return false;

        // Engineers keep the Gnomish Army Knife for its on-use Defibrillate
        // even when another multi-tool has better raw quality.
        if (profile.isGnomish && botAI->HasSkill(SKILL_ENGINEERING))
            return true;

        for (ItemTemplate const* candidate : owned)
        {
            if (candidate->ItemId == proto->ItemId)
                continue;

            ToolProfile const candidateProfile = GetToolProfile(bot, candidate, required);
            if (!IsUsefulMultiTool(botAI, candidateProfile))
                continue;

            if ((candidateProfile.requiredCoverage & profile.requiredCoverage) != profile.requiredCoverage)
                continue;

            if (candidateProfile.requiredCoverage != profile.requiredCoverage)
                return false;

            if (IsBetterTool(candidate, proto))
                return false;
        }

        return true;
    }

    bool const familyRequired = profile.category->categoryType == 24
                                    ? (profile.category->categoryMask & required.coverage) != 0
                                    : required.otherCategories.count(profile.category->ID) != 0;
    if (!familyRequired)
        return false;

    // A gathering-skill bonus beats a plain tool and is not replaced by a
    // multi-tool: the extra skill can unlock nodes the bot could not use.
    if (profile.skillBonus > 0)
    {
        for (ItemTemplate const* candidate : owned)
        {
            if (candidate->ItemId == proto->ItemId)
                continue;

            ToolProfile const candidateProfile = GetToolProfile(bot, candidate, required);
            if (!candidateProfile.category || candidateProfile.category->ID != profile.category->ID)
                continue;

            if (candidateProfile.skillBonus > profile.skillBonus ||
                (candidateProfile.skillBonus == profile.skillBonus && IsBetterTool(candidate, proto)))
                return false;
        }

        return true;
    }

    for (ItemTemplate const* candidate : owned)
    {
        if (candidate->ItemId == proto->ItemId)
            continue;

        ToolProfile const candidateProfile = GetToolProfile(bot, candidate, required);
        if (candidateProfile.category && candidateProfile.category->ID == profile.category->ID &&
            candidateProfile.skillBonus > 0)
            return false;

        if (profile.category->categoryType == 24 && candidateProfile.isMulti &&
            IsUsefulMultiTool(botAI, candidateProfile) &&
            (candidateProfile.requiredCoverage & profile.category->categoryMask) != 0)
            return false;

        if (!candidateProfile.isMulti && !candidateProfile.isRod && candidateProfile.category &&
            candidateProfile.category->ID == profile.category->ID && IsBetterTool(candidate, proto))
            return false;
    }

    return true;
}

bool IsToolNeededByBotInternal(PlayerbotAI* botAI, ItemTemplate const* proto)
{
    Player* bot = botAI->GetBot();
    ToolRequirementsState const required = GetToolRequirementsState(bot);
    ToolProfile const profile = GetToolProfile(bot, proto, required);
    if (!profile.isTool)
        return false;

    if (bot->GetItemCount(proto->ItemId, false) > 0)
        return IsSelectedToolInternal(botAI, proto);

    if (profile.isFishingRod)
    {
        if (!required.fishing || !profile.usable)
            return false;

        ItemTemplate const* best = nullptr;
        for (ItemTemplate const* candidate : GetOwnedToolTemplates(bot))
        {
            ToolProfile const candidateProfile = GetToolProfile(bot, candidate, required);
            if (!candidateProfile.isFishingRod || !candidateProfile.usable)
                continue;

            if (!best || IsBetterFishingRod(candidate, best))
                best = candidate;
        }

        return !best || IsBetterFishingRod(proto, best);
    }

    if (!profile.category)
        return required.items.count(proto->ItemId) != 0;

    if (profile.isRod)
    {
        if (!required.rods)
            return false;

        ItemTemplate const* best = nullptr;
        for (ItemTemplate const* candidate : GetOwnedToolTemplates(bot))
        {
            TotemCategoryEntry const* category = sTotemCategoryStore.LookupEntry(candidate->TotemCategory);
            if (!category || category->categoryType != 3)
                continue;

            if (bot->CanUseItem(candidate) != EQUIP_ERR_OK)
                continue;

            if (!best || IsBetterRod(candidate, best))
                best = candidate;
        }

        return !best || IsBetterRod(proto, best);
    }

    if (profile.isMulti)
    {
        if (!IsUsefulMultiTool(botAI, profile))
            return false;

        if (profile.isGnomish && botAI->HasSkill(SKILL_ENGINEERING))
            return true;

        for (ItemTemplate const* candidate : GetOwnedToolTemplates(bot))
        {
            ToolProfile const candidateProfile = GetToolProfile(bot, candidate, required);
            if (!IsUsefulMultiTool(botAI, candidateProfile))
                continue;

            if ((candidateProfile.requiredCoverage & profile.requiredCoverage) != profile.requiredCoverage)
                continue;

            if (candidateProfile.requiredCoverage != profile.requiredCoverage || IsBetterTool(candidate, proto))
                return false;
        }

        return true;
    }

    bool const familyRequired = profile.category->categoryType == 24
                                    ? (profile.category->categoryMask & required.coverage) != 0
                                    : required.otherCategories.count(profile.category->ID) != 0;
    if (!familyRequired)
        return false;

    if (profile.skillBonus > 0)
    {
        for (ItemTemplate const* candidate : GetOwnedToolTemplates(bot))
        {
            ToolProfile const candidateProfile = GetToolProfile(bot, candidate, required);
            if (!candidateProfile.category || candidateProfile.category->ID != profile.category->ID)
                continue;

            if (candidateProfile.skillBonus > profile.skillBonus ||
                (candidateProfile.skillBonus == profile.skillBonus && !IsBetterTool(proto, candidate)))
                return false;
        }

        return true;
    }

    for (ItemTemplate const* candidate : GetOwnedToolTemplates(bot))
    {
        ToolProfile const candidateProfile = GetToolProfile(bot, candidate, required);
        if (candidateProfile.category && candidateProfile.category->ID == profile.category->ID &&
            candidateProfile.skillBonus > 0)
            return false;

        if (profile.category->categoryType == 24 && candidateProfile.isMulti &&
            IsUsefulMultiTool(botAI, candidateProfile) &&
            (candidateProfile.requiredCoverage & profile.category->categoryMask) != 0)
            return false;

        if (!candidateProfile.isMulti && !candidateProfile.isRod && candidateProfile.category &&
            candidateProfile.category->ID == profile.category->ID && !IsBetterTool(proto, candidate))
            return false;
    }

    return true;
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

    // Engineers can craft the Gnomish Army Knife for its on-use Defibrillate
    // and its Engineering/multi-tool coverage. It is never added by the factory.
    if (proto->ItemId == 40772 && botAI->HasSkill(SKILL_ENGINEERING))
        return true;

    if (proto->TotemCategory && bot->HasItemTotemCategory(proto->TotemCategory))
        return false;

    ToolRequirementCache const& requirements = GetToolRequirements(bot);
    if (requirements.items.count(proto->ItemId))
        return true;

    return proto->TotemCategory && requirements.categories.count(proto->TotemCategory) != 0;
}

bool IsToolItem(ItemTemplate const* proto) { return IsToolItemInternal(proto); }

bool IsSelectedTool(PlayerbotAI* botAI, ItemTemplate const* proto)
{
    if (!botAI || !proto || !botAI->GetBot())
        return false;

    if (botAI->GetBot()->GetItemCount(proto->ItemId, false) == 0)
        return false;

    return IsSelectedToolInternal(botAI, proto);
}

bool IsToolNeededByBot(PlayerbotAI* botAI, ItemTemplate const* proto)
{
    if (!botAI || !proto || !botAI->GetBot())
        return false;

    return IsToolNeededByBotInternal(botAI, proto);
}

uint8 GetUsefulRank(PlayerbotAI* botAI, ItemTemplate const* proto, ItemUsage usage)
{
    if (!botAI || !proto)
        return 255;

    if (IsMissingToolForBot(botAI, proto))
        return 0;

    Player* bot = botAI->GetBot();
    if (!bot)
        return 255;

    if (proto->Class == ITEM_CLASS_CONTAINER && BagPolicy::IsBagUpgradeForBot(bot, proto))
        return 2;

    switch (usage)
    {
        case ITEM_USAGE_EQUIP:
        case ITEM_USAGE_REPLACE:
        case ITEM_USAGE_BAD_EQUIP:
        case ITEM_USAGE_BROKEN_EQUIP:
            return 1;
        case ITEM_USAGE_USE:
            // Skeleton keys are class 0, so check the keyring family before the
            // consumable level gate. Keep a full stack of each usable key; keys
            // are consumed when used, so replenish below the stack target.
            if (SkeletonKey::IsKeyItem(proto))
            {
                uint32 const stackTarget = std::max<uint32>(1, proto->GetMaxStackSize());
                return bot->GetItemCount(proto->ItemId, false) < stackTarget ? 3 : 255;
            }

            // Do not spend reagents on food, drink, bandages or potions that
            // are far below the bot's level; they are not worth a craft slot.
            // Scroll item levels reflect rank, not the strength of the buff;
            // a low-level scroll can still be useful at max level.
            if (proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass != ITEM_SUBCLASS_SCROLL &&
                proto->ItemLevel + 15 < bot->GetLevel())
                return 255;
            return 3;
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
