#include "AcceptInvitationAction.h"

#include "Chat.h"
#include "Event.h"
#include "ObjectAccessor.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotSecurity.h"
#include "Playerbots.h"
#include "WorldPacket.h"

bool AcceptInvitationAction::Execute(Event event)
{
    Group* grp = bot->GetGroupInvite();
    if (!grp && !bot->GetGroup())
        return false;
    WorldPacket packet = event.getPacket();
    uint8 flag;
    std::string name;
    packet >> flag >> name;

    LOG_INFO("playerbots", "Bot {} received an invitation with flag: {}, from inviter: {}.", bot->GetName().c_str(), flag, name.c_str());

    // Player* inviter = ObjectAccessor::FindPlayer(grp->GetLeaderGUID());
    Player* inviter = ObjectAccessor::FindPlayerByName(name, true);
    if (!inviter)
        return false;

    LOG_INFO("playerbots", "Bot {} found inviter: {} with GUID: {}.", bot->GetName().c_str(), inviter->GetName().c_str(), inviter->GetGUID().ToString().c_str());
    
    // Check if the bot is already in a group based on the flag and inviter's security level
    if (flag == 0 && botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, inviter))
    {
        // Build a packet to leave the group
        WorldPacket leavePacket;
        leavePacket << uint32(PARTY_OP_LEAVE) << bot->GetName() << uint32(0); // operation, bot name, and a filler value

        bot->GetSession()->HandleGroupDisbandOpcode(leavePacket);

        // Log the action instead of messaging the inviter
        LOG_INFO("playerbots", "Bot {} left its current group on invitation from {}. Request inviter to re-invite.",
                 bot->GetName().c_str(), inviter->GetName().c_str());

        return false; // Do not proceed with accepting invite immediately
    }

    // Check inviter security level (if not in a group)
    if (!botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, inviter))
    {
        WorldPacket data(SMSG_GROUP_DECLINE, 10);
        data << bot->GetName();
        inviter->SendDirectMessage(&data);
        bot->UninviteFromGroup();
        return false;
    }

    // Accept the invitation
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
