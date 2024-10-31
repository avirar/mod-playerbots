#include "AcceptInvitationAction.h"
#include "Event.h"
#include "ObjectAccessor.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotSecurity.h"
#include "Playerbots.h"
#include "WorldPacket.h"
#include "Guild.h"

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

    Group* currentGroup = bot->GetGroup();
    PlayerbotAI* inviterBotAI = GET_PLAYERBOT_AI(inviter);

    // Handle case where inviter is a bot
    if (inviterBotAI)
    {
        if (currentGroup)
        {
            Player* currentLeader = ObjectAccessor::FindPlayer(currentGroup->GetLeaderGUID());
            if (currentLeader && GET_PLAYERBOT_AI(currentLeader))
            {
                LOG_INFO("playerbots", "Bot {} declined invite from {} because it is already in a group led by a bot.", bot->GetName().c_str(), inviter->GetName().c_str());
                WorldPacket data(SMSG_GROUP_DECLINE, 10);
                data << bot->GetName();
                inviter->SendDirectMessage(&data);
                return false;
            }
        }

        if (!sPlayerbotAIConfig->randomBotGroupNearby)
        {
            WorldPacket data(SMSG_GROUP_DECLINE, 10);
            data << bot->GetName();
            inviter->SendDirectMessage(&data);
            return false;
        }
    }
    else  // Handle case where inviter is a real player
    {
        if (Guild* botGuild = bot->GetGuild())
        {
            Guild::Member const* inviterGuildMember = botGuild->GetMember(inviter->GetGUID());

            if (inviterGuildMember && inviterGuildMember->GetRankId() <= 1)
            {
                if (currentGroup)
                {
                    // Use bot->Whisper to send a message directly to the inviter
                    bot->Whisper("I am currently in a group but will leave to join you.", LANG_UNIVERSAL, inviter);
                    botAI->DoSpecificAction("leave", Event(), true);
                }

                Group* inviterGroup = inviter->GetGroup();
                if (inviterGroup && !inviterGroup->IsFull())
                {
                    inviterGroup->AddMember(bot);
                    inviterGroup->BroadcastGroupUpdate();

                    botAI->SetMaster(inviter);
                    botAI->TellMaster("I have joined your group as requested.");

                    botAI->ResetStrategies();
                    botAI->ChangeStrategy("+follow,-lfg,-bg", BOT_STATE_NON_COMBAT);
                    botAI->Reset();

                    if (sPlayerbotAIConfig->summonWhenGroup && bot->GetDistance(inviter) > sPlayerbotAIConfig->sightDistance)
                    {
                        Teleport(inviter, bot);
                    }
                    return true;
                }
                else if (inviterGroup && inviterGroup->IsFull())
                {
                    bot->Whisper("I could not join your group as it is full.", LANG_UNIVERSAL, inviter);
                    return false;
                }
                else
                {
                    bot->Whisper("You need to be in a group for me to join.", LANG_UNIVERSAL, inviter);
                    return false;
                }
            }
        }

        if (!botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, inviter))
        {
            WorldPacket data(SMSG_GROUP_DECLINE, 10);
            data << bot->GetName();
            inviter->SendDirectMessage(&data);
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
