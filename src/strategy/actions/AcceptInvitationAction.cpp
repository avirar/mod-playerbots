#include "AcceptInvitationAction.h"

#include "Chat.h"
#include "Event.h"
#include "GroupMgr.h"
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

        // Wait for a short delay if needed before re-checking the inviter's group
        inviter->UpdateObjectVisibility();

        Group* inviterGroup = inviter->GetGroup();
        if (!inviterGroup) {
            // If inviter is not in a group, create a new one and add both players
            inviterGroup = new Group();
            if (inviterGroup->Create(inviter)) {
                inviterGroup->AddMember(bot);
                sGroupMgr->AddGroup(inviterGroup);
                LOG_INFO("playerbots", "Bot {} joined a new group with inviter {}.", bot->GetName().c_str(), inviter->GetName().c_str());
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
            } 
            else
            {
                LOG_ERROR("playerbots", "Failed to create group for bot {} and inviter {}.", bot->GetName().c_str(), inviter->GetName().c_str());
                delete inviterGroup;
                return false;
            }
        } else if (!inviterGroup->IsFull()) 
        {
            // If inviter already has a group, add the bot to it
            inviterGroup->AddMember(bot);
            inviterGroup->BroadcastGroupUpdate();
            LOG_INFO("playerbots", "Bot {} joined inviter {}'s existing group.", bot->GetName().c_str(), inviter->GetName().c_str());
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
        } 
        else 
        {
            LOG_INFO("playerbots", "Bot {} could not join inviter {}'s group because it is full.", bot->GetName().c_str(), inviter->GetName().c_str());
            return false;
        }
        
        return true;
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
