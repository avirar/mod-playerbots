#include "RaidUlduarStrategy.h"

#include "RaidUlduarMultipliers.h"

void RaidUlduarStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Flame Leviathan
    triggers.push_back(new TriggerNode(
        "flame leviathan vehicle near",
        NextAction::array(0, new NextAction("flame leviathan enter vehicle", ACTION_RAID + 2), nullptr)));

    triggers.push_back(new TriggerNode(
        "flame leviathan on vehicle",
        NextAction::array(0, new NextAction("flame leviathan vehicle", ACTION_RAID + 1), nullptr)));

    // Ignis the Furnace Master
    triggers.push_back(new TriggerNode(
        "ignis move construct to scorched ground",
        NextAction::array(0, new NextAction("ignis move construct to scorched ground", ACTION_RAID), nullptr)));

    triggers.push_back(new TriggerNode(
        "ignis move molten construct to water",
        NextAction::array(0, new NextAction("ignis move molten construct to water", ACTION_RAID + 1), nullptr)));
    
}

void RaidUlduarStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new FlameLeviathanMultiplier(botAI));
}
