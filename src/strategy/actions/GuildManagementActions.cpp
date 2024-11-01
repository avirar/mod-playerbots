/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "GuildManagementActions.h"

#include "GuildMgr.h"
#include "GuildPackets.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "BroadcastHelper.h"

Player* GuidManageAction::GetPlayer(Event event)
{
    Player* player = nullptr;
    ObjectGuid guid = event.getObject();

    if (guid)
    {
        player = ObjectAccessor::FindPlayer(guid);

        if (player)
            return player;
    }

    std::string text = event.getParam();

    if (!text.empty())
    {
        if (normalizePlayerName(text))
        {
            player = ObjectAccessor::FindPlayerByName(text.c_str());

            if (player)
                return player;
        }

        return nullptr;
    }

    Player* master = GetMaster();
    if (!master)
        guid = bot->GetTarget();
    else
        guid = master->GetTarget();

    player = ObjectAccessor::FindPlayer(guid);

    if (player)
        return player;

    player = event.getOwner();

    if (player)
        return player;

    return nullptr;
}

bool GuidManageAction::Execute(Event event)
{
    // Attempt to retrieve the player based on the event
    Player* player = GetPlayer(event);

    // Log the result of player retrieval
    if (!player)
    {
        LOG_DEBUG("playerbots", "GuidManageAction::Execute - No player found for the given event.");
        return false;
    }

    // Check if the player is valid for the action and not the bot itself
    if (!PlayerIsValid(player))
    {
        LOG_DEBUG("playerbots", "GuidManageAction::Execute - Player '{}' is not valid for this action.", player->GetName().c_str());
        return false;
    }
    if (player == bot)
    {
        LOG_DEBUG("playerbots", "GuidManageAction::Execute - Skipping action for bot '{}'.", bot->GetName().c_str());
        return false;
    }

    // Log that the player passed validation checks and the packet is being prepared
    LOG_INFO("playerbots", "Executing action on player '{}'", player->GetName().c_str());

    // Prepare and send the packet with the player's name
    WorldPacket data(opcode);
    data << player->GetName();
    SendPacket(data);

    // Confirm the packet was sent successfully
    LOG_INFO("playerbots", "Packet sent successfully to player '{}'", player->GetName().c_str());

    return true;
}


bool GuidManageAction::PlayerIsValid(Player* member) { return !member->GetGuildId(); }

uint8 GuidManageAction::GetRankId(Player* member)
{
    return sGuildMgr->GetGuildById(member->GetGuildId())->GetMember(member->GetGUID())->GetRankId();
}

bool GuildInviteAction::isUseful()
{
    return bot->GetGuildId() && sGuildMgr->GetGuildById(bot->GetGuildId())->HasRankRight(bot, GR_RIGHT_INVITE);
}

void GuildInviteAction::SendPacket(WorldPacket packet)
{
    LOG_INFO("playerbots", "Bot {} is sending a guild invitation.", bot->GetName().c_str());
    WorldPackets::Guild::GuildInviteByName data = WorldPacket(packet);
    bot->GetSession()->HandleGuildInviteOpcode(data);
}

bool GuildInviteAction::PlayerIsValid(Player* member) { return !member->GetGuildId(); }

bool GuildPromoteAction::isUseful()
{
    return bot->GetGuildId() && sGuildMgr->GetGuildById(bot->GetGuildId())->HasRankRight(bot, GR_RIGHT_PROMOTE);
}

void GuildPromoteAction::SendPacket(WorldPacket packet)
{
    WorldPackets::Guild::GuildPromoteMember data = WorldPacket(packet);
    bot->GetSession()->HandleGuildPromoteOpcode(data);
}

bool GuildPromoteAction::PlayerIsValid(Player* member)
{
    return member->GetGuildId() == bot->GetGuildId() && GetRankId(bot) < GetRankId(member) - 1;
}

bool GuildDemoteAction::isUseful()
{
    return bot->GetGuildId() && sGuildMgr->GetGuildById(bot->GetGuildId())->HasRankRight(bot, GR_RIGHT_DEMOTE);
}

void GuildDemoteAction::SendPacket(WorldPacket packet)
{
    WorldPackets::Guild::GuildDemoteMember data = WorldPacket(packet);
    bot->GetSession()->HandleGuildDemoteOpcode(data);
}

bool GuildDemoteAction::PlayerIsValid(Player* member)
{
    return member->GetGuildId() == bot->GetGuildId() && GetRankId(bot) < GetRankId(member);
}

