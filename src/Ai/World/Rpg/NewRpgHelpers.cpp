/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgHelpers.h"
#include "QuestLevelRules.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

#include "Bag.h"
#include "DBCStores.h"
#include "Creature.h"
#include "GameObject.h"
#include "GossipDef.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ItemUsageValue.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "World.h"

namespace NewRpgHelpers
{
bool CanInteractWithQuestGiver(Player* bot, Object* questGiver)
{
    // This is a variant of Player::CanInteractWithQuestGiver
    // that removes the distance check and keeps all other checks
    switch (questGiver->GetTypeId())
    {
        case TYPEID_UNIT:  // Player::GetNPCIfCanInteractWith
        {
            ObjectGuid guid = questGiver->GetGUID();

            // unit checks
            if (!guid)
                return false;

            if (!bot->IsInWorld() || bot->IsDuringRemoveFromWorld())
                return false;

            if (bot->IsInFlight())
                return false;

            // exist (we need look pets also for some interaction (quest/etc)
            Creature* creature = ObjectAccessor::GetCreatureOrPetOrVehicle(*bot, guid);
            if (!creature)
                return false;

            // Deathstate checks
            if (!bot->IsAlive() &&
                !(creature->GetCreatureTemplate()->type_flags & CREATURE_TYPE_FLAG_VISIBLE_TO_GHOSTS))
                return false;

            // alive or spirit healer
            if (!creature->IsAlive() &&
                !(creature->GetCreatureTemplate()->type_flags & CREATURE_TYPE_FLAG_INTERACT_WHILE_DEAD))
                return false;

            // appropriate npc type
            if (!creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
                return false;

            // not allow interaction under control, but allow with own pets
            if (creature->GetCharmerGUID())
                return false;

            // xinef: perform better check
            if (creature->GetReactionTo(bot) <= REP_UNFRIENDLY)
                return false;

            return true;
        }
        case TYPEID_GAMEOBJECT:  // Player::GetGameObjectIfCanInteractWith
        {
            ObjectGuid guid = questGiver->GetGUID();

            if (GameObject* go = bot->GetMap()->GetGameObject(guid))
            {
                if (go->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER)
                {
                    // Players cannot interact with gameobjects that use the "Point" icon
                    if (go->GetGOInfo()->IconName == "Point")
                        return false;

                    return true;
                }
            }

            return false;
        }
        // unused for now
        // case TYPEID_PLAYER:
        //     return bot->IsAlive() && questGiver->ToPlayer()->IsAlive();
        // case TYPEID_ITEM:
        //     return bot->IsAlive();
        default:
            break;
    }
    return false;
}

bool HasQuestToAcceptOrReward(Player* bot, WorldObject* object)
{
    ObjectGuid guid = object->GetGUID();
    bot->PrepareQuestMenu(guid);
    QuestMenu const& menu = bot->PlayerTalkClass->GetQuestMenu();
    if (menu.Empty())
        return false;

    for (uint8 idx = 0; idx < menu.GetMenuItemCount(); idx++)
    {
        QuestMenuItem const& item = menu.GetItem(idx);
        Quest const* quest = sObjectMgr->GetQuestTemplate(item.QuestId);
        if (!quest)
            continue;
        QuestStatus const& status = bot->GetQuestStatus(item.QuestId);
        if (status == QUEST_STATUS_COMPLETE && bot->CanRewardQuest(quest, 0, false))
        {
            return true;
        }
    }
    for (uint8 idx = 0; idx < menu.GetMenuItemCount(); idx++)
    {
        QuestMenuItem const& item = menu.GetItem(idx);
        Quest const* quest = sObjectMgr->GetQuestTemplate(item.QuestId);
        if (!quest)
            continue;

        QuestStatus const& status = bot->GetQuestStatus(item.QuestId);
        if (status == QUEST_STATUS_NONE && bot->CanTakeQuest(quest, false) && bot->CanAddQuest(quest, false) &&
            IsQuestWorthDoing(bot, quest) && IsQuestCapableDoing(bot, quest))
        {
            return true;
        }
    }
    return false;
}

bool IsQuestWorthDoing(Player* bot, Quest const* quest)
{
    bool isLowLevelQuest =
        bot->GetLevel() > (bot->GetQuestLevel(quest) + sWorld->getIntConfig(CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF));

    if (isLowLevelQuest)
        return false;

    if (quest->IsRepeatable())
        return false;

    if (quest->IsSeasonal())
        return false;

    return true;
}

bool IsQuestCapableDoing(Player* bot, Quest const* quest)
{
    bool highLevelQuest = bot->GetLevel() + QUEST_MAX_LEVELS_ABOVE_BOT < bot->GetQuestLevel(quest);
    if (highLevelQuest)
        return false;

    // Elite quest and dungeon quest etc
    if (quest->GetType() != 0)
        return false;

    // now we only capable of doing solo quests
    if (quest->GetSuggestedPlayers() >= 2)
        return false;

    return true;
}

ObjectGuid FindQuestGiver(PlayerbotAI* botAI, float distanceLimit)
{
    Player* bot = botAI->GetBot();
    GuidSet const& ignored = botAI->rpgInfo.ignoredQuestGivers;
    WorldObject* nearestObject = nullptr;

    for (char const* candidates : {"possible new rpg targets", "possible new rpg game objects"})
    {
        GuidVector const guids = botAI->GetAiObjectContext()->GetValue<GuidVector>(candidates)->Get();
        for (ObjectGuid const& guid : guids)
        {
            if (ignored.count(guid))
                continue;

            WorldObject* object = ObjectAccessor::GetWorldObject(*bot, guid);

            if (!object || !object->IsInWorld())
                continue;

            if (distanceLimit && bot->GetDistance(object) > distanceLimit)
                continue;

            if (CanInteractWithQuestGiver(bot, object) && HasQuestToAcceptOrReward(bot, object))
            {
                if (!nearestObject || bot->GetExactDist(nearestObject) > bot->GetExactDist(object))
                    nearestObject = object;
                break;
            }
        }
    }

    return nearestObject ? nearestObject->GetGUID() : ObjectGuid();
}

bool IsProfessionConversionSpell(uint32 spellId)
{
    return spellId == PROFESSION_MILLING_SPELL || spellId == PROFESSION_PROSPECTING_SPELL ||
           spellId == PROFESSION_DISENCHANT_SPELL;
}

uint32 GetProfessionConversionSkillLine(uint32 spellId)
{
    switch (spellId)
    {
        case PROFESSION_MILLING_SPELL:
            return SKILL_INSCRIPTION;
        case PROFESSION_PROSPECTING_SPELL:
            return SKILL_JEWELCRAFTING;
        case PROFESSION_DISENCHANT_SPELL:
            return SKILL_ENCHANTING;
        default:
            return 0;
    }
}

bool IsMillingInput(Player* bot, ItemTemplate const* proto, uint32 count)
{
    return bot && proto && bot->HasSpell(PROFESSION_MILLING_SPELL) &&
           proto->HasFlag(ITEM_FLAG_IS_MILLABLE) && count >= PROFESSION_CONVERSION_INPUT_COUNT &&
           proto->RequiredSkillRank <= bot->GetSkillValue(SKILL_INSCRIPTION);
}

bool IsProspectingInput(Player* bot, ItemTemplate const* proto, uint32 count)
{
    return bot && proto && bot->HasSpell(PROFESSION_PROSPECTING_SPELL) &&
           proto->HasFlag(ITEM_FLAG_IS_PROSPECTABLE) && count >= PROFESSION_CONVERSION_INPUT_COUNT &&
           proto->RequiredSkillRank <= bot->GetSkillValue(SKILL_JEWELCRAFTING);
}

bool IsDisenchantCandidate(Player* bot, ItemTemplate const* proto)
{
    return bot && proto && bot->HasSpell(PROFESSION_DISENCHANT_SPELL) && proto->DisenchantID &&
           (proto->Class == ITEM_CLASS_ARMOR || proto->Class == ITEM_CLASS_WEAPON) &&
           proto->Quality >= ITEM_QUALITY_UNCOMMON && proto->Quality <= ITEM_QUALITY_EPIC &&
           bot->HasSkill(SKILL_ENCHANTING) &&
           bot->GetSkillValue(SKILL_ENCHANTING) >= proto->RequiredDisenchantSkill;
}

bool IsDisenchantableItem(Player* bot, ItemTemplate const* proto, ItemUsage usage)
{
    if (!IsDisenchantCandidate(bot, proto))
        return false;

    // Off-class greens are NONE (BoE ones may be AH/VENDOR); those are safe to
    // destroy for materials. BAD_EQUIP is deliberately excluded: it marks a
    // stopgap upgrade the equip path may still wear.
    if (usage == ITEM_USAGE_QUEST || usage == ITEM_USAGE_GUILD_TASK || usage == ITEM_USAGE_EQUIP ||
        usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_USE || usage == ITEM_USAGE_SKILL ||
        usage == ITEM_USAGE_KEEP)
        return false;

    return usage == ITEM_USAGE_DISENCHANT || usage == ITEM_USAGE_NONE || usage == ITEM_USAGE_VENDOR ||
           usage == ITEM_USAGE_AH;
}

bool HasProfessionConversionInput(PlayerbotAI* botAI, uint32 spellId)
{
    if (!botAI)
        return false;

    Player* bot = botAI->GetBot();
    if (!bot || !IsProfessionConversionSpell(spellId))
        return false;

    auto matches = [&](ItemTemplate const* proto, uint32 count) -> bool
    {
        switch (spellId)
        {
            case PROFESSION_MILLING_SPELL:
                return IsMillingInput(bot, proto, count);
            case PROFESSION_PROSPECTING_SPELL:
                return IsProspectingInput(bot, proto, count);
            case PROFESSION_DISENCHANT_SPELL:
            {
                if (!IsDisenchantCandidate(bot, proto))
                    return false;

                ItemUsage const usage =
                    botAI->GetAiObjectContext()->GetValue<ItemUsage>("item usage", proto->ItemId)->Get();
                return IsDisenchantableItem(bot, proto, usage);
            }
            default:
                return false;
        }
    };

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (matches(item->GetTemplate(), item->GetCount()))
                return true;

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bot->GetItemByPos(bagSlot, slot))
                    if (matches(item->GetTemplate(), item->GetCount()))
                        return true;

    return false;
}

namespace
{
std::string Lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return char(std::tolower(character)); });
    return value;
}

