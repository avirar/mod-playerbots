/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "AcceptInvitationAction.h"

#include "Event.h"
#include "ObjectAccessor.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotSecurity.h"
#include "Playerbots.h"
#include "WorldPacket.h"

bool AcceptInvitationAction::Execute(Event event)
{
    Group* grp = bot->GetGroupInvite();
    if (!grp)
        return false;

    WorldPacket packet = event.getPacket();
    uint8 flag;
    std::string name;
    packet >> flag >> name;

    Player* inviter = ObjectAccessor::FindPlayerByName(name, true);
    if (!inviter)
        return false;

    // Check if the inviter is a bot
    PlayerbotAI* inviterBotAI = GET_PLAYERBOT_AI(inviter);

    if (inviterBotAI)  // Inviter is a bot
    {
        // Check if the bot is already in a group with a bot leader
        Group* currentGroup = bot->GetGroup();
        if (currentGroup)
        {
            Player* currentLeader = ObjectAccessor::FindPlayer(currentGroup->GetLeaderGUID());
            if (currentLeader && GET_PLAYERBOT_AI(currentLeader))
            {
                LOG_INFO("playerbots", "Bot {} declined invite from {} because it is already in a group led by a bot.", bot->GetName().c_str(), inviter->GetName().c_str());

                // Decline the invitation
                WorldPacket data(SMSG_GROUP_DECLINE, 10);
                data << bot->GetName();
                inviter->SendDirectMessage(&data);
                bot->UninviteFromGroup();
                return false;
            }
        }

        // Check the configuration setting for bot grouping
        if (!sPlayerbotAIConfig->randomBotGroupNearby)
        {
            // Decline the invitation as bot grouping is not allowed
            WorldPacket data(SMSG_GROUP_DECLINE, 10);
            data << bot->GetName();
            inviter->SendDirectMessage(&data);
            bot->UninviteFromGroup();
            return false;
        }
    }
    else  // Inviter is a real player
    {
        if (!botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, inviter))
        {
            // Decline the invitation due to insufficient security level
            WorldPacket data(SMSG_GROUP_DECLINE, 10);
            data << bot->GetName();
            inviter->SendDirectMessage(&data);
            bot->UninviteFromGroup();
            return false;
        }
    }

    WorldPacket p;
    uint32 roles_mask = 0;
    p << roles_mask;
    bot->GetSession()->HandleGroupAcceptOpcode(p);

    if (sRandomPlayerbotMgr->IsRandomBot(bot))
        botAI->SetMaster(inviter);

    botAI->ResetStrategies();
    botAI->ChangeStrategy("+follow,-lfg,-bg", BOT_STATE_NON_COMBAT);
    botAI->Reset();

    botAI->TellMaster("Hello");

    if (sPlayerbotAIConfig->summonWhenGroup && bot->GetDistance(inviter) > sPlayerbotAIConfig->sightDistance)
    {
        Teleport(inviter, bot);
    }
    return true;
}
