/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// TEST-ONLY (A/B harness): shared lightweight loot sampler, applied to both A/B builds so the
// metrics are comparable. Not for upstream.

#ifndef PLAYERBOTS_LOOTSAMPLER_H
#define PLAYERBOTS_LOOTSAMPLER_H

class LootSampler
{
public:
    static void SetEnabled(bool enabled);
    static bool IsEnabled();
    static void Update();
};

#endif