std::string Trim(std::string value)
{
    auto const notSpace = [](unsigned char character) { return !std::isspace(character); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

bool IsGatheringProfession(uint32 skill)
{
    return skill == SKILL_HERBALISM || skill == SKILL_SKINNING || skill == SKILL_FISHING;
}
}  // namespace

uint32 ParseProfessionName(std::string const& name)
{
    std::string const lower = Lowercase(Trim(name));

    static std::map<std::string, uint32> const names = {
        {"alchemy", SKILL_ALCHEMY},           {"blacksmithing", SKILL_BLACKSMITHING},
        {"blacksmith", SKILL_BLACKSMITHING},  {"bs", SKILL_BLACKSMITHING},
        {"enchanting", SKILL_ENCHANTING},     {"enchanter", SKILL_ENCHANTING},
        {"engineering", SKILL_ENGINEERING},   {"engineer", SKILL_ENGINEERING},
        {"inscription", SKILL_INSCRIPTION},   {"jewelcrafting", SKILL_JEWELCRAFTING},
        {"jc", SKILL_JEWELCRAFTING},          {"leatherworking", SKILL_LEATHERWORKING},
        {"lw", SKILL_LEATHERWORKING},         {"mining", SKILL_MINING},
        {"tailoring", SKILL_TAILORING},       {"tailor", SKILL_TAILORING},
        {"cooking", SKILL_COOKING},           {"cook", SKILL_COOKING},
        {"first aid", SKILL_FIRST_AID},       {"firstaid", SKILL_FIRST_AID},
        {"first-aid", SKILL_FIRST_AID},       {"herbalism", SKILL_HERBALISM},
        {"skinning", SKILL_SKINNING},         {"fishing", SKILL_FISHING}};

    auto const it = names.find(lower);
    return it == names.end() ? 0 : it->second;
}

ProfessionCommand ParseProfessionCommand(std::string const& text)
{
    static std::string const doProfessions = "do professions";
    static std::string const doProfession = "do profession";

    ProfessionCommand result;
    if (text == doProfessions || text.rfind(doProfessions + " ", 0) == 0)
    {
        result.recognized = true;
        result.plural = true;
        if (text.size() > doProfessions.size())
            result.names = text.substr(doProfessions.size() + 1);
        return result;
    }

    if (text == doProfession || text.rfind(doProfession + " ", 0) == 0)
    {
        result.recognized = true;
        result.plural = false;
        if (text.size() > doProfession.size())
            result.names = text.substr(doProfession.size() + 1);
        return result;
    }

    std::string const candidate = text.rfind("do ", 0) == 0 ? text.substr(3) : text;
    if (!candidate.empty() && ParseProfessionName(candidate) != 0)
    {
        result.recognized = true;
        result.plural = false;
        result.names = candidate;
    }

    return result;
}

std::vector<uint32> GetKnownCraftProfessions(PlayerbotAI* botAI)
{
    static uint32 const craftProfessions[] = {
        SKILL_ALCHEMY,       SKILL_BLACKSMITHING,  SKILL_ENCHANTING, SKILL_ENGINEERING, SKILL_INSCRIPTION,
        SKILL_JEWELCRAFTING, SKILL_LEATHERWORKING, SKILL_MINING,     SKILL_TAILORING,   SKILL_COOKING,
        SKILL_FIRST_AID};

    std::vector<uint32> result;
    if (!botAI)
        return result;

    for (uint32 skill : craftProfessions)
        if (botAI->HasSkill(SkillType(skill)))
            result.push_back(skill);

    return result;
}

ProfessionSelection ParseProfessionSelection(PlayerbotAI* botAI, std::string const& text)
{
    ProfessionSelection result;
    if (!botAI)
    {
        result.error = ProfessionParseError::NoProfessions;
        return result;
    }

    bool const allProfessions = Trim(text).empty();
    std::string normalized = Lowercase(text);
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::replace(normalized.begin(), normalized.end(), ';', ' ');

    std::istringstream stream(normalized);
    std::string token;
    while (stream >> token)
    {
        if (token == "first")
        {
            std::string next;
            if (!(stream >> next) || (next != "aid" && next != "firstaid"))
            {
                result.error = ProfessionParseError::Unknown;
                result.detail = token;
                return result;
            }
            token = "first aid";
        }

        uint32 const skill = ParseProfessionName(token);
        if (!skill)
        {
            result.error = ProfessionParseError::Unknown;
            result.detail = token;
            return result;
        }

        if (IsGatheringProfession(skill))
        {
            result.error = ProfessionParseError::Gathering;
            result.detail = GetProfessionName(skill);
            return result;
        }

        if (!botAI->HasSkill(SkillType(skill)))
        {
            result.error = ProfessionParseError::NotLearned;
            result.detail = GetProfessionName(skill);
            return result;
        }

        if (std::find(result.skills.begin(), result.skills.end(), skill) == result.skills.end())
            result.skills.push_back(skill);
    }

    if (result.skills.empty())
    {
        if (!allProfessions)
        {
            result.error = ProfessionParseError::NoProfessions;
            return result;
        }

        result.skills = GetKnownCraftProfessions(botAI);
        if (result.skills.empty())
        {
            result.error = ProfessionParseError::NoProfessions;
            return result;
        }
    }

    return result;
}

std::string GetProfessionName(uint32 skill)
{
    SkillLineEntry const* entry = sSkillLineStore.LookupEntry(skill);
    return entry ? entry->name[0] : std::to_string(skill);
}
}  // namespace NewRpgHelpers
