/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NEWRPGHELPERS_H
#define PLAYERBOTS_NEWRPGHELPERS_H

#include "ObjectGuid.h"

class Object;
class Player;
class PlayerbotAI;
class Quest;
class WorldObject;

enum ItemUsage : uint32;
struct ItemTemplate;

// Quest checks shared by the new RPG actions and values. They encode new RPG questing policy
// (solo, non-repeatable, level window), not general quest rules.
namespace NewRpgHelpers
{
// Radius within which a quest giver with something to accept or reward pulls the bot over.
constexpr float QUEST_GIVER_SEARCH_DISTANCE = 80.0f;

// Player::CanInteractWithQuestGiver without the distance check.
bool CanInteractWithQuestGiver(Player* bot, Object* questGiver);
bool HasQuestToAcceptOrReward(Player* bot, WorldObject* object);
bool IsQuestWorthDoing(Player* bot, Quest const* quest);
bool IsQuestCapableDoing(Player* bot, Quest const* quest);

// Nearest giver in the new RPG target caches with something to accept or reward, skipping givers
// the bot gave up on. A distanceLimit of 0 searches the whole cache.
ObjectGuid FindQuestGiver(PlayerbotAI* botAI, float distanceLimit = 0.0f);

// Profession conversion spells (milling, prospecting, disenchanting) target a bag item instead of
// declaring a Reagent[] entry. The profession selection gate and the profession action must agree
// on what counts as a usable input, or a bot is never selected to start the conversion.
constexpr uint32 PROFESSION_MILLING_SPELL = 51005;
constexpr uint32 PROFESSION_PROSPECTING_SPELL = 31252;
constexpr uint32 PROFESSION_DISENCHANT_SPELL = 13262;
constexpr uint32 PROFESSION_CONVERSION_INPUT_COUNT = 5;
constexpr uint32 PROFESSION_DISENCHANT_INPUT_COUNT = 1;

bool IsProfessionConversionSpell(uint32 spellId);
uint32 GetProfessionConversionSkillLine(uint32 spellId);
bool IsMillingInput(Player* bot, ItemTemplate const* proto, uint32 count);
bool IsProspectingInput(Player* bot, ItemTemplate const* proto, uint32 count);
bool IsDisenchantCandidate(Player* bot, ItemTemplate const* proto);
bool IsDisenchantableItem(Player* bot, ItemTemplate const* proto, ItemUsage usage);
bool HasProfessionConversionInput(PlayerbotAI* botAI, uint32 spellId);
}  // namespace NewRpgHelpers

#endif
