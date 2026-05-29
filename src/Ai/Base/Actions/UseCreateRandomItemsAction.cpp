#include "UseCreateRandomItemsAction.h"
#include "PlayerbotAI.h"
#include "ItemTemplate.h"
#include "WorldPacket.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SharedDefines.h"
#include "ItemVisitors.h"

class FindCreateRandomItemVisitor : public FindItemVisitor
{
public:
    FindCreateRandomItemVisitor(UseCreateRandomItemsAction* action) : FindItemVisitor(), action(action) {}

protected:
    bool Accept(ItemTemplate const* itemTemplate) override
    {
        // Check item properties
        if (itemTemplate->Class != ITEM_CLASS_MISC)
            return false;

        if (itemTemplate->SubClass != 11) // Junk subclass
            return false;

        if (!(itemTemplate->Flags & ITEM_FLAG_PLAYERCAST))
            return false;

        // Check if it has a spell with USE trigger
        if (itemTemplate->Spells[0].SpellId == 0)
            return false;

        if (itemTemplate->Spells[0].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
            return false;

        // Verify the spell is valid and has no conditions
        return action->IsValidCreateRandomItemSpell(itemTemplate->Spells[0].SpellId);
    }

private:
    UseCreateRandomItemsAction* action;
};

bool UseCreateRandomItemsAction::Execute(Event event)
{
    Item* item = FindCreateRandomItem();
    if (!item)
        return false;

    uint8 bagIndex = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    uint32 spellId = item->GetTemplate()->Spells[0].SpellId;

    // Verify we can cast the spell
    if (!botAI->CanCastSpell(spellId, bot, false, nullptr, item))
        return false;

    // Build and send the USE_ITEM packet
    WorldPacket packet(CMSG_USE_ITEM);
    uint8 castCount = 1;
    ObjectGuid itemGuid = item->GetGUID();
    uint32 glyphIndex = 0;
    uint8 castFlags = 0;

    packet << bagIndex << slot << castCount << spellId << itemGuid << glyphIndex << castFlags;

    // Add target flag - self target for these simple items
    uint32 targetFlag = TARGET_FLAG_NONE;
    packet << targetFlag;
    packet << bot->GetPackGUID();

    bot->GetSession()->HandleUseItemOpcode(packet);

    std::ostringstream out;
    out << "Using " << item->GetTemplate()->Name1;
    botAI->TellMasterNoFacing(out.str());

    return true;
}

Item* UseCreateRandomItemsAction::FindCreateRandomItem()
{
    FindCreateRandomItemVisitor visitor(this);
    IterateItems(&visitor, ITERATE_ITEMS_IN_BAGS);
    std::vector<Item*> items = visitor.GetResult();
    return items.empty() ? nullptr : *items.begin();
}

bool UseCreateRandomItemsAction::IsValidCreateRandomItemSpell(uint32 spellId)
{
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return false;

    // Check that effect 0 is SPELL_EFFECT_CREATE_RANDOM_ITEM
    if (spellInfo->Effects[0].Effect != SPELL_EFFECT_CREATE_RANDOM_ITEM)
        return false;

    // Verify no conditions on the spell
    if (spellInfo->Targets != 0)
        return false;

    if (spellInfo->TargetCreatureType != 0)
        return false;

    if (spellInfo->RequiresSpellFocus != 0)
        return false;

    if (spellInfo->CasterAuraState != 0)
        return false;

    if (spellInfo->TargetAuraState != 0)
        return false;

    if (spellInfo->MaxLevel != 0)
        return false;

    if (spellInfo->BaseLevel != 0)
        return false;

    if (spellInfo->SpellLevel != 0)
        return false;

    return true;
}
