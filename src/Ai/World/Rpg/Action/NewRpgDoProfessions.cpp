/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgDoProfessions.h"

#include <algorithm>
#include <list>
#include <unordered_map>
#include <vector>

#include "DBCStructure.h"
#include "GameObject.h"
#include "ItemUsageValue.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Playerbots.h"
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

// Game object entries are static data, so the focus -> entries lookup is built once.
std::vector<uint32> const& GameObjectEntriesForFocus(uint32 focusId)
{
    static std::unordered_map<uint32, std::vector<uint32>> const entriesByFocus = []
    {
        std::unordered_map<uint32, std::vector<uint32>> entries;
        for (auto const& [entry, goTemplate] : *sObjectMgr->GetGameObjectTemplates())
        {
            if (goTemplate.type == GAMEOBJECT_TYPE_SPELL_FOCUS && goTemplate.spellFocus.focusId)
                entries[goTemplate.spellFocus.focusId].push_back(entry);
        }
        return entries;
    }();

    static std::vector<uint32> const none;
    auto it = entriesByFocus.find(focusId);
    return it == entriesByFocus.end() ? none : it->second;
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

    if (bot->IsMounted())
    {
        bot->Dismount();
        return true;
    }

    uint8 const bagSpace = AI_VALUE(uint8, "bag space");
    uint32 const skillValue = bot->GetPureSkillValue(data.skillLine);

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
        if (chance <= 0)
            continue;

        bool hasAnyReagent = false;
        bool hasReagents = true;
        for (uint8 r = 0; r < MAX_SPELL_REAGENTS && hasReagents; ++r)
        {
            if (spellInfo->ReagentCount[r] == 0)
                continue;

            hasAnyReagent = true;
            hasReagents = bot->HasItemCount(spellInfo->Reagent[r], spellInfo->ReagentCount[r], false);
        }
        if (!hasAnyReagent || !hasReagents || !botAI->CanCastSpell(spellId, bot, true))
            continue;

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(outputItemId);
        if (!proto)
            continue;

        ItemUsage const usage = AI_VALUE2(ItemUsage, "item usage", outputItemId);
        bool const useful = usage == ITEM_USAGE_USE || usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE ||
                            usage == ITEM_USAGE_QUEST || usage == ITEM_USAGE_SKILL;
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
        GameObject* focus = nullptr;
        std::list<GameObject*> gameObjects;
        bot->GetGameObjectListWithEntryInGrid(gameObjects, GameObjectEntriesForFocus(chosen->spellFocus), 100.0f);
        for (GameObject* go : gameObjects)
        {
            if (go->isSpawned() && go->GetGOInfo()->type == GAMEOBJECT_TYPE_SPELL_FOCUS &&
                go->GetGOInfo()->spellFocus.focusId == chosen->spellFocus &&
                (!focus || bot->GetDistance(go) < bot->GetDistance(focus)))
                focus = go;
        }

        if (!focus)
        {
            LOG_DEBUG("playerbots", "[New RPG] {} no spell focus {} for recipe {}", bot->GetName(), chosen->spellFocus,
                      chosen->spellId);
            info.ChangeToIdle();
            return true;
        }

        float const focusDistance = float(focus->GetGOInfo()->spellFocus.dist) - 1.0f;
        if (!focus->IsWithinDistInMap(bot, focusDistance))
        {
            if (!bot->isMoving())
                MoveWorldObjectTo(focus->GetGUID(), 3.0f);
            return true;
        }
    }

    // A player stops, stands and dismounts to craft; consume the tick so follow/grind
    // cannot immediately start moving us again.
    if (bot->isMoving())
    {
        bot->StopMoving();
        return true;
    }
    if (!bot->IsStandState())
    {
        bot->SetStandState(UNIT_STAND_STATE_STAND);
        return true;
    }

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
