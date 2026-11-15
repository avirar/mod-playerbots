/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgDoProfessions.h"

#include <algorithm>
#include <vector>

#include "DBCStructure.h"
#include "GameObject.h"
#include "ItemUsageValue.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Playerbots.h"
#include "SpellFocusMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace
{
struct ProfessionRecipe
{
    uint32 spellId = 0;
    int32 chance = 0;
    uint32 tier = 0;
    uint32 spellFocus = 0;
    bool useful = false;
};

constexpr uint32 BASIC_CAMPFIRE_SPELL = 818;
constexpr uint32 COOKING_FIRE_FOCUS = 4;
constexpr uint32 CAMPFIRE_WAIT_MS = 10 * IN_MILLISECONDS;

bool TryBasicCampfire(PlayerbotAI* botAI, Player* bot, NewRpgInfo::DoProfessions& data, uint32 focusId)
{
    if (focusId != COOKING_FIRE_FOCUS || !bot->HasSpell(BASIC_CAMPFIRE_SPELL) ||
        bot->HasSpellCooldown(BASIC_CAMPFIRE_SPELL))
        return false;

    if (bot->isMoving())
        bot->StopMoving();
    if (!bot->IsStandState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    data.lastCampfire = getMSTime();
    LOG_DEBUG("playerbots", "[New RPG] {} professions casting basic campfire", bot->GetName());
    if (botAI->CastSpell(BASIC_CAMPFIRE_SPELL, bot))
        return true;

    data.lastCampfire = 0;
    return false;
}
}  // namespace

bool NewRpgDoProfessionsAction::Execute(Event /*event*/)
{
    NewRpgInfo& info = botAI->rpgInfo;
    LOG_DEBUG("playerbots", "[New RPG] {} do professions execute (status {})", bot->GetName(),
              uint32(info.GetStatus()));
    auto* dataPtr = std::get_if<NewRpgInfo::DoProfessions>(&info.data);
    if (!dataPtr)
        return false;
    auto& data = *dataPtr;

    if (!bot->IsAlive() || bot->IsInCombat())
        return false;

    if (!data.skillLine || !botAI->HasSkill(SkillType(data.skillLine)))
    {
        info.ChangeToIdle();
        return true;
    }

    if (data.skillLine != SKILL_FIRST_AID && data.skillLine != SKILL_COOKING)
    {
        info.ChangeToIdle();
        return true;
    }

    // Let the current cast finish before selecting the next recipe.
    if (bot->HasUnitState(UNIT_STATE_CASTING) || bot->IsNonMeleeSpellCast(false))
        return true;

    if (bot->IsInFlight())
    {
        LOG_DEBUG("playerbots", "[New RPG] {} professions waiting: in flight", bot->GetName());
        return true;
    }
    if (bot->IsMounted())
        bot->Dismount();

    uint8 const bagSpace = AI_VALUE(uint8, "bag space");
    uint32 const skillValue = bot->GetPureSkillValue(data.skillLine);
    bool const skillMaxed = skillValue >= bot->GetMaxSkillValue(data.skillLine);

    std::vector<ProfessionRecipe> usefulRecipes;
    std::vector<ProfessionRecipe> skillUpRecipes;

    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            continue;

        uint32 outputItemId = 0;
        for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
        {
            if (spellInfo->Effects[e].Effect == SPELL_EFFECT_CREATE_ITEM)
            {
                outputItemId = spellInfo->Effects[e].ItemType;
                break;
            }
        }
        if (!outputItemId)
            continue;

        int32 chance = 0;
        uint32 tier = 0;
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineAbilityEntry const* skill = itr->second;
            if (skill->SkillLine != data.skillLine)
                continue;

            chance = std::max(chance, ItemUsageValue::CraftSkillGainChance(skillValue, skill->TrivialSkillLineRankHigh,
                                                                           skill->TrivialSkillLineRankLow));
            tier = std::max(tier, skill->TrivialSkillLineRankLow);
        }
        bool hasAnyReagent = false;
        bool hasReagents = true;
        for (uint8 r = 0; r < MAX_SPELL_REAGENTS && hasReagents; ++r)
        {
            if (spellInfo->ReagentCount[r] == 0)
                continue;

            hasAnyReagent = true;
            hasReagents = bot->HasItemCount(spellInfo->Reagent[r], spellInfo->ReagentCount[r], false);
        }
        // CanCastSpell rejects every recipe while the bot is walking to the spell focus,
        // so only gate on the recipe itself here; CastSpell validates the actual cast.
        if (!hasAnyReagent || !hasReagents || bot->HasSpellCooldown(spellId))
            continue;

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(outputItemId);
        if (!proto)
            continue;

        ItemUsage const usage = AI_VALUE2(ItemUsage, "item usage", outputItemId);
        bool const useful = usage == ITEM_USAGE_USE || usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE ||
                            usage == ITEM_USAGE_QUEST || usage == ITEM_USAGE_SKILL;

        // At max skill there is no skill-up to gain; useful outputs are still
        // crafted up to their item-usage limit.
        if (!useful && (chance <= 0 || skillMaxed))
            continue;

        (useful ? usefulRecipes : skillUpRecipes)
            .push_back({spellId, chance, tier, spellInfo->RequiresSpellFocus, useful});
    }

    auto pickBest = [](std::vector<ProfessionRecipe>& recipes) -> ProfessionRecipe*
    {
        ProfessionRecipe* best = nullptr;
        for (ProfessionRecipe& recipe : recipes)
        {
            if (!best || recipe.chance > best->chance || (recipe.chance == best->chance && recipe.tier > best->tier))
                best = &recipe;
        }
        return best;
    };

    ProfessionRecipe* chosen = pickBest(usefulRecipes);
    if (!chosen)
        chosen = pickBest(skillUpRecipes);

    if (!chosen)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} no professions recipe with skill-up chance and materials (skill {})",
                  bot->GetName(), data.skillLine);
        info.ChangeToIdle();
        return true;
    }

    // Skill-up surplus respects the bag-space rule; actually useful outputs may go up to
    // their item-usage limit even when the bags are already fairly full.
    if (!chosen->useful && bagSpace > 80)
    {
        LOG_DEBUG("playerbots", "[New RPG] {} stopping professions: bags {}% and recipe {} is surplus", bot->GetName(),
                  bagSpace, chosen->spellId);
        info.ChangeToIdle();
        return true;
    }
    if (chosen->useful && bagSpace >= 100)
    {
        info.ChangeToIdle();
        return true;
    }

    if (chosen->spellFocus)
    {
        Value<ObjectGuid>* focusValue = context->GetValue<ObjectGuid>("spell focus", chosen->spellFocus);
        if (!focusValue)
        {
            LOG_ERROR("playerbots", "[New RPG] {} missing 'spell focus' value", bot->GetName());
            info.ChangeToIdle();
            return true;
        }

        ObjectGuid const focusGuid = focusValue->Get();
        WorldObject* worldObject = focusGuid.IsEmpty() ? nullptr : botAI->GetWorldObject(focusGuid);
        GameObject* focus = worldObject ? worldObject->ToGameObject() : nullptr;
        SpellFocusSpawn const* cached = (!focus && !focusGuid.IsEmpty()) ? sSpellFocusMgr.Get(focusGuid) : nullptr;

        if (!focus && !cached)
        {
            if (TryBasicCampfire(botAI, bot, data, chosen->spellFocus))
                return true;
            if (data.lastCampfire && GetMSTimeDiffToNow(data.lastCampfire) < CAMPFIRE_WAIT_MS)
                return true;

            LOG_DEBUG("playerbots", "[New RPG] {} no spell focus {} for recipe {}", bot->GetName(), chosen->spellFocus,
                      chosen->spellId);
            info.ChangeToIdle();
            return true;
        }

        float distance = 0.0f;
        float focusDistance = 0.0f;
        if (focus)
        {
            distance = bot->GetDistance(focus);
            focusDistance = float(focus->GetGOInfo()->spellFocus.dist) - 1.0f;
        }
        else
        {
            float const dx = cached->pos.GetPositionX() - bot->GetPositionX();
            float const dy = cached->pos.GetPositionY() - bot->GetPositionY();
            float const dz = cached->pos.GetPositionZ() - bot->GetPositionZ();
            distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            focusDistance = cached->distance - 1.0f;
        }

        if (!focus || distance > focusDistance)
        {
            // A campfire is better than walking more than 100 yards to an existing focus.
            if (distance > 100.0f && TryBasicCampfire(botAI, bot, data, chosen->spellFocus))
                return true;
            if (data.lastCampfire && GetMSTimeDiffToNow(data.lastCampfire) < CAMPFIRE_WAIT_MS)
                return true;

            LOG_DEBUG("playerbots", "[New RPG] {} professions moving to spell focus {} (distance {})", bot->GetName(),
                      focusGuid.ToString(), distance);
            context->GetValue<ObjectGuid>("spell focus target")->Set(focusGuid);
            botAI->DoSpecificAction("move to spell focus", Event("spell focus"), true);
            return true;
        }
    }

    // A player stops and stands to craft; do both in the same tick so the sparse
    // random-bot schedule is not spent waiting for the next one.
    if (bot->isMoving())
        bot->StopMoving();
    if (!bot->IsStandState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    data.spellId = chosen->spellId;
    data.lastCast = getMSTime();
    ++data.casts;
    LOG_DEBUG("playerbots", "[New RPG] {} professions skill {} recipe {} chance {} useful {} tier {}", bot->GetName(),
              data.skillLine, chosen->spellId, chosen->chance, chosen->useful, chosen->tier);

    if (botAI->CastSpell(chosen->spellId, bot))
        return true;

    LOG_DEBUG("playerbots", "[New RPG] {} professions cast failed for recipe {} (skill {})", bot->GetName(),
              chosen->spellId, data.skillLine);
    botAI->SetNextCheckDelay(1000);
    return false;
}
