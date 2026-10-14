/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_INTERACTPROFILES_H
#define PLAYERBOTS_INTERACTPROFILES_H

#include "InteractTypes.h"
#include <memory>

class Player;

// Target selection for one interaction kind. The executor creates one per candidate. Execution
// (the actual item use) stays in the action for now and moves in here when the shared use helper
// lands (see the interact plan, phase 2S).
class InteractProfile
{
public:
    static std::unique_ptr<InteractProfile> Create(InteractKind kind);

    virtual ~InteractProfile() = default;

    virtual char const* Name() const = 0;

    // Resolves the best target for the candidate within range. False when none is available.
    virtual bool SelectTarget(Player* bot, QuestItemCandidate const& candidate, float range,
                              InteractTarget& out) const = 0;
};

class SelfUseProfile : public InteractProfile
{
public:
    char const* Name() const override { return "self"; }
    bool SelectTarget(Player* bot, QuestItemCandidate const& candidate, float range,
                      InteractTarget& out) const override;
};

class UseOnCreatureProfile : public InteractProfile
{
public:
    char const* Name() const override { return "creature"; }
    bool SelectTarget(Player* bot, QuestItemCandidate const& candidate, float range,
                      InteractTarget& out) const override;
};

class UseOnGameObjectProfile : public InteractProfile
{
public:
    char const* Name() const override { return "gameobject"; }
    bool SelectTarget(Player* bot, QuestItemCandidate const& candidate, float range,
                      InteractTarget& out) const override;
};

class UseAtLocationProfile : public InteractProfile
{
public:
    char const* Name() const override { return "location"; }
    bool SelectTarget(Player* bot, QuestItemCandidate const& candidate, float range,
                      InteractTarget& out) const override;
};

#endif
