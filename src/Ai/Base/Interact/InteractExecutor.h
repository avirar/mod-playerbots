/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_INTERACTEXECUTOR_H
#define PLAYERBOTS_INTERACTEXECUTOR_H

#include "InteractTypes.h"
#include <string>
#include <unordered_map>
#include <vector>

class Player;

// Per-bot quest-item interaction state: candidate pools (rebuilt at a throttle), per-target ignore
// cooldowns, and the "best" pickers the actions use. Owned by the "interact executor" value so both
// the actions and the triggers read the same state.
class InteractExecutor
{
public:
    void Refresh(Player* bot);

    std::vector<InteractTarget> const& Nearby() const { return nearby; }
    std::vector<InteractTarget> const& Far() const { return far; }

    // Best non-ignored target, or nullptr. Checks the ignore map fresh (Refresh itself is throttled).
    InteractTarget const* BestNearby();
    InteractTarget const* BestFar();

    void NotifyUsed(InteractTarget const& target);
    void NotifyFailed(InteractTarget const& target);

private:
    static constexpr uint32 REFRESH_INTERVAL_MS = 2000;
    static constexpr uint32 USED_COOLDOWN_MS = 4000;
    static constexpr uint32 FAILED_COOLDOWN_MS = 20000;
    static constexpr size_t MAX_SCANNED_CANDIDATES = 8;

    std::vector<InteractTarget> nearby;
    std::vector<InteractTarget> far;
    std::unordered_map<std::string, uint32> ignoredUntil;
    uint32 lastRefresh{0};

    static std::string Key(InteractTarget const& target);
    bool IsIgnored(InteractTarget const& target, uint32 now) const;
    void Prune(uint32 now);
};

#endif
