/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include <algorithm>

#include <fmt/core.h>

#include "BattleGroundTactics.h"
#include "Chat.h"
#include "ChatCommandTags.h"
#include "GuildTaskMgr.h"
#include "NewRpgBaseAction.h"
#include "NewRpgHelpers.h"
#include "PerfMonitor.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "Random.h"
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
            {"rpg", HandleDebugRpgCommand, SEC_GAMEMASTER, Console::Yes},
            {"tp", HandleDebugTeleportCommand, SEC_GAMEMASTER, Console::Yes},
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
            handler->SendErrorMessage(
                "Usage: .playerbots exec <bot> <command> (state-changing commands; "
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
            handler->SendErrorMessage(
                "Usage: .playerbots inspect <bot> <query> (state, position, tpos, "
                "movement, target, hp, strategy, action, values, travel, budget, rpg, professions, spells, inventory)");
            return false;
        }

        // Console commands run on the world thread; waiting for the bot's map thread
        // there stalls world updates. Print the snapshot when the map thread produces it.
        if (!handler->GetSession() && PlayerbotAI::IsMapThreadQuery(std::string(command)))
        {
            std::string const botName = target.GetName();
            botAI->HandleRemoteCommandAsync(std::string(command), [botName](std::string result)
                                            {
                                                fmt::print("{}: {}\n", botName,
                                                           result.empty() ? "<empty>" : result);
                                            });
            return true;
        }

        std::string const result = botAI->HandleRemoteCommand(std::string(command));
        handler->PSendSysMessage("{}: {}", target.GetName(), result.empty() ? "<empty>" : result);
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

    static bool HandleDebugTeleportCommand(ChatHandler* handler, PlayerIdentifier target, float x, float y, float z,
                                           Optional<uint32> mapId)
    {
        Player* bot = target.GetConnectedPlayer();
        if (!bot)
        {
            handler->SendErrorMessage("{} is not online.", target.GetName());
            return false;
        }

        if (!GET_PLAYERBOT_AI(bot))
        {
            handler->SendErrorMessage("{} is not a bot.", target.GetName());
            return false;
        }

        if (handler->HasLowerSecurity(bot))
            return false;

        uint32 const map = mapId ? *mapId : bot->GetMapId();
        if (!bot->TeleportTo(map, x, y, z, bot->GetOrientation()))
        {
            handler->SendErrorMessage("Failed to teleport {} to {} {} {} (map {}).", target.GetName(), x, y, z, map);
            return false;
        }

        handler->PSendSysMessage("{} teleported to {} {} {} (map {}).", target.GetName(), x, y, z, map);
        return true;
    }

    static std::string ProfessionErrorText(NewRpgHelpers::ProfessionSelection const& selection)
    {
        switch (selection.error)
        {
            case NewRpgHelpers::ProfessionParseError::Unknown:
                return "Unknown profession: " + selection.detail;
            case NewRpgHelpers::ProfessionParseError::NotLearned:
                return "Not learned: " + selection.detail;
            case NewRpgHelpers::ProfessionParseError::Gathering:
                return selection.detail + " is a gathering profession; use 'do gather'";
            case NewRpgHelpers::ProfessionParseError::NoProfessions:
                return "No craft profession to work on";
            case NewRpgHelpers::ProfessionParseError::None:
            default:
                return "Invalid profession selection";
        }
    }

    static bool HandleDebugRpgCommand(ChatHandler* handler, PlayerIdentifier target, Tail command)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        std::string const text(command);
        if (!botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT))
            botAI->ChangeStrategy("+new rpg", BOT_STATE_NON_COMBAT);

        if (text == "idle")
        {
            botAI->rpgInfo.ChangeToIdle();
            handler->PSendSysMessage("{}: rpg status IDLE", target.GetName());
            return true;
        }

        NewRpgHelpers::ProfessionCommand const professionCommand =
            NewRpgHelpers::ParseProfessionCommand(text);
        if (!professionCommand.recognized)
        {
            handler->SendErrorMessage(
                "Usage: .playerbots debug rpg <bot> do profession <name>|do professions [name ...]|idle");
            return false;
        }

        if (!professionCommand.plural && professionCommand.names.empty())
        {
            handler->SendErrorMessage(
                "Usage: .playerbots debug rpg <bot> do profession <name>|do professions [name ...]|idle");
            return false;
        }

        std::vector<uint32> skills;
        if (professionCommand.names.empty())
        {
            skills = NewRpgBaseAction::SelectProfessionsToLevel(botAI);
            if (skills.empty())
            {
                handler->SendErrorMessage("{}: No profession work available right now", target.GetName());
                return false;
            }

            size_t const start = urand(0, skills.size() - 1);
            std::rotate(skills.begin(), skills.begin() + start, skills.end());
        }
        else
        {
            NewRpgHelpers::ProfessionSelection const selection =
                NewRpgHelpers::ParseProfessionSelection(botAI, professionCommand.names);
            if (selection.error != NewRpgHelpers::ProfessionParseError::None)
            {
                handler->SendErrorMessage("{}: {}", target.GetName(), ProfessionErrorText(selection));
                return false;
            }

            if (!professionCommand.plural && selection.skills.size() != 1)
            {
                handler->SendErrorMessage("{}: 'do profession' takes exactly one profession", target.GetName());
                return false;
            }

            skills = selection.skills;
        }

        std::vector<uint32> pending(skills.begin() + 1, skills.end());
        botAI->rpgInfo.ChangeToDoProfessions(skills.front(), std::move(pending));
        handler->PSendSysMessage("{}: rpg status DO_PROFESSIONS (skill {})", target.GetName(), skills.front());
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
