/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "InteractExecutor.h"

#include "InteractProfiles.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "QuestItemCatalog.h"
#include "Timer.h"
#include <algorithm>
#include <memory>

void InteractExecutor::Refresh(Player* bot)
{
    uint32 const now = getMSTime();
    if (lastRefresh && now - lastRefresh < REFRESH_INTERVAL_MS)
        return;

    lastRefresh = now;
    nearby.clear();
    far.clear();

    if (!bot || !bot->IsInWorld())
        return;

    std::vector<QuestItemCandidate> candidates;
    QuestItemCatalog::Build(bot, candidates);
    if (candidates.empty())
        return;

    // Prefer objectives closest to completion and cap the number of target scans.
    std::sort(candidates.begin(), candidates.end(),
              [](QuestItemCandidate const& a, QuestItemCandidate const& b)
              { return (a.requiredCount - a.currentCount) < (b.requiredCount - b.currentCount); });

    if (candidates.size() > MAX_SCANNED_CANDIDATES)
        candidates.resize(MAX_SCANNED_CANDIDATES);

    Prune(now);

    float const scanRange = sPlayerbotAIConfig.InteractScanDistance;

    for (QuestItemCandidate const& candidate : candidates)
    {
        std::unique_ptr<InteractProfile> profile = InteractProfile::Create(candidate.kind);
        if (!profile)
            continue;

        InteractTarget target;
        if (!profile->SelectTarget(bot, candidate, scanRange, target))
            continue;

        if (IsIgnored(target, now))
            continue;

        if (target.distance <= INTERACT_ACTION_RANGE)
            nearby.push_back(target);
        else
            far.push_back(target);
    }

    auto byDistance = [](InteractTarget const& a, InteractTarget const& b) { return a.distance < b.distance; };
    std::sort(nearby.begin(), nearby.end(), byDistance);
    std::sort(far.begin(), far.end(), byDistance);
}

InteractTarget const* InteractExecutor::BestNearby()
{
    uint32 const now = getMSTime();
    for (InteractTarget const& target : nearby)
        if (!IsIgnored(target, now))
            return &target;

    return nullptr;
}

InteractTarget const* InteractExecutor::BestFar()
{
    uint32 const now = getMSTime();
    for (InteractTarget const& target : far)
        if (!IsIgnored(target, now))
            return &target;

    return nullptr;
}

void InteractExecutor::NotifyUsed(InteractTarget const& target)
{
    ignoredUntil[Key(target)] = getMSTime() + USED_COOLDOWN_MS;
}

void InteractExecutor::NotifyFailed(InteractTarget const& target)
{
    ignoredUntil[Key(target)] = getMSTime() + FAILED_COOLDOWN_MS;
}

std::string InteractExecutor::Key(InteractTarget const& target)
{
    return std::to_string(target.guid.GetRawValue()) + ":" + std::to_string(target.candidate.spellId) + ":" +
           std::to_string(target.candidate.questId) + ":" + std::to_string(target.candidate.objectiveIdx);
}

bool InteractExecutor::IsIgnored(InteractTarget const& target, uint32 now) const
{
    auto itr = ignoredUntil.find(Key(target));
    return itr != ignoredUntil.end() && itr->second > now;
}

void InteractExecutor::Prune(uint32 now)
{
    for (auto itr = ignoredUntil.begin(); itr != ignoredUntil.end();)
    {
        if (itr->second <= now)
            itr = ignoredUntil.erase(itr);
        else
            ++itr;
    }
}