bool GuildRemoveAction::isUseful()
{
    return bot->GetGuildId() && sGuildMgr->GetGuildById(bot->GetGuildId())->HasRankRight(bot, GR_RIGHT_REMOVE);
}

void GuildRemoveAction::SendPacket(WorldPacket packet)
{
    WorldPackets::Guild::GuildOfficerRemoveMember data = WorldPacket(packet);
    bot->GetSession()->HandleGuildRemoveOpcode(data);
}

bool GuildRemoveAction::PlayerIsValid(Player* member)
{
    return member->GetGuildId() == bot->GetGuildId() && GetRankId(bot) < GetRankId(member);
};

bool GuildManageNearbyAction::Execute(Event event)
{
    uint32 found = 0;

    Guild* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
    Guild::Member* botMember = guild->GetMember(bot->GetGUID());

    GuidVector nearGuids = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest friendly players")->Get();
    uint8 botRankId = botMember->GetRankId();

    // Ensure only the guild leader can perform this action
    if (botRankId == 0) 
    {
        uint32 officerCount = 0;
        uint32 totalMembers = guild->GetMemberSize();  // Total number of members in the guild
        
        LOG_INFO("playerbots", "Checking current officer count for guild '{}'. Total members: {}", guild->GetName().c_str(), totalMembers);
        
        for (uint32 guidCounter = 0; guidCounter < totalMembers; ++guidCounter)
        {
            // Retrieve the member using the GUID counter
            if (auto* member = guild->GetMember(ObjectGuid::Create<HighGuid::Player>(guidCounter)))
            {
                if (member->GetRankId() == 1)  // Check if the member is an officer
                {
                    officerCount++;
                    LOG_INFO("playerbots", "Member '{}' is an officer (RankId: 1). Current officer count: {}", member->GetName().c_str(), officerCount);
                }
            }
        }
        
        LOG_INFO("playerbots", "Total officer count in guild '{}' is {}. Minimum required officers: 2", guild->GetName().c_str(), officerCount);
        
        if (officerCount < 2)  // If less than two officers, promote candidates
        {
            LOG_INFO("playerbots", "Less than two officers found in guild '{}'. Attempting to promote nearby candidates.", guild->GetName().c_str());
        
            for (const auto& guid : nearGuids)
            {
                Player* player = ObjectAccessor::FindPlayer(guid);
                if (!player || bot == player)
                {
                    if (!player)
                        LOG_DEBUG("playerbots", "Player with GUID '{}' is not found or not online.", guid.GetRawValue());
                    if (bot == player)
                        LOG_DEBUG("playerbots", "Skipping self-promotion for bot '{}'", bot->GetName().c_str());
                    continue;
                }
        
                if (player->GetGuildId() == bot->GetGuildId())
                {
                    LOG_INFO("playerbots", "Nearby player '{}' is also a guild member of '{}'", player->GetName().c_str(), guild->GetName().c_str());
        
                    PlayerbotAI* playerBotAI = GET_PLAYERBOT_AI(player);
                    if (playerBotAI && (playerBotAI->GetGrouperType() == GrouperType::SOLO || playerBotAI->GetGrouperType() == GrouperType::MEMBER))
                    {
                        if (auto* member = guild->GetMember(player->GetGUID()))
                        {
                            // Check if the player is already an officer
                            if (member->GetRankId() == 1)  // Rank ID 1 is for Officer
                            {
                                LOG_INFO("playerbots", "Player '{}' is already an officer in guild '{}'. No promotion needed.", player->GetName().c_str(), guild->GetName().c_str());
                                continue;  // Skip to the next player
                            }
                            // Promote to officer rank
                            LOG_INFO("playerbots", "Promoting player '{}' to officer rank in guild '{}'", player->GetName().c_str(), guild->GetName().c_str());
                                member->ChangeRank(1);  // Use ChangeRank to set rank to officer (RankId: 1)

                            officerCount++;
                            LOG_INFO("playerbots", "New officer count after promotion: {}", officerCount);
                        }
        
                        if (officerCount >= 2)
                        {
                            LOG_INFO("playerbots", "Minimum required officer count reached for guild '{}'. Promotion process completed.", guild->GetName().c_str());
                            break;
                        }
                    }
                    else
                    {
                        LOG_DEBUG("playerbots", "Player '{}' does not meet promotion criteria or is already grouped.", player->GetName().c_str());
                    }
                }
                else
                {
                    LOG_DEBUG("playerbots", "Nearby player '{}' is not a member of the bot's guild '{}'. Skipping.", player->GetName().c_str(), guild->GetName().c_str());
                }
            }
        }
        else
        {
            LOG_INFO("playerbots", "Guild '{}' already has the required number of officers ({}). No promotion needed.", guild->GetName().c_str(), officerCount);
        }


    }
    for (auto& guid : nearGuids)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
    
        // Skip if player not found or is the bot itself
        if (!player || bot == player)
        {
            LOG_DEBUG("playerbots", "Skipping self or non-existent player (GUID: {}).", guid.GetRawValue());
            continue;
        }
    
        // Skip if player is in Do Not Disturb (DND) mode
        if (player->isDND())
        {
            LOG_DEBUG("playerbots", "Skipping player '{}' - DND mode active.", player->GetName().c_str());
            continue;
        }
    
        // Config and guild size checks
        if (!sPlayerbotAIConfig->randomBotGuildNearby)
        {
            LOG_DEBUG("playerbots", "Skipping nearby guild invite - 'randomBotGuildNearby' disabled in config.");
            return false;
        }
    
        if (guild->GetMemberSize() > 1000)
        {
            LOG_DEBUG("playerbots", "Guild '{}' has over 1000 members; skipping invite checks.", guild->GetName().c_str());
            return false;
        }
    
        // Check for invite permissions
        if ((guild->GetRankRights(botMember->GetRankId()) & GR_RIGHT_INVITE) == 0)
        {
            LOG_DEBUG("playerbots", "Bot lacks invite permissions; skipping player '{}'.", player->GetName().c_str());
            continue;
        }
    
        // Skip players with pending invitations
        if (player->GetGuildIdInvited())
        {
            LOG_DEBUG("playerbots", "Player '{}' already has a pending guild invitation.", player->GetName().c_str());
            continue;
        }

        if (player->GetGuildId())
        {
            LOG_DEBUG("playerbots", "Player '{}' already has a guild.", player->GetName().c_str());
            continue;
        }
    
        // Check for specific bot invite conditions
        PlayerbotAI* botAi = GET_PLAYERBOT_AI(player);
        if (!sPlayerbotAIConfig->randomBotInvitePlayer && botAi && botAi->IsRealPlayer())
        {
            LOG_DEBUG("playerbots", "Skipping player '{}' - Real player invites disabled in config.", player->GetName().c_str());
            continue;
        }
    
        if (botAi && !botAi->IsRealPlayer())
        {
            if (botAi->GetGuilderType() == GuilderType::SOLO)
            {
                LOG_DEBUG("playerbots", "Skipping bot '{}' - Solo bots are not eligible for guilds.", player->GetName().c_str());
                continue;
            }
    
            if (botAi->HasActivePlayerMaster() && !sRandomPlayerbotMgr->IsRandomBot(player))
            {
                LOG_DEBUG("playerbots", "Skipping bot '{}' - Controlled by active player.", player->GetName().c_str());
                continue;
            }
        }
    
        // Distance check
        bool sameGroup = bot->GetGroup() && bot->GetGroup()->IsMember(player->GetGUID());
        if (!sameGroup && sServerFacade->GetDistance2d(bot, player) > sPlayerbotAIConfig->spellDistance)
        {
            LOG_DEBUG("playerbots", "Player '{}' is out of invite range.", player->GetName().c_str());
            continue;
        }

        LOG_INFO("playerbots", "Inviting player '{}' to guild '{}'.", player->GetName().c_str(), guild->GetName().c_str());
        // Attempt guild invite if all conditions are met
        if (botAI->DoSpecificAction("guild invite", Event("guild management", guid), true))
        {
            if (sPlayerbotAIConfig->inviteChat)
                return true;
            found++;
        }
    }

    return found > 0;
}

bool GuildManageNearbyAction::isUseful()
{
    if (!bot->GetGuildId())
        return false;

    Guild* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
    Guild::Member* botMember = guild->GetMember(bot->GetGUID());

    return guild->GetRankRights(botMember->GetRankId()) & (GR_RIGHT_DEMOTE | GR_RIGHT_PROMOTE | GR_RIGHT_INVITE);
}

bool GuildLeaveAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (owner && !botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, owner, true))
    {
        botAI->TellError("Sorry, I am happy in my guild :)");
        return false;
    }

    WorldPackets::Guild::GuildLeave data = WorldPacket(CMSG_GUILD_LEAVE);
    bot->GetSession()->HandleGuildLeaveOpcode(data);
    return true;
}

bool GuildLeaveAction::isUseful() { return bot->GetGuildId(); }
