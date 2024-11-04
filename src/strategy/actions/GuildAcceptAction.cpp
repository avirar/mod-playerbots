/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "GuildAcceptAction.h"

#include "Event.h"
#include "GuildPackets.h"
#include "PlayerbotSecurity.h"
#include "Playerbots.h"

bool GuildAcceptAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());
    p.rpos(0);  // Reset read position to the beginning of the packet

    Player* inviter = nullptr;
    std::string invitedName;
    p >> invitedName;

    // Log the inviter name parsed from the packet
    LOG_INFO("playerbots", "Guild invitation packet received. Inviter name: '{}'", invitedName);

    // Normalize and retrieve the player object for the inviter
    if (normalizePlayerName(invitedName))
        inviter = ObjectAccessor::FindPlayerByName(invitedName.c_str());

    // Log if inviter is found or not
    if (!inviter)
    {
        LOG_ERROR("playerbots", "Inviter '{}' not found. Unable to process guild invitation.", invitedName);
        return false;
    }

    // Initial guild and bot state checks
    bool accept = true;
    uint32 guildId = inviter->GetGuildId();
    if (!guildId)
    {
        LOG_ERROR("playerbots", "Inviter '{}' is not in a guild. Cannot accept invitation.", inviter->GetName().c_str());
        botAI->TellError("You are not in a guild!");
        accept = false;
    }
    else if (bot->GetGuildId())
    {
        LOG_ERROR("playerbots", "Bot '{}' is already in a guild. Declining invitation.", bot->GetName().c_str());
        botAI->TellError("Sorry, I am in a guild already");
        accept = false;
    }
    else if (!GET_PLAYERBOT_AI(inviter) && !botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, inviter, true))
    {
        LOG_INFO("playerbots", "Bot '{}' does not have the required security level to join inviter '{}' guild.", bot->GetName().c_str(), inviter->GetName().c_str());
        botAI->TellError("Sorry, I don't want to join your guild :(");
        accept = false;
    }

    // Decide to accept or decline based on checks and log the decision
    if (accept)
    {
        LOG_INFO("playerbots", "Bot '{}' accepting guild invite from '{}', Guild ID: {}", bot->GetName().c_str(), inviter->GetName().c_str(), guildId);
        WorldPackets::Guild::AcceptGuildInvite data = WorldPacket(CMSG_GUILD_ACCEPT);
        bot->GetSession()->HandleGuildAcceptOpcode(data);
    }
    else
    {
        LOG_INFO("playerbots", "Bot '{}' declining guild invite from '{}'.", bot->GetName().c_str(), inviter->GetName().c_str());
        WorldPackets::Guild::GuildDeclineInvitation data = WorldPacket(CMSG_GUILD_DECLINE);
        bot->GetSession()->HandleGuildDeclineOpcode(data);
    }

    return true;
}
