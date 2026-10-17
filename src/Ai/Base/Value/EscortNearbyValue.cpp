/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "EscortNearbyValue.h"
#include "NewRpgBaseAction.h"
#include "NewRpgInfo.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

namespace
{
Creature* ActiveEscortee(PlayerbotAI* botAI, Player* bot)
{
    if (!botAI || !bot || !sPlayerbotAIConfig.QuestEscorts)
        return nullptr;

    auto const* data = std::get_if<NewRpgInfo::DoQuest>(&botAI->rpgInfo.data);
    if (!data || !data->questId || bot->GetQuestStatus(data->questId) != QUEST_STATUS_INCOMPLETE)
        return nullptr;

    return NewRpgBaseAction::FindEscorteeForQuest(bot, data->questId, 100.0f);
}
}  // namespace

bool EscortNearbyValue::Calculate() { return ActiveEscortee(botAI, bot) != nullptr; }

bool EscortAttackedValue::Calculate()
{
    Creature* escortee = ActiveEscortee(botAI, bot);
    return escortee && !escortee->getAttackers().empty();
}
