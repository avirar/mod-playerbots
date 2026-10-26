/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BattleGroundTactics.h"
#include "Chat.h"
#include "ChatCommandTags.h"
#include "GuildTaskMgr.h"
#include "MapMgr.h"
#include "Nav3DManager.h"
#include "PerfMonitor.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"

using namespace Acore::ChatCommands;

class playerbots_commandscript : public CommandScript
{
public:
    playerbots_commandscript() : CommandScript("playerbots_commandscript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable playerbotsDebugCommandTable = {
            {"bg", HandleDebugBGCommand, SEC_GAMEMASTER, Console::Yes},
        };

        static ChatCommandTable playerbotsAccountCommandTable = {
            {"setKey", HandleSetSecurityKeyCommand, SEC_PLAYER, Console::No},
            {"link", HandleLinkAccountCommand, SEC_PLAYER, Console::No},
            {"linkedAccounts", HandleViewLinkedAccountsCommand, SEC_PLAYER, Console::No},
            {"unlink", HandleUnlinkAccountCommand, SEC_PLAYER, Console::No},
        };

        static ChatCommandTable playerbotsCommandTable = {
            {"bot", HandlePlayerbotCommand, SEC_PLAYER, Console::No},
            {"exec", HandleExecCommand, SEC_GAMEMASTER, Console::Yes},
            {"inspect", HandleInspectCommand, SEC_GAMEMASTER, Console::Yes},
            {"gtask", HandleGuildTaskCommand, SEC_GAMEMASTER, Console::Yes},
            {"nav3d", HandleNav3DCommand, SEC_GAMEMASTER, Console::Yes},
            {"nav3dmap", HandleNav3DMapCommand, SEC_GAMEMASTER, Console::Yes},
            {"nav3dteleport", HandleNav3DTeleportCommand, SEC_GAMEMASTER, Console::Yes},
            {"pmon", HandlePerfMonCommand, SEC_GAMEMASTER, Console::Yes},
            {"rndbot", HandleRandomPlayerbotCommand, SEC_GAMEMASTER, Console::Yes},
            {"debug", playerbotsDebugCommandTable},
            {"account", playerbotsAccountCommandTable},
        };

        static ChatCommandTable commandTable = {
            {"playerbots", playerbotsCommandTable},
        };

