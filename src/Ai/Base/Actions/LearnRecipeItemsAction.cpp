/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "LearnRecipeItemsAction.h"

#include "Bag.h"
#include "ChatHelper.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "SpellMgr.h"

namespace
{
constexpr uint32 SPELL_LEARNING_ITEM_1 = 483;
constexpr uint32 SPELL_LEARNING_ITEM_2 = 55884;

uint32 RecipeItemSpell(ItemTemplate const* proto)
{
    if (!proto || proto->Class != ITEM_CLASS_RECIPE)
        return 0;

    if (proto->Spells[0].SpellId == SPELL_LEARNING_ITEM_1 || proto->Spells[0].SpellId == SPELL_LEARNING_ITEM_2)
        return proto->Spells[1].SpellId;

    // A book whose on-use spell learns the rank directly (e.g. Master First Aid - Doctor in the House).
    if (SpellInfo const* itemSpell = sSpellMgr->GetSpellInfo(proto->Spells[0].SpellId))
        for (SpellEffectInfo const& effect : itemSpell->Effects)
            if (effect.Effect == SPELL_EFFECT_LEARN_SPELL && effect.TriggerSpell)
                return effect.TriggerSpell;

    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        if (proto->Spells[i].SpellId && proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_LEARN_SPELL_ID)
            return proto->Spells[i].SpellId;

    return 0;
}

bool MasterCanLearnRecipe(PlayerbotAI* botAI, Player* bot, Item* item, uint32 recipeSpellId)
{
    Player* master = botAI->GetMaster();
    if (!master || master == bot)
        return false;

    Group* group = bot->GetGroup();
    if (!group || group != master->GetGroup())
        return false;

    ItemTemplate const* proto = item->GetTemplate();
    // A soulbound copy cannot be handed to the master.
    if (proto->Bonding == BIND_WHEN_PICKED_UP && !item->IsBOPTradable())
        return false;

    return !master->HasSpell(recipeSpellId) && master->CanUseItem(proto) == EQUIP_ERR_OK;
}
}  // namespace

bool LearnRecipeItemsAction::Execute(Event event)
{
    // A pushed item that is not a recipe cannot be learned; skip the bag scan.
    if (event.GetSource() == "item push result")
    {
        WorldPacket& packet = event.getPacket();
        if (packet.size() >= 29)
        {
            packet.rpos(25);
            ItemTemplate const* pushed = sObjectMgr->GetItemTemplate(packet.read<uint32>());
            if (!pushed || pushed->Class != ITEM_CLASS_RECIPE)
                return false;
        }
    }

    std::vector<Item*> items;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            items.push_back(item);
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bot->GetItemByPos(bagSlot, slot))
                    items.push_back(item);

    for (Item* item : items)
    {
        ItemTemplate const* proto = item->GetTemplate();
        uint32 const recipeSpellId = RecipeItemSpell(proto);
        if (!recipeSpellId)
            continue;

        // Leave a recipe the grouped master can learn in the bags and tell the
        // master once; they can ask the bot to use it or trade for it.
        if (MasterCanLearnRecipe(botAI, bot, item, recipeSpellId))
        {
            if (!botAI->announcedRecipeSpells.contains(recipeSpellId))
            {
                std::string const message = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "rpg_recipe_for_master", "I have a recipe you can learn: %item",
                    {{"%item", botAI->GetChatHelper()->FormatItem(proto)}});
                if (botAI->TellMaster(message))
                    botAI->announcedRecipeSpells.insert(recipeSpellId);
            }
            continue;
        }

        if (bot->HasSpell(recipeSpellId) || bot->CanUseItem(proto) != EQUIP_ERR_OK)
            continue;

        bool hasProfession = false;
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(recipeSpellId);
        for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            if (itr->second->SkillLine && botAI->HasSkill(SkillType(itr->second->SkillLine)))
            {
                hasProfession = true;
                break;
            }
        }
        if (!hasProfession)
            continue;

        if (bot->isMoving())
            bot->StopMoving();
        if (!bot->IsStandState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);

        LOG_DEBUG("playerbots", "[recipe items] {} learning recipe {} from item {}", bot->GetName(), recipeSpellId,
                  proto->ItemId);
        if (UseItemAuto(item))
            return true;
    }

    return false;
}
