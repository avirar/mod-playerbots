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
#include "ObjectMgr.h"
#include "PerfMonitor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "QuestDef.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "Timer.h"

#include <filesystem>
#include <sstream>

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
            {"nav3dmove", HandleNav3DMoveCommand, SEC_GAMEMASTER, Console::Yes},
            {"nav3dmap", HandleNav3DMapCommand, SEC_GAMEMASTER, Console::Yes},
            {"nav3dair", HandleNav3DAirCommand, SEC_GAMEMASTER, Console::Yes},
            {"nav3dcanfly", HandleNav3DCanFlyCommand, SEC_GAMEMASTER, Console::Yes},
            {"nav3dteleport", HandleNav3DTeleportCommand, SEC_GAMEMASTER, Console::Yes},
            {"nav3dunmount", HandleNav3DUnmountCommand, SEC_GAMEMASTER, Console::Yes},
            {"nav3dbake", HandleNav3DBakeCommand, SEC_GAMEMASTER, Console::Yes},
            {"nav3dairbake", HandleNav3DAirBakeCommand, SEC_GAMEMASTER, Console::Yes},
            {"nav3dairdetail", HandleNav3DAirDetailCommand, SEC_GAMEMASTER, Console::Yes},
            {"nav3dtile", HandleNav3DTileCommand, SEC_GAMEMASTER, Console::Yes},
            {"addquest", HandleAddQuestCommand, SEC_GAMEMASTER, Console::Yes},
            {"completequest", HandleCompleteQuestCommand, SEC_GAMEMASTER, Console::Yes},
            {"surface", HandleSurfaceCommand, SEC_GAMEMASTER, Console::Yes},
            {"savebot", HandleSaveBotCommand, SEC_GAMEMASTER, Console::Yes},
            {"revive", HandleReviveCommand, SEC_GAMEMASTER, Console::Yes},
            {"abandonquest", HandleAbandonQuestCommand, SEC_GAMEMASTER, Console::Yes},
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
                                      "movement, mount, target, hp, strategy, action, values, travel, budget)");
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

    // Test-only: run the nav3d chat action directly, bypassing the chat command security queue
    // so a random bot (no master) can exercise the movement path.
    static bool HandleNav3DMoveCommand(ChatHandler* handler, PlayerIdentifier target, Tail command)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        std::string const param = std::string(command);
        if (param.empty())
        {
            handler->SendErrorMessage("Usage: nav3dmove <bot> [fly] <x> <y> <z> [viaX viaY viaZ ...]");
            return false;
        }

        Event event("nav3d", param, botAI->GetBot());
        bool const started = botAI->DoSpecificAction("nav3d", event, false);
        handler->PSendSysMessage("nav3dmove {} started={}", target.GetName(), started);
        LOG_INFO("playerbots", "nav3dmove {} started={} param={}", target.GetName(), started, param);
        return started;
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

    static bool HandleNav3DAirCommand(ChatHandler* handler, uint32 mapId, float sx, float sy, float sz,
                                      float gx, float gy, float gz)
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
        bool const found = Nav3DManager::Instance().FindPathAir(map, PHASEMASK_NORMAL, start, goal, path, result);
        handler->PSendSysMessage(
            "nav3dair {}: found={} direct={} points={} len={:.1f} ms={} cell={:.1f}", mapId,
            found ? 1 : 0, result.Direct ? 1 : 0, path.size(), result.Length, result.ElapsedMs,
            result.CellSize);
        LOG_INFO("playerbots",
                 "nav3dair {} start=({:.2f},{:.2f},{:.2f}) goal=({:.2f},{:.2f},{:.2f}) found={} direct={} "
                 "points={} len={:.1f} ms={} cell={:.1f}",
                 mapId, sx, sy, sz, gx, gy, gz, found ? 1 : 0, result.Direct ? 1 : 0, path.size(),
                 result.Length, result.ElapsedMs, result.CellSize);
        for (size_t i = 0; i < path.size(); ++i)
            LOG_INFO("playerbots", "nav3dair {} point[{}] = ({:.2f},{:.2f},{:.2f})", mapId, i, path[i].x,
                     path[i].y, path[i].z);
        return true;
    }

    static bool HandleNav3DCanFlyCommand(ChatHandler* handler, PlayerIdentifier target)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;
        Player* bot = botAI->GetBot();
        bool const allowed = Nav3DManager::CanUseFlight(bot);
        handler->PSendSysMessage("nav3dcanfly {} map={} zone={} area={} canFly={}", target.GetName(),
                                 bot->GetMapId(), bot->GetZoneId(), bot->GetAreaId(), allowed ? 1 : 0);
        LOG_INFO("playerbots", "nav3dcanfly {} map={} zone={} area={} canFly={}", target.GetName(),
                 bot->GetMapId(), bot->GetZoneId(), bot->GetAreaId(), allowed ? 1 : 0);
        return true;
    }

    static bool HandleNav3DTileCommand(ChatHandler* handler, PlayerIdentifier target, float x, float y,
                                       float z, Optional<float> radiusArg)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        Player* bot = botAI->GetBot();
        Map* map = bot->GetMap();
        if (!map)
        {
            handler->SendErrorMessage("nav3dtile {} has no map", target.GetName());
            return false;
        }

        constexpr float gridSize = 533.3333f;
        uint16 const tileX = uint16(std::max<int32>(0, int32(std::floor(32.0f - x / gridSize))));
        uint16 const tileY = uint16(std::max<int32>(0, int32(std::floor(32.0f - y / gridSize))));
        std::shared_ptr<Nav3DTile> tile = Nav3DManager::Instance().GetTile(map->GetId(), tileX, tileY,
                                                                          Nav3D::MediumWater);
        if (!tile)
        {
            handler->SendErrorMessage("nav3dtile no tile for map {} tile {}:{}", map->GetId(), tileX, tileY);
            return false;
        }

        Nav3DTileHeader const& header = tile->Header();
        float const radius = radiusArg.value_or(0.5f);
        bool const navigable = tile->IsNavigable(x, y, z, radius);
        uint8 const clearance = tile->GetClearance(x, y, z);
        handler->PSendSysMessage(
            "nav3dtile map {} tile {}:{} origin=({:.2f},{:.2f},{:.2f}) size={}x{}x{} cell={:.2f} "
            "point=({:.2f},{:.2f},{:.2f}) radius={:.2f} navigable={} clearance={}",
            map->GetId(), tileX, tileY, header.OriginX, header.OriginY, header.OriginZ, header.SizeX,
            header.SizeY, header.SizeZ, header.CellSize, x, y, z, radius, navigable ? 1 : 0, clearance);
        LOG_INFO("playerbots",
                 "nav3dtile map {} tile {}:{} origin=({:.2f},{:.2f},{:.2f}) size={}x{}x{} cell={:.2f} "
                 "point=({:.2f},{:.2f},{:.2f}) radius={:.2f} navigable={} clearance={}",
                 map->GetId(), tileX, tileY, header.OriginX, header.OriginY, header.OriginZ, header.SizeX,
                 header.SizeY, header.SizeZ, header.CellSize, x, y, z, radius, navigable ? 1 : 0, clearance);
        return true;
    }

    static bool HandleNav3DUnmountCommand(ChatHandler* handler, PlayerIdentifier target)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        Player* bot = botAI->GetBot();
        uint32 const mountSpell = bot->GetMountBlockId();
        bot->Dismount();
        handler->PSendSysMessage("nav3dunmount {} mountSpell={} mounted={}", target.GetName(), mountSpell,
                                 bot->IsMounted() ? 1 : 0);
        LOG_INFO("playerbots", "nav3dunmount {} removed mount spell {} mounted={}", target.GetName(), mountSpell,
                 bot->IsMounted() ? 1 : 0);
        return true;
    }

    static bool HandleNav3DAirBakeCommand(ChatHandler* handler, PlayerIdentifier target, Tail command)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        Player* bot = botAI->GetBot();
        Map* map = bot->GetMap();
        if (!map)
        {
            handler->SendErrorMessage("nav3dairbake {} has no map", target.GetName());
            return false;
        }

        std::istringstream in{std::string(command)};
        uint32 tileX = 0;
        uint32 tileY = 0;
        float minX = 0.0f;
        float maxX = 0.0f;
        float minY = 0.0f;
        float maxY = 0.0f;
        float minZ = 0.0f;
        float maxZ = 0.0f;
        if (!(in >> tileX >> tileY >> minX >> maxX >> minY >> maxY >> minZ >> maxZ))
        {
            handler->SendErrorMessage(
                "Usage: nav3dairbake <bot> <tileX> <tileY> <minX> <maxX> <minY> <maxY> <minZ> <maxZ> "
                "[seedX seedY seedZ ...]");
            return false;
        }

        std::vector<Position> seeds;
        float seedX = 0.0f;
        float seedY = 0.0f;
        float seedZ = 0.0f;
        while (in >> seedX >> seedY >> seedZ)
            seeds.emplace_back(seedX, seedY, seedZ);
        if (seeds.empty())
        {
            handler->SendErrorMessage("nav3dairbake requires at least one seed");
            return false;
        }

        std::string dataPath = sWorld ? sWorld->GetDataPath() : std::string();
        if (!dataPath.empty() && dataPath.back() != '/')
            dataPath += '/';
        dataPath += sPlayerbotAIConfig.Nav3DDataPath;
        if (!dataPath.empty() && dataPath.back() != '/')
            dataPath += '/';

        std::string const source = Acore::StringFormat("{}map{:04}/{:02}_{:02}_air.n3t", dataPath,
                                                       map->GetId(), tileX, tileY);
        std::string const output = source + ".new";
        std::string const backup = source + ".bak";

        std::error_code ec;
        if (std::filesystem::exists(source, ec) && !std::filesystem::exists(backup, ec))
            std::filesystem::copy_file(source, backup, std::filesystem::copy_options::none, ec);
        if (ec)
        {
            handler->SendErrorMessage("nav3dairbake backup failed: {}", ec.message());
            return false;
        }

        std::string error;
        if (!Nav3DManager::Instance().BakeAirTile(map, bot->GetPhaseMask(), uint16(tileX), uint16(tileY),
                                                  4.0f, minZ, maxZ, minX, maxX, minY, maxY, source,
                                                  output, seeds, 0.0f, error))
        {
            handler->SendErrorMessage("nav3dairbake {} failed: {}", target.GetName(), error);
            return false;
        }

        std::filesystem::copy_file(output, source, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            handler->SendErrorMessage("nav3dairbake commit failed: {}", ec.message());
            return false;
        }
        Nav3DManager::Instance().ClearTileCache();

        handler->PSendSysMessage("nav3dairbake {} tile {}:{} wrote {}", target.GetName(), tileX, tileY,
                                 source);
        LOG_INFO("playerbots", "nav3dairbake {} tile {}:{} source={} backup={}", target.GetName(), tileX,
                 tileY, source, backup);
        return true;
    }

    static bool HandleNav3DAirDetailCommand(ChatHandler* handler, PlayerIdentifier target, Tail command)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        Player* bot = botAI->GetBot();
        Map* map = bot->GetMap();
        if (!map)
        {
            handler->SendErrorMessage("nav3dairdetail {} has no map", target.GetName());
            return false;
        }

        std::istringstream in{std::string(command)};
        uint32 tileX = 0;
        uint32 tileY = 0;
        float minX = 0.0f;
        float maxX = 0.0f;
        float minY = 0.0f;
        float maxY = 0.0f;
        float minZ = 0.0f;
        float maxZ = 0.0f;
        float corridor = 20.0f;
        if (!(in >> tileX >> tileY >> minX >> maxX >> minY >> maxY >> minZ >> maxZ >> corridor))
        {
            handler->SendErrorMessage(
                "Usage: nav3dairdetail <bot> <tileX> <tileY> <minX> <maxX> <minY> <maxY> "
                "<minZ> <maxZ> <corridor> <seedX seedY seedZ ...>");
            return false;
        }

        std::vector<Position> seeds;
        float seedX = 0.0f;
        float seedY = 0.0f;
        float seedZ = 0.0f;
        while (in >> seedX >> seedY >> seedZ)
            seeds.emplace_back(seedX, seedY, seedZ);
        if (seeds.empty())
        {
            handler->SendErrorMessage("nav3dairdetail requires at least one seed");
            return false;
        }

        std::string dataPath = sWorld ? sWorld->GetDataPath() : std::string();
        if (!dataPath.empty() && dataPath.back() != '/')
            dataPath += '/';
        dataPath += sPlayerbotAIConfig.Nav3DDataPath;
        if (!dataPath.empty() && dataPath.back() != '/')
            dataPath += '/';

        std::string const source = Acore::StringFormat("{}map{:04}/{:02}_{:02}_airdetail.n3t",
                                                       dataPath, map->GetId(), tileX, tileY);
        std::string const output = source + ".new";
        std::string const backup = source + ".bak";

        std::error_code ec;
        if (std::filesystem::exists(source, ec) && !std::filesystem::exists(backup, ec))
            std::filesystem::copy_file(source, backup, std::filesystem::copy_options::none, ec);
        if (ec)
        {
            handler->SendErrorMessage("nav3dairdetail backup failed: {}", ec.message());
            return false;
        }

        std::string error;
        if (!Nav3DManager::Instance().BakeAirDetailTile(
                map, bot->GetPhaseMask(), uint16(tileX), uint16(tileY), 1.0f, minZ, maxZ, minX,
                maxX, minY, maxY, corridor, seeds, output, error))
        {
            handler->SendErrorMessage("nav3dairdetail {} failed: {}", target.GetName(), error);
            return false;
        }

        std::filesystem::copy_file(output, source, std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec)
        {
            handler->SendErrorMessage("nav3dairdetail commit failed: {}", ec.message());
            return false;
        }
        Nav3DManager::Instance().ClearTileCache();

        handler->PSendSysMessage("nav3dairdetail {} tile {}:{} wrote {}", target.GetName(), tileX,
                                 tileY, source);
        LOG_INFO("playerbots", "nav3dairdetail {} tile {}:{} source={} backup={} corridor={:.1f}",
                 target.GetName(), tileX, tileY, source, backup, corridor);
        return true;
    }

    static bool HandleNav3DBakeCommand(ChatHandler* handler, PlayerIdentifier target, Tail command)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        std::istringstream in{std::string(command)};
        uint32 tileX = 0;
        uint32 tileY = 0;
        float cellSize = 1.0f;
        float waterLevel = 0.0f;
        float minZ = -150.0f;
        float boundsMinX = 0.0f;
        float boundsMaxX = 0.0f;
        float boundsMinY = 0.0f;
        float boundsMaxY = 0.0f;
        if (!(in >> tileX >> tileY))
        {
            handler->SendErrorMessage(
                "Usage: nav3dbake <bot> <tileX> <tileY> [cellSize] [waterLevel] [minZ] "
                "[minX maxX minY maxY]");
            return false;
        }
        in >> cellSize;
        if (!in)
            cellSize = 1.0f;
        in.clear();
        in >> waterLevel;
        if (!in)
            waterLevel = 0.0f;
        in.clear();
        in >> minZ;
        if (!in)
            minZ = waterLevel - 150.0f;

        in.clear();
        if (!(in >> boundsMinX >> boundsMaxX >> boundsMinY >> boundsMaxY))
            boundsMinX = boundsMaxX = boundsMinY = boundsMaxY = 0.0f;

        Player* bot = botAI->GetBot();
        Map* map = bot->GetMap();
        if (!map)
        {
            handler->SendErrorMessage("nav3dbake {} has no map", target.GetName());
            return false;
        }

        std::string dataPath = sWorld ? sWorld->GetDataPath() : std::string();
        if (!dataPath.empty() && dataPath.back() != '/')
            dataPath += '/';
        dataPath += sPlayerbotAIConfig.Nav3DDataPath;
        if (!dataPath.empty() && dataPath.back() != '/')
            dataPath += '/';

        std::string const relative =
            Acore::StringFormat("map{:04}/{}_{}_water.n3t", map->GetId(), tileX, tileY);
        std::filesystem::path const outputPath = std::filesystem::path(dataPath) / relative;
        std::error_code ec;
        std::filesystem::create_directories(outputPath.parent_path(), ec);
        if (ec)
        {
            handler->SendErrorMessage("nav3dbake cannot create {}: {}", outputPath.parent_path().string(),
                                      ec.message());
            return false;
        }

        std::string error;
        bool const baked = Nav3DManager::Instance().BakeWaterTile(
            map, bot->GetPhaseMask(), uint16(tileX), uint16(tileY), cellSize, waterLevel, minZ,
            boundsMinX, boundsMaxX, boundsMinY, boundsMaxY, outputPath.string(), error);
        if (!baked)
        {
            handler->SendErrorMessage("nav3dbake failed: {}", error);
            LOG_ERROR("playerbots", "nav3dbake failed map {} tile {}:{}: {}", map->GetId(), tileX, tileY,
                      error);
            return false;
        }

        Nav3DManager::Instance().ClearTileCache();
        handler->PSendSysMessage("nav3dbake {} map {} tile {}:{} -> {}", target.GetName(), map->GetId(),
                                 tileX, tileY, outputPath.string());
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

    static bool HandleAddQuestCommand(ChatHandler* handler, PlayerIdentifier target, uint32 questId)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        Player* bot = botAI->GetBot();
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
        {
            handler->SendErrorMessage("Unknown quest {}", questId);
            return false;
        }

        if (bot->GetQuestStatus(questId) != QUEST_STATUS_NONE)
        {
            handler->PSendSysMessage("{} already has quest {} (status {})", target.GetName(), questId,
                                     uint32(bot->GetQuestStatus(questId)));
            return true;
        }

        if (!bot->CanAddQuest(quest, true))
        {
            handler->SendErrorMessage("{} cannot add quest {} (level/prev/race/faction)", target.GetName(), questId);
            return false;
        }

        bot->AddQuest(quest, nullptr);
        handler->PSendSysMessage("addquest {} {} status={}", target.GetName(), questId,
                                 uint32(bot->GetQuestStatus(questId)));
        LOG_INFO("playerbots", "addquest {} {} status={}", target.GetName(), questId,
                 uint32(bot->GetQuestStatus(questId)));
        return true;
    }

    static bool HandleCompleteQuestCommand(ChatHandler* handler, PlayerIdentifier target, uint32 questId)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        Player* bot = botAI->GetBot();
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
        {
            handler->SendErrorMessage("Unknown quest {}", questId);
            return false;
        }

        if (bot->GetQuestStatus(questId) == QUEST_STATUS_NONE)
        {
            handler->SendErrorMessage("{} does not have quest {}", target.GetName(), questId);
            return false;
        }

        bot->CompleteQuest(questId);
        if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
            bot->RewardQuest(quest, 0, bot, false);

        handler->PSendSysMessage("completequest {} {} status={}", target.GetName(), questId,
                                 uint32(bot->GetQuestStatus(questId)));
        LOG_INFO("playerbots", "completequest {} {} status={}", target.GetName(), questId,
                 uint32(bot->GetQuestStatus(questId)));
        return true;
    }

    static bool HandleSurfaceCommand(ChatHandler* handler, PlayerIdentifier target)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        Player* bot = botAI->GetBot();
        if (!bot->IsUnderWater())
        {
            handler->SendErrorMessage("{} is not underwater", target.GetName());
            return false;
        }

        botAI->diveAirMs = 15000;
        botAI->diveSubmergedSince = getMSTime();
        bool const started = botAI->DoSpecificAction("surface from dive", Event(), false);
        handler->PSendSysMessage("surface {} started={} air={}s underwater={}", target.GetName(), started,
                                 botAI->diveAirMs / IN_MILLISECONDS, bot->IsUnderWater() ? 1 : 0);
        LOG_INFO("playerbots", "surface {} started={} air={}s", target.GetName(), started,
                 botAI->diveAirMs / IN_MILLISECONDS);
        return started;
    }

    static bool HandleSaveBotCommand(ChatHandler* handler, PlayerIdentifier target)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        Player* bot = botAI->GetBot();
        bot->SaveToDB(false, false);
        handler->PSendSysMessage("savebot {} saved", target.GetName());
        LOG_INFO("playerbots", "savebot {}", target.GetName());
        return true;
    }

    static bool HandleAbandonQuestCommand(ChatHandler* handler, PlayerIdentifier target, uint32 questId)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        Player* bot = botAI->GetBot();
        if (bot->GetQuestStatus(questId) == QUEST_STATUS_NONE)
        {
            handler->SendErrorMessage("{} does not have quest {}", target.GetName(), questId);
            return false;
        }

        bot->AbandonQuest(questId);
        handler->PSendSysMessage("abandonquest {} {} status={}", target.GetName(), questId,
                                 uint32(bot->GetQuestStatus(questId)));
        LOG_INFO("playerbots", "abandonquest {} {} status={}", target.GetName(), questId,
                 uint32(bot->GetQuestStatus(questId)));
        return true;
    }

    static bool HandleReviveCommand(ChatHandler* handler, PlayerIdentifier target)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        Player* bot = botAI->GetBot();
        if (!bot->IsAlive())
        {
            botAI->ChangeStrategy("-dead", BOT_STATE_NON_COMBAT);
            bot->ResurrectPlayer(1.0f);
            bot->SpawnCorpseBones();
            bot->SetHealth(bot->GetMaxHealth());
        }

        handler->PSendSysMessage("revive {} alive={} hp={}", target.GetName(), bot->IsAlive() ? 1 : 0,
                                 bot->GetHealth());
        LOG_INFO("playerbots", "revive {} alive={} hp={}", target.GetName(), bot->IsAlive() ? 1 : 0,
                 bot->GetHealth());
        return bot->IsAlive();
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
