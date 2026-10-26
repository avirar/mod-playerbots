/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Nav3DTestAction.h"

#include "Log.h"
#include "MotionMaster.h"
#include "Nav3DManager.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

namespace
{
    float Distance(G3D::Vector3 const& left, G3D::Vector3 const& right)
    {
        float const dx = left.x - right.x;
        float const dy = left.y - right.y;
        float const dz = left.z - right.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    bool Near(G3D::Vector3 const& left, G3D::Vector3 const& right)
    {
        float const dx = left.x - right.x;
        float const dy = left.y - right.y;
        float const dz = left.z - right.z;
        return dx * dx + dy * dy + dz * dz <= 0.05f * 0.05f;
    }
}

bool Nav3DTestAction::Execute(Event event)
{
    std::string text = event.getParam();
    bool const fly = text.rfind("fly ", 0) == 0;
    if (fly)
        text.erase(0, 4);

    std::istringstream input(text);

    std::vector<Position> points;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    while (input >> x >> y >> z)
        points.emplace_back(x, y, z);

    if (points.empty())
    {
        botAI->TellMasterNoFacing("Usage: nav3d [fly] <goalX> <goalY> <goalZ> [viaX viaY viaZ ...]");
        return true;
    }

    if (!sPlayerbotAIConfig.Nav3DEnabled || !sPlayerbotAIConfig.Nav3DLazy)
    {
        botAI->TellMasterNoFacing("Nav3D is disabled (or lazy mode is off) in playerbots.conf");
        return true;
    }

    Player* bot = botAI->GetBot();
    if (!bot || !bot->GetMap())
        return false;

    // Test-only: a forced fly spline does not disable the player's gravity by itself. Put the
    // bot on a real flying mount so the mount aura supplies the fly flags.
    if (fly)
    {
        bot->SetSkill(SKILL_RIDING, 4, 300, 300);
        if (bot->IsMounted())
            bot->Dismount();
        bot->CastSpell(bot, 34068, true);  // Swift Nether Drake
    }

    Position const start(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    Position const goal = points.front();

    // This is a local movement probe, not a world-travel command. A stale or wrong-map goal
    // otherwise looks like a valid direct line in the vmap void and produces a huge spline.
    constexpr float kMaxCommandDistance = 450.0f;
    float const commandDistance = std::sqrt(std::pow(goal.GetPositionX() - start.GetPositionX(), 2) +
                                            std::pow(goal.GetPositionY() - start.GetPositionY(), 2) +
                                            std::pow(goal.GetPositionZ() - start.GetPositionZ(), 2));
    if (commandDistance > kMaxCommandDistance)
    {
        std::ostringstream error;
        error << "nav3d goal is " << std::fixed << std::setprecision(1) << commandDistance
              << " yd away; this command is limited to " << kMaxCommandDistance << " yd";
        botAI->TellMasterNoFacing(error.str());
        return true;
    }

    std::vector<Position> route;
    route.push_back(start);
    for (size_t i = 1; i < points.size(); ++i)
        route.push_back(points[i]);
    route.push_back(goal);

    Movement::PointsArray path;
    Nav3DResult result;
    bool found = false;

    // Inside a WMO the navmesh may already have the route even though the outside approach does
    // not. Prefer a normal 2D path that ends within interaction range; fall back to 3D otherwise.
    if (route.size() == 2)
    {
        PathGenerator generator(bot);
        generator.CalculatePath(goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ());
        PathType const pathType = generator.GetPathType();
        if ((pathType & PATHFIND_NORMAL) && !(pathType & PATHFIND_NOPATH))
        {
            Movement::PointsArray const& navPath = generator.GetPath();
            G3D::Vector3 const goalPoint(goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ());
            // GetActualEndPosition() is the requested point before NormalizePath() snaps the
            // stored points to terrain. Use the real last point: in the vmap void the requested
            // point can be valid while the stored point is at INVALID_HEIGHT.
            float const endDistance = navPath.empty() ? std::numeric_limits<float>::max()
                                                      : Distance(navPath.back(), goalPoint);
            if (endDistance <= 5.0f)
            {
                path = navPath;
                found = true;
                result.Direct = path.size() <= 2;
                result.Partial = endDistance > 0.5f;
                result.BestDistance = endDistance;
                result.BestX = navPath.back().x;
                result.BestY = navPath.back().y;
                result.BestZ = navPath.back().z;
                for (size_t i = 1; i < path.size(); ++i)
                    result.Length += Distance(path[i - 1], path[i]);
            }
        }
    }

    if (!found)
    {
        result.Direct = true;
        found = true;
        for (size_t i = 0; i + 1 < route.size(); ++i)
        {
            Movement::PointsArray leg;
            Nav3DResult legResult;
            if (!Nav3DManager::Instance().FindPath(bot->GetMap(), bot->GetPhaseMask(), route[i], route[i + 1], leg,
                                                   legResult))
            {
                found = false;
                result.BestDistance = legResult.BestDistance;
                result.BestX = legResult.BestX;
                result.BestY = legResult.BestY;
                result.BestZ = legResult.BestZ;
                result.Nodes = legResult.Nodes;
                result.Expanded = legResult.Expanded;
                result.ElapsedMs = legResult.ElapsedMs;
                break;
            }

            for (G3D::Vector3 const& point : leg)
            {
                if (path.empty() || !Near(path.back(), point))
                    path.push_back(point);
            }

            result.Nodes += legResult.Nodes;
            result.Expanded += legResult.Expanded;
            result.ElapsedMs += legResult.ElapsedMs;
            result.Length += legResult.Length;
            result.Direct = result.Direct && legResult.Direct;
            result.Partial = result.Partial || legResult.Partial;
            result.Reversed = result.Reversed || legResult.Reversed;
            result.BestDistance = std::max(result.BestDistance, legResult.BestDistance);
        }
    }

    result.Found = found;
    if (!found)
        path.clear();

    std::ostringstream summary;
    summary << "nav3d " << (found ? "found" : "failed") << " fly=" << (fly ? 1 : 0)
            << " legs=" << (route.size() - 1) << " direct=" << (result.Direct ? 1 : 0)
            << " partial=" << (result.Partial ? 1 : 0) << " reversed=" << (result.Reversed ? 1 : 0)
            << " nodes=" << result.Nodes
            << " expanded=" << result.Expanded << " ms=" << result.ElapsedMs << " len=" << result.Length
            << " best=" << result.BestDistance << " points=" << path.size();

    if (!found)
    {
        botAI->TellMasterNoFacing(summary.str());
        LOG_INFO("playerbots", "{} start=({:.2f},{:.2f},{:.2f}) goal=({:.2f},{:.2f},{:.2f}) {}", bot->GetName(),
                 start.GetPositionX(), start.GetPositionY(), start.GetPositionZ(), goal.GetPositionX(),
                 goal.GetPositionY(), goal.GetPositionZ(), summary.str());
        return true;
    }

    if (!Nav3DManager::Instance().IsPathClear(bot->GetMap(), bot->GetPhaseMask(), path))
    {
        botAI->TellMasterNoFacing("nav3d path blocked by clearance");
        return true;
    }

    bot->StopMoving();
    bot->GetMotionMaster()->MoveSplinePath(&path, fly ? FORCED_MOVEMENT_FLY : FORCED_MOVEMENT_NONE);

    // Freeze the AI while the one-shot debug spline plays out; the movement generator still runs.
    uint32 delay = uint32(result.Length * 250.0f);
    delay = std::max<uint32>(30000, std::min<uint32>(120000, delay));
    botAI->SetNextCheckDelay(delay);

    botAI->TellMasterNoFacing(summary.str() + " moving");
    LOG_INFO("playerbots",
             "{} start=({:.2f},{:.2f},{:.2f}) goal=({:.2f},{:.2f},{:.2f}) {} moving delay={}ms", bot->GetName(),
             start.GetPositionX(), start.GetPositionY(), start.GetPositionZ(), goal.GetPositionX(),
             goal.GetPositionY(), goal.GetPositionZ(), summary.str(), delay);
    for (size_t i = 0; i < path.size(); ++i)
        LOG_INFO("playerbots", "nav3d {} point[{}] = ({:.2f},{:.2f},{:.2f})", bot->GetName(), i, path[i].x, path[i].y,
                 path[i].z);
    return true;
}
