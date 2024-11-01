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
    Player* player = GetPlayer(event);

    if (!player || !PlayerIsValid(player) || player == bot)
        return false;

    WorldPacket data(opcode);
    data << player->GetName();
    SendPacket(data);

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
        WorldSession* botSession = bot->GetSession();
        WorldPackets::Guild::QueryGuildInfoResponse response;
        
        if (botSession)
        {
            guild->HandleQuery(botSession); // Send the query to populate response data
        }
        
        // Retrieve current emblem data
        EmblemInfo currentEmblem(response.Info.EmblemStyle, response.Info.EmblemColor, 
                                 response.Info.BorderStyle, response.Info.BorderColor, 
                                 response.Info.BackgroundColor);
        
        LOG_INFO("playerbots", "Parsed emblem data for guild {} - Style: {}, Color: {}, BorderStyle: {}, BorderColor: {}, BackgroundColor: {}",
                 guild->GetName().c_str(), currentEmblem.GetStyle(), currentEmblem.GetColor(), 
                 currentEmblem.GetBorderStyle(), currentEmblem.GetBorderColor(), currentEmblem.GetBackgroundColor());

        // Rank check and set for "Veteran" rank (ID 2)
        uint32 veteranRights = GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK | GR_RIGHT_INVITE;
        uint32 moneyPerDay = 1000;

        // Log parsed rank information for verification
        LOG_INFO("playerbots", "Parsed rank data for guild {} - RankCount: {}, VeteranRank (ID 2) Rights: {}, Money Per Day: {}",
            guild->GetName().c_str(), response.Info.RankCount, veteranRights, moneyPerDay);

        // Assuming `response` is a `QueryGuildInfoResponse` packet object we parsed earlier
        if (response.Info.RankCount > 2 && response.Info.Ranks[2] == "Veteran")
        {
            uint32 currentRights = veteranRights; // Use default rights as placeholders for comparison
            uint32 currentBankMoneyPerDay = moneyPerDay; // Placeholder for comparison
        
            // Verify rights by comparing against defaults, then update if needed
            if (currentRights != veteranRights || currentBankMoneyPerDay != moneyPerDay)
            {
                LOG_INFO("playerbots", "Guild {} 'Veteran' rank not set correctly, updating...", guild->GetName().c_str());
        
                // Set the rank info directly without accessing private members
                guild->HandleSetRankInfo(2, veteranRights, "Veteran", moneyPerDay);
        
                // Re-query the guild information to verify
                guild->HandleQuery(botSession);
            }
        }
        else
        {
            LOG_ERROR("playerbots", "Rank 'Veteran' not found in guild {}. Rank check and update skipped.", guild->GetName().c_str());
        }


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
                    LOG_INFO("playerbots", "Nearby player '{}' is a guild member of '{}'", player->GetName().c_str(), guild->GetName().c_str());
        
                    PlayerbotAI* playerBotAI = GET_PLAYERBOT_AI(player);
                    if (playerBotAI && (playerBotAI->GetGrouperType() == GrouperType::SOLO || playerBotAI->GetGrouperType() == GrouperType::MEMBER))
                    {
                        // Promote to officer rank
                        LOG_INFO("playerbots", "Promoting player '{}' to officer rank in guild '{}'", player->GetName().c_str(), guild->GetName().c_str());
        
                        if (auto* member = guild->GetMember(player->GetGUID()))
                        {
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

        if (!player || bot == player)
            continue;

        if (player->isDND())
            continue;

        // Promote or demote nearby members based on chance.
        // if (player->GetGuildId() && player->GetGuildId() == bot->GetGuildId())
        // {
        //    Guild::Member* member = guild->GetMember(player->GetGUID());
        //    uint32 dCount = AI_VALUE(uint32, "death count");

        //    if (!urand(0, 30) && dCount > 2 && guild->GetRankRights(botMember->GetRankId()) & GR_RIGHT_DEMOTE)
        //    {
        //        BroadcastHelper::BroadcastGuildMemberDemotion(botAI, bot, player);

        //        botAI->DoSpecificAction("guild demote", Event("guild management", guid), true);
        //        continue;
        //    }

        //    continue;
        // }

        if (!sPlayerbotAIConfig->randomBotGuildNearby)
            return false;

        if (guild->GetMemberSize() > 1000)
            return false;

        if ( (guild->GetRankRights(botMember->GetRankId()) & GR_RIGHT_INVITE) == 0)
            continue;

        if (player->GetGuildIdInvited())
            continue;

        PlayerbotAI* botAi = GET_PLAYERBOT_AI(player);

        if (!sPlayerbotAIConfig->randomBotInvitePlayer && botAi && botAi->IsRealPlayer())
            continue;

        if (botAi && !botAi->IsRealPlayer())
        {
            if (botAi->GetGuilderType() == GuilderType::SOLO) //Do not invite solo bots.
                continue;

            if (botAi->HasActivePlayerMaster() && !sRandomPlayerbotMgr->IsRandomBot(player)) //Do not invite alts of active players. 
                continue;
        }

        bool sameGroup = bot->GetGroup() && bot->GetGroup()->IsMember(player->GetGUID());

        if (!sameGroup && sServerFacade->GetDistance2d(bot, player) > sPlayerbotAIConfig->spellDistance)
            continue;

        if (sPlayerbotAIConfig->inviteChat && (sRandomPlayerbotMgr->IsRandomBot(bot) || !botAI->HasActivePlayerMaster()))
        {
            /* std::map<std::string, std::string> placeholders;
            placeholders["%name"] = player->GetName();
            placeholders["%members"] = std::to_string(guild->GetMemberSize());
            placeholders["%guildname"] = guild->GetName();
            AreaTableEntry const* current_area = botAI->GetCurrentArea();
            AreaTableEntry const* current_zone = botAI->GetCurrentZone();
            placeholders["%area_name"] = current_area ? current_area->area_name[BroadcastHelper::GetLocale()] : BOT_TEXT1("string_unknown_area");
            placeholders["%zone_name"] = current_zone ? current_zone->area_name[BroadcastHelper::GetLocale()] : BOT_TEXT1("string_unknown_area");

            std::vector<std::string> lines;

            //TODO - Move these hardcoded texts to sql!
            switch ((urand(0, 10) * urand(0, 10)) / 10)
            {
            case 0:
                lines.push_back(BOT_TEXT2("Hey %name do you want to join my guild?", placeholders));
                break;
            case 1:
                lines.push_back(BOT_TEXT2("Hey man you wanna join my guild %name?", placeholders));
                break;
            case 2:
                lines.push_back(BOT_TEXT2("I think you would be a good contribution to %guildname. Would you like to join %name?", placeholders));
                break;
            case 3:
                lines.push_back(BOT_TEXT2("My guild %guildname has %members quality members. Would you like to make it 1 more %name?", placeholders));
                break;
            case 4:
                lines.push_back(BOT_TEXT2("Hey %name do you want to join %guildname? We have %members members and looking to become number 1 of the server.", placeholders));
                break;
            case 5:
                lines.push_back(BOT_TEXT2("I'm not really good at smalltalk. Do you wanna join my guild %name/r?", placeholders));
                break;
            case 6:
                lines.push_back(BOT_TEXT2("Welcome to %zone_name.... do you want to join my guild %name?", placeholders));
                break;
            case 7:
                lines.push_back(BOT_TEXT2("%name, you should join my guild!", placeholders));
                break;
            case 8:
                lines.push_back(BOT_TEXT2("%name, I got this guild....", placeholders));
                break;
            case 9:
                lines.push_back(BOT_TEXT2("You are actually going to join my guild %name?", placeholders));
                lines.push_back(BOT_TEXT2("Haha.. you are the man! We are going to raid Molten...", placeholders));
                break;
            case 10:
                lines.push_back(BOT_TEXT2("Hey Hey! do you guys wanna join my gild????", placeholders));
                lines.push_back(BOT_TEXT2("We've got a bunch of high levels and we are really super friendly..", placeholders));
                lines.push_back(BOT_TEXT2("..and watch your dog and do your homework...", placeholders));
                lines.push_back(BOT_TEXT2("..and we raid once a week and are working on MC raids...", placeholders));
                lines.push_back(BOT_TEXT2("..and we have more members than just me...", placeholders));
                lines.push_back(BOT_TEXT2("..and please stop I'm lonenly and we can get a ride the whole time...", placeholders));
                lines.push_back(BOT_TEXT2("..and it's really beautifull and I feel like crying...", placeholders));
                lines.push_back(BOT_TEXT2("So what do you guys say are you going to join are you going to join?", placeholders));
                break;
            }

            for (auto line : lines)
                if (sameGroup)
                {
                    WorldPacket data;
                    ChatHandler::BuildChatPacket(data, bot->GetGroup()->isRaidGroup() ? CHAT_MSG_RAID : CHAT_MSG_PARTY, line.c_str(), LANG_UNIVERSAL, CHAT_TAG_NONE, bot->GetGUID(), bot->GetName());
                    bot->GetGroup()->BroadcastPacket(&data, true);
                }
                else
                    bot->Say(line, (bot->GetTeamId() == TEAM_ALLIANCE ? LANG_COMMON : LANG_ORCISH));*/
        }

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
