/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_INTERACTTYPES_H
#define PLAYERBOTS_INTERACTTYPES_H

#include "ObjectGuid.h"
#include "SharedDefines.h"

// Distance within which an interaction is attempted; beyond it the bot walks (up to the scan
// distance from config). Item spell ranges vary, so the action may still be refused by the server.
constexpr float INTERACT_ACTION_RANGE = 25.0f;

// How a quest objective item is used. Extend with corpse/rotation kinds as profiles land.
enum class InteractKind : uint8
{
    SelfUse = 0,         // use the item where the bot is (self/anywhere spells)
    UseOnCreature = 1,   // use the item on a nearby living creature
    UseOnGameObject = 2, // use the item on a nearby gameobject (open/use/activate)
    UseAtLocation = 3,   // walk to the objective's POI and use the item there
};

// One usable (quest, objective, item, spell) tuple compiled from the bot's quest log.
struct QuestItemCandidate
{
    uint32 questId{0};
    uint32 objectiveIdx{0}; // index into RequiredItemId/RequiredNpcOrGo (0..QUEST_ITEM_OBJECTIVES_COUNT-1)
    uint32 itemId{0};
    uint32 spellId{0};
    InteractKind kind{InteractKind::SelfUse};
    uint32 targetEntry{0}; // creature/GO entry the spell or quest names; 0 = any
    uint32 requiredCount{0};
    uint32 currentCount{0};
};

// The resolved interaction: where to go and what to do.
struct InteractTarget
{
    QuestItemCandidate candidate;
    ObjectGuid guid; // creature/GO guid; empty for self/location
    uint32 mapId{0};
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float distance{0.0f};
};

#endif
