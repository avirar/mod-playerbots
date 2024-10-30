/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "LeaveGroupAction.h"

#include "Event.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

bool LeaveGroupAction::Execute(Event event)
{
    Player* master = event.getOwner();
    return Leave(master);
}

bool PartyCommandAction::Execute(Event event)
{
    WorldPacket& p = event.getPacket();
    p.rpos(0);
    uint32 operation;
    std::string member;

    p >> operation >> member;

    if (operation != PARTY_OP_LEAVE)
        return false;

    Player* master = GetMaster();
    if (master && member == master->GetName())
        return Leave(bot);
	
	botAI->Reset();

    return false;
}

bool UninviteAction::Execute(Event event)
{
    WorldPacket& p = event.getPacket();
    if (p.GetOpcode() == CMSG_GROUP_UNINVITE)
    {
        p.rpos(0);
        std::string membername;
        p >> membername;

        // player not found
        if (!normalizePlayerName(membername))
        {
            return false;
        }

        if (bot->GetName() == membername)
            return Leave(bot);
    }

    if (p.GetOpcode() == CMSG_GROUP_UNINVITE_GUID)
    {
        p.rpos(0);
        ObjectGuid guid;
        p >> guid;

        if (bot->GetGUID() == guid)
            return Leave(bot);
    }
	
	botAI->Reset();

    return false;
}

bool LeaveGroupAction::Leave(Player* player)
{
    if (player && !GET_PLAYERBOT_AI(player) && !botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, player))
    {
        LOG_INFO("playerbots", "Bot {} does not leave group due to insufficient security level from inviter.", bot->GetName().c_str());
        return false;
    }

    bool aiMaster = GET_PLAYERBOT_AI(botAI->GetMaster()) != nullptr;
    botAI->TellMaster("Goodbye!", PLAYERBOT_SECURITY_TALK);

    bool randomBot = sRandomPlayerbotMgr->IsRandomBot(bot);
    bool shouldStay = randomBot && bot->GetGroup() && player == bot;

    if (!shouldStay)
    {
        LOG_INFO("playerbots", "Bot {} leaves group because it's either not a random bot or lacks group conditions to stay.", bot->GetName().c_str());

        WorldPacket p;
        p << uint32(PARTY_OP_LEAVE) << bot->GetName() << uint32(0);
        bot->GetSession()->HandleGroupDisbandOpcode(p);
    }

    if (randomBot)
    {
        LOG_INFO("playerbots", "Bot {} removes master assignment due to random bot status.", bot->GetName().c_str());
        GET_PLAYERBOT_AI(bot)->SetMaster(nullptr);
    }

    if (!aiMaster)
        botAI->ResetStrategies(!randomBot);

    botAI->Reset();

    return true;
}


bool LeaveFarAwayAction::Execute(Event event)
{
    // allow bot to leave party when they want
    return Leave(botAI->GetGroupMaster());
}

bool LeaveFarAwayAction::isUseful()
{
    if (bot->InBattleground())
    {
        LOG_INFO("playerbots", "Bot {} stays in group because it's in a battleground.", bot->GetName().c_str());
        return false;
    }

    if (bot->InBattlegroundQueue())
    {
        LOG_INFO("playerbots", "Bot {} stays in group because it's in a battleground queue.", bot->GetName().c_str());
        return false;
    }

    if (!bot->GetGroup())
    {
        return false;
    }

    Player* master = botAI->GetGroupMaster();
    Player* trueMaster = botAI->GetMaster();

    if (!master || (bot == master && (botAI->GetGrouperType() == GrouperType::SOLO || botAI->GetGrouperType() == GrouperType::MEMBER) && !botAI->IsRealPlayer()))
    {
        LOG_INFO("playerbots", "Bot {} leaves group because there is no group master, or it is the master with an unsuitable GrouperType (SOLO or MEMBER).", bot->GetName().c_str());
        return false;
    }

    if (botAI->GetGrouperType() == GrouperType::SOLO)
    {
        LOG_INFO("playerbots", "Bot {} leaves group because it has GrouperType SOLO.", bot->GetName().c_str());
        return true;
    }

    uint32 dCount = AI_VALUE(uint32, "death count");
    if (dCount > 9)
    {
        LOG_INFO("playerbots", "Bot {} leaves group due to high death count ({} > 9).", bot->GetName().c_str(), dCount);
        return true;
    }
    if (dCount > 4 && !botAI->HasRealPlayerMaster())
    {
        LOG_INFO("playerbots", "Bot {} leaves group due to moderate death count ({} > 4) without a real player master.", bot->GetName().c_str(), dCount);
        return true;
    }

    if (bot->GetGuildId() == master->GetGuildId() && bot->GetLevel() > master->GetLevel() + 5)
    {
        if (AI_VALUE(bool, "should get money"))
        {
            LOG_INFO("playerbots", "Bot {} stays in group due to guild and money-sharing condition.", bot->GetName().c_str());
            return false;
        }
    }

    if (abs(int32(master->GetLevel() - bot->GetLevel())) > 4)
    {
        LOG_INFO("playerbots", "Bot {} leaves group because of large level difference with master ({} levels).", bot->GetName().c_str(), abs(int32(master->GetLevel() - bot->GetLevel())));
        return true;
    }

    if (bot->GetMapId() != master->GetMapId() || bot->GetDistance2d(master) >= 2 * sPlayerbotAIConfig->rpgDistance)
    {
        LOG_INFO("playerbots", "Bot {} leaves group due to distance: map mismatch or distance exceeds {} units.", bot->GetName().c_str(), 2 * sPlayerbotAIConfig->rpgDistance);
        return true;
    }

    botAI->Reset();

    return false;
}

