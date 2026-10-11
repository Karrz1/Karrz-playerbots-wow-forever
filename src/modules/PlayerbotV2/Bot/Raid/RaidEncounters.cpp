#include "RaidEncounters.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>

namespace Playerbot::Raid {

namespace {

std::map<uint32_t, std::unique_ptr<RaidRules>>& Registry()
{
    static std::map<uint32_t, std::unique_ptr<RaidRules>> rules;
    static std::once_flag once;
    std::call_once(once, []
    {
        auto molten = std::make_unique<RaidRules>();
        molten->name = "Molten Core";
        RegisterMoltenCoreMovement(*molten);
        RegisterMoltenCore(*molten);
        rules[409] = std::move(molten);

        auto blackwing = std::make_unique<RaidRules>();
        blackwing->name = "Blackwing Lair";
        RegisterBlackwingMovement(*blackwing);
        RegisterBlackwingLair(*blackwing);
        rules[469] = std::move(blackwing);

        auto onyxia = std::make_unique<RaidRules>();
        onyxia->name = "Onyxia's Lair";
        RegisterOnyxia(*onyxia);
        // Onyxia's breath comes out of the front on the ground phases.
        onyxia->avoidFrontal.push_back({ 10184, 40.0f, 45.0f, Who::NonTank });
        // Tail Sweep 15847 behind her (classic_boss_onyxia.cpp): melee stand at her sides.
        onyxia->avoidFrontal.push_back({ 10184, 20.0f, 45.0f, Who::NonTank, 180.0f });
        // Phase 3 Bellowing Roar 18431: AoE fear, dispelled off whoever a dispeller can reach.
        onyxia->dispel.push_back({ 18431, DispelKind::Magic });
        rules[249] = std::move(onyxia);

        auto aq40 = std::make_unique<RaidRules>();
        aq40->name = "Temple of Ahn'Qiraj";
        RegisterTempleOfAhnQiraj(*aq40);
        rules[531] = std::move(aq40);

        auto naxx = std::make_unique<RaidRules>();
        naxx->name = "Naxxramas";
        RegisterNaxxramas(*naxx);
        rules[533] = std::move(naxx);
    });
    return rules;
}

} // namespace

RaidRules const* RulesForMap(uint32_t mapId)
{
    auto& registry = Registry();
    auto it = registry.find(mapId);
    return it == registry.end() ? nullptr : it->second.get();
}

bool OwnsAura(uint32_t mapId, uint32_t aura)
{
    RaidRules const* rules = RulesForMap(mapId);
    if (!rules)
        return false;
    for (Dispel const& rule : rules->dispel)
        if (rule.aura == aura)
            return true;
    for (ControlledMember const& rule : rules->controlledMember)
        if (rule.aura == aura)
            return true;
    return std::find(rules->noDispel.begin(), rules->noDispel.end(), aura) != rules->noDispel.end();
}

bool InterruptFirst(uint32_t mapId, uint32_t spell)
{
    RaidRules const* rules = RulesForMap(mapId);
    return rules && std::find(rules->interruptFirst.begin(), rules->interruptFirst.end(), spell) != rules->interruptFirst.end();
}

bool NoTank(uint32_t mapId, uint32_t entry)
{
    RaidRules const* rules = RulesForMap(mapId);
    return rules && std::find(rules->noTank.begin(), rules->noTank.end(), entry) != rules->noTank.end();
}

int PriorityFor(RaidRules const& rules, uint32_t entry)
{
    for (Priority const& rule : rules.priority)
        if (rule.enemy == entry)
            return rule.value;
    return 1;
}

std::vector<uint32_t> KillOrder(uint32_t mapId)
{
    std::vector<uint32_t> order;
    RaidRules const* rules = RulesForMap(mapId);
    if (!rules)
        return order;
    std::vector<Priority> ranked;
    for (Priority const& rule : rules->priority)
        if (rule.value >= 2)
            ranked.push_back(rule);
    std::stable_sort(ranked.begin(), ranked.end(), [](Priority const& a, Priority const& b) { return a.value > b.value; });
    for (Priority const& rule : ranked)
        order.push_back(rule.enemy);
    return order;
}

} // namespace Playerbot::Raid
