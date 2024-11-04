/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "LeaveGroupAction.h"

#include "Chat.h"
#include "Event.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"

bool LeaveGroupAction::Execute(Event event)
{
    Player* master = event.getOwner();
    return Leave(master);
}

#include "Chat.h"  // Make sure to include Chat.h for ChatHandler

bool PartyCommandAction::Execute(Event event)
{
    WorldPacket& p = event.getPacket();
    p.rpos(0);
    uint32 operation, result;
    std::string member;

    p >> operation >> member >> result;

    // Log the packet details
    LOG_INFO("playerbots", "Bot {} received packet with operation: {}, member: {}, result: {}",
             bot->GetName().c_str(), operation, member.c_str(), result);


    // Handle already in group case
    if (result == ERR_ALREADY_IN_GROUP_S)
    {
        Player* inviter = ObjectAccessor::FindPlayerByName(member);  // Locate the inviter by name
    
        // Check if inviter exists
        if (!inviter)
        {
            // Log if inviter could not be found
            LOG_INFO("playerbots", "Bot {} received an invite from '{}', but no player with that name was found in the world.",
                     bot->GetName().c_str(), member.c_str());
        }
        else
        {
            // Log inviter's details if found
            LOG_INFO("playerbots", "Bot {} found inviter: {} with GUID: {}.", bot->GetName().c_str(), inviter->GetName().c_str(), inviter->GetGUID().ToString().c_str());
    
            // Check if inviter has an active session
            if (!inviter->GetSession())
            {
                // Log if inviter exists but has no active session
                LOG_INFO("playerbots", "Bot {} received an invite from '{}', but the inviter has no active session.", bot->GetName().c_str(), inviter->GetName().c_str());
            }
            else
            {
                // Log if inviter exists and has an active session but bot is already in a group
                LOG_INFO("playerbots", "Bot {} is already in a group. Invite by {} (GUID: {}).",
                         bot->GetName().c_str(), inviter->GetName().c_str(), inviter->GetGUID().ToString().c_str());
                return true;
            }
        }
    }
    else
    {
        LOG_INFO("playerbots", "Bot {} received invite packet that was not ERR_ALREADY_IN_GROUP_S", bot->GetName().c_str());
    }

    // Original leave group functionality
    if (operation == PARTY_OP_LEAVE)
    {
        Player* master = GetMaster();
        if (master && member == master->GetName())
            return Leave(bot);
        
        botAI->Reset();
    }

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
    // Check if the bot is in any LFG activity, battleground, or battleground queue
    if (bot->isUsingLfg())
    {
        Group* group = bot->GetGroup();
        if (group && group->isLFGGroup() && GroupHasRealPlayer(group))
        {
            return false;
        }
    }
    
    if (bot->inRandomLfgDungeon())
    {
        Group* group = bot->GetGroup();
        if (group && group->isLFGGroup() && GroupHasRealPlayer(group))
        {
            return false;
        }
    }
    
    if (bot->InBattleground())
    {
        return false;
    }
    
    if (bot->InBattlegroundQueue())
    {
        return false;
    }
    
    // Check if the bot's group is an LFG group and has a real player; if so, prevent leaving
    Group* group = bot->GetGroup();
    if (group && group->isLFGGroup() && GroupHasRealPlayer(group))
    {
        return false;
    }

    if (!group)
    {
        return false;
    }

    Player* master = botAI->GetGroupMaster();
    Player* trueMaster = botAI->GetMaster();

    if (!GET_PLAYERBOT_AI(master))
    {
        return false;
    }

    if (!master || (bot == master && (botAI->GetGrouperType() == GrouperType::SOLO || botAI->GetGrouperType() == GrouperType::MEMBER) && !botAI->IsRealPlayer()))
    {
        LOG_INFO("playerbots", "Bot {} leaves group because there is no group master, or it is the master with an unsuitable GrouperType (SOLO or MEMBER).", bot->GetName().c_str());
        return true;
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

// Helper function to check for a real player in the group without a new method
bool LeaveGroupAction::GroupHasRealPlayer(Group* group)
{
    if (!group)
        return false;

    for (GroupReference const* ref = group->GetFirstMember(); ref != nullptr; ref = ref->next())
    {
        Player* player = ref->GetSource();
        if (player && player->IsVisible() && !GET_PLAYERBOT_AI(player)) // No PlayerbotAI means a real player
        {
            return true; // Found a real player in the group
        }
    }
    return false; // No real players in the group
}
