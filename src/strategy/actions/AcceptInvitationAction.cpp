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
    Group* inviteGroup = bot->GetGroupInvite();
    if (!inviteGroup)
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

    // Handle ERR_ALREADY_IN_GROUP_S if bot is grouped
    if (currentGroup)
    {
        // Check if the current group leader is a bot
        Player* currentLeader = ObjectAccessor::FindPlayer(currentGroup->GetLeaderGUID());
        if (currentLeader && GET_PLAYERBOT_AI(currentLeader) && inviterBotAI)
        {
            LOG_INFO("playerbots", "Bot {} declined invite from {} because it is already in a group led by a bot.", bot->GetName().c_str(), inviter->GetName().c_str());
            WorldPacket data(SMSG_GROUP_DECLINE, 10);
            data << bot->GetName();
            inviter->SendDirectMessage(&data);
            return false;
        }

        // If inviter is a high-ranking guild member, leave current group and join them
        if (Guild* botGuild = bot->GetGuild())
        {
            Guild::Member const* inviterGuildMember = botGuild->GetMember(inviter->GetGUID());
            if (inviterGuildMember && inviterGuildMember->GetRankId() <= 1) 
            {
                bot->Whisper("I am currently in a group but will leave to join you.", LANG_UNIVERSAL, inviter);
                botAI->DoSpecificAction("leave group", Event(), true);
            }
        }
    }

    Group* inviterGroup = inviter->GetGroup();

    // Case: Inviter is ungrouped - accept the invite and create a new group
    if (!inviterGroup)
    {
        WorldPacket p;
        uint32 roles_mask = 0;
        p << roles_mask;
        bot->GetSession()->HandleGroupAcceptOpcode(p);  // This will create a new group with inviter as the leader

        if (sRandomPlayerbotMgr->IsRandomBot(bot))
            botAI->SetMaster(inviter);

        botAI->ResetStrategies();
        botAI->ChangeStrategy("+follow,-lfg,-bg", BOT_STATE_NON_COMBAT);
        botAI->TellMaster("Hello");

        if (sPlayerbotAIConfig->summonWhenGroup && bot->GetDistance(inviter) > sPlayerbotAIConfig->sightDistance)
        {
            Teleport(inviter, bot);
        }
        return true;
    }

    // Case: Inviter is in a group
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

    return false;
}