        return commandTable;
    }

    static bool HandlePlayerbotCommand(ChatHandler* handler, char const* args)
    {
        return PlayerbotMgr::HandlePlayerbotMgrCommand(handler, args);
    }

    // Sends its own error so the core does not append its generic help text.
    static PlayerbotAI* GetOnlineBotAI(ChatHandler* handler, PlayerIdentifier const& target)
    {
        Player* player = target.GetConnectedPlayer();
        PlayerbotAI* botAI = player ? PlayerbotsMgr::instance().GetPlayerbotAI(player) : nullptr;
        if (!botAI)
            handler->SendErrorMessage("{} is not an online bot", target.GetName());
        return botAI;
    }

    // From the console or SOAP there is no player object to send the command, so the bot sends it to itself.
    // The bot's security check always lets a bot command itself.
    static bool HandleExecCommand(ChatHandler* handler, PlayerIdentifier target, Tail command)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        if (command.empty())
        {
            handler->SendErrorMessage("Usage: .playerbots exec <bot> <command> (state-changing commands; "
                                      "query replies go to the bot's master - use inspect)");
            return false;
        }

        Player* from = handler->GetSession() ? handler->GetSession()->GetPlayer() : botAI->GetBot();
        botAI->HandleCommand(CHAT_MSG_WHISPER, std::string(command), from);
        handler->PSendSysMessage("{}: '{}' sent", target.GetName(), std::string(command));
        return true;
    }

    static bool HandleInspectCommand(ChatHandler* handler, PlayerIdentifier target, Tail command)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        if (command.empty())
        {
            handler->SendErrorMessage("Usage: .playerbots inspect <bot> <query> (state, position, tpos, "
                                      "movement, target, hp, strategy, action, values, travel, budget)");
            return false;
        }

        std::string const result = botAI->HandleRemoteCommand(std::string(command));
        handler->PSendSysMessage("{}: {}", target.GetName(), result.empty() ? "<empty>" : result);
        return true;
    }

    static bool HandleNav3DCommand(ChatHandler* handler, PlayerIdentifier target, float x, float y, float z)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        Player* bot = botAI->GetBot();
        if (!bot->GetMap())
        {
            handler->SendErrorMessage("{} has no map", target.GetName());
            return false;
        }

        Position const start(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        Position const goal(x, y, z);
        Movement::PointsArray path;
        Nav3DResult result;
        bool const found =
            Nav3DManager::Instance().FindPath(bot->GetMap(), bot->GetPhaseMask(), start, goal, path, result);

        handler->PSendSysMessage(
            "nav3d {}: found={} direct={} partial={} reversed={} nodes={} expanded={} ms={} len={:.1f} "
            "best={:.1f} bestPos=({:.2f},{:.2f},{:.2f}) points={}",
            target.GetName(), found ? 1 : 0, result.Direct ? 1 : 0, result.Partial ? 1 : 0,
            result.Reversed ? 1 : 0, result.Nodes, result.Expanded, result.ElapsedMs, result.Length,
            result.BestDistance, result.BestX, result.BestY, result.BestZ, path.size());
        LOG_INFO("playerbots",
                 "nav3d {} start=({:.2f},{:.2f},{:.2f}) goal=({:.2f},{:.2f},{:.2f}) found={} direct={} partial={} "
                 "reversed={} nodes={} expanded={} ms={} len={:.1f} best={:.1f} "
                 "bestPos=({:.2f},{:.2f},{:.2f}) points={}",
                 target.GetName(), start.GetPositionX(), start.GetPositionY(), start.GetPositionZ(), x, y, z,
                 found ? 1 : 0, result.Direct ? 1 : 0, result.Partial ? 1 : 0, result.Reversed ? 1 : 0,
                 result.Nodes, result.Expanded, result.ElapsedMs, result.Length, result.BestDistance, result.BestX,
                 result.BestY, result.BestZ, path.size());
        for (size_t i = 0; i < path.size(); ++i)
            LOG_INFO("playerbots", "nav3d {} point[{}] = ({:.2f},{:.2f},{:.2f})", target.GetName(), i, path[i].x,
                     path[i].y, path[i].z);
        return true;
    }

    static bool HandleNav3DMapCommand(ChatHandler* handler, uint32 mapId, float sx, float sy, float sz, float gx,
                                      float gy, float gz)
    {
        Map* map = sMapMgr->FindMap(mapId, 0);
        if (!map)
        {
            handler->SendErrorMessage("Map {} is not loaded", mapId);
            return false;
        }

        Position const start(sx, sy, sz);
        Position const goal(gx, gy, gz);
        Movement::PointsArray path;
        Nav3DResult result;
        bool const found = Nav3DManager::Instance().FindPath(map, PHASEMASK_NORMAL, start, goal, path, result);

        handler->PSendSysMessage(
            "nav3dmap {}: found={} direct={} partial={} reversed={} nodes={} expanded={} ms={} len={:.1f} "
            "best={:.1f} bestPos=({:.2f},{:.2f},{:.2f}) points={}",
            mapId, found ? 1 : 0, result.Direct ? 1 : 0, result.Partial ? 1 : 0, result.Reversed ? 1 : 0,
            result.Nodes, result.Expanded, result.ElapsedMs, result.Length, result.BestDistance, result.BestX,
            result.BestY, result.BestZ, path.size());
        LOG_INFO("playerbots",
                 "nav3dmap {} start=({:.2f},{:.2f},{:.2f}) goal=({:.2f},{:.2f},{:.2f}) found={} direct={} partial={} "
                 "reversed={} nodes={} expanded={} ms={} len={:.1f} best={:.1f} "
                 "bestPos=({:.2f},{:.2f},{:.2f}) points={}",
                 mapId, sx, sy, sz, gx, gy, gz, found ? 1 : 0, result.Direct ? 1 : 0, result.Partial ? 1 : 0,
                 result.Reversed ? 1 : 0, result.Nodes, result.Expanded, result.ElapsedMs, result.Length,
                 result.BestDistance, result.BestX, result.BestY, result.BestZ, path.size());
        for (size_t i = 0; i < path.size(); ++i)
            LOG_INFO("playerbots", "nav3dmap {} point[{}] = ({:.2f},{:.2f},{:.2f})", mapId, i, path[i].x, path[i].y,
                     path[i].z);
        return true;
    }

    static bool HandleNav3DTeleportCommand(ChatHandler* handler, PlayerIdentifier target, uint32 mapId, float x,
                                           float y, float z)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        Player* bot = botAI->GetBot();
        if (!bot->TeleportTo(mapId, x, y, z, bot->GetOrientation()))
        {
            handler->SendErrorMessage("nav3dteleport {} failed", target.GetName());
            return false;
        }

        handler->PSendSysMessage("nav3dteleport {} -> map {} ({:.2f},{:.2f},{:.2f})", target.GetName(), mapId, x, y,
                                 z);
        LOG_INFO("playerbots", "nav3dteleport {} -> map {} ({:.2f},{:.2f},{:.2f})", target.GetName(), mapId, x, y,
                 z);
        return true;
    }

    static bool HandleRandomPlayerbotCommand(ChatHandler* handler, char const* args)
    {
        return RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(handler, args);
    }

    static bool HandleGuildTaskCommand(ChatHandler* handler, char const* args)
    {
        return GuildTaskMgr::HandleConsoleCommand(handler, args);
    }

    static bool HandlePerfMonCommand(ChatHandler* /*handler*/, char const* args)
    {
        if (!strcmp(args, "reset"))
        {
            sPerfMonitor.Reset();
            return true;
        }

        if (!strcmp(args, "tick"))
        {
            sPerfMonitor.PrintStats(true, false);
            sPerfMonitor.DumpJson(true);
            return true;
        }

        if (!strcmp(args, "stack"))
        {
            sPerfMonitor.PrintStats(false, true);
            sPerfMonitor.DumpJson(false);
            return true;
        }

        if (!strcmp(args, "toggle"))
        {
            sPlayerbotAIConfig.PerfMonEnabled = !sPlayerbotAIConfig.PerfMonEnabled;
            if (sPlayerbotAIConfig.PerfMonEnabled)
                LOG_INFO("playerbots", "Performance monitor enabled");
            else
                LOG_INFO("playerbots", "Performance monitor disabled");
            return true;
        }

        sPerfMonitor.PrintStats();
        sPerfMonitor.DumpJson(false);
        return true;
    }

    static bool HandleDebugBGCommand(ChatHandler* handler, char const* args)
    {
        return BGTactics::HandleConsoleCommand(handler, args);
    }

    static bool HandleSetSecurityKeyCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
        {
            handler->PSendSysMessage("Usage: .playerbots account setKey <securityKey>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();
        std::string key = args;

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleSetSecurityKeyCommand(player, key);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleLinkAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
            return false;

        char* accountName = strtok((char*)args, " ");
        char* key = strtok(nullptr, " ");

        if (!accountName || !key)
        {
            handler->PSendSysMessage("Usage: .playerbots account link <accountName> <securityKey>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleLinkAccountCommand(player, accountName, key);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleViewLinkedAccountsCommand(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleViewLinkedAccountsCommand(player);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleUnlinkAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
            return false;

        char* accountName = strtok((char*)args, " ");
        if (!accountName)
        {
            handler->PSendSysMessage("Usage: .playerbots account unlink <accountName>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleUnlinkAccountCommand(player, accountName);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }
};

void AddPlayerbotsCommandscripts() { new playerbots_commandscript(); }
