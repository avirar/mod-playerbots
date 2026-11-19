/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PROFESSIONUSEFULPOLICY_H
#define PLAYERBOTS_PROFESSIONUSEFULPOLICY_H

#include "Define.h"

enum ItemUsage : uint32;
struct ItemTemplate;
class PlayerbotAI;

namespace ProfessionUsefulPolicy
{
    // Final useful output (not a skill-up or AH-only recipe). This is the
    // single extension point for new useful categories (consumables, glyphs,
    // gems, ...).
    bool IsFinalUseful(PlayerbotAI* botAI, ItemTemplate const* proto, ItemUsage usage);

    // A profession tool the bot needs and does not have. Used both for the
    // useful pre-pass and for buying/crafting missing tools.
    bool IsMissingToolForBot(PlayerbotAI* botAI, ItemTemplate const* proto);

    // Lower is more important. 255 means not useful.
    uint8 GetUsefulRank(PlayerbotAI* botAI, ItemTemplate const* proto, ItemUsage usage);
}  // namespace ProfessionUsefulPolicy

#endif
