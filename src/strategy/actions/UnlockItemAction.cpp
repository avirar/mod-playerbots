#include "UnlockItemAction.h"
#include "OpenItemAction.h"
#include "ChatHelper.h"
// #include "ItemTemplate.h"
#include "ItemUsageValue.h"
//#include "PlayerbotAI.h"
#include "Playerbots.h"

bool UnlockItemAction::Execute(Event event)
{
    std::vector<Item*> items =
        AI_VALUE2(std::vector<Item*>, "inventory items", "usage " + std::to_string(ITEM_USAGE_UNLOCK));
    // std::reverse(items.begin(), items.end());

    for (auto& item : items)
    {
        if (CastCustomSpellAction::Execute(
                Event("unlock items", "1804 " + chat->FormatQItem(item->GetEntry()))))
        {
            // Now call the OpenItem action.
            OpenItemAction openItemAction(botAI);
            openItemAction.OpenItem(item);

            return true;
        }
    }

    return false;
}

bool UnlockItemAction::isUseful()
{
    return botAI->HasSkill(SKILL_LOCKPICKING) && !bot->IsInCombat() &&
           AI_VALUE2(uint32, "item count", "usage " + std::to_string(ITEM_USAGE_UNLOCK)) > 0;
}
