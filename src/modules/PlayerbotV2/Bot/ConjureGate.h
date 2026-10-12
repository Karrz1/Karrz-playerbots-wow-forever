// Gates for the warlock/mage conjure upkeep: pure functions, so the rules that
// use them and the unit tests read the same numbers.
#pragma once

#include <cstdint>

namespace Playerbot::V2 {

// A mana spell's cost: a flat amount, or a percent of the caster's base mana.
constexpr int32_t ConjureManaCost(int32_t flat_cost, float pct_of_base, int32_t base_mana)
{
    int32_t const pct_cost = pct_of_base > 0.0f ? static_cast<int32_t>(pct_of_base * float(base_mana) / 100.0f) : 0;
    return flat_cost > pct_cost ? flat_cost : pct_cost;
}

// Conjures are 3 s hard casts out of combat: never in combat (or the moment
// after one), and only with the mana to pay for them. A refusal is paced by the
// caller's per-spell retry lockout.
constexpr bool MayConjure(bool in_combat, bool recently_in_combat, int32_t mana, int32_t cost)
{
    return !in_combat && !recently_in_combat && mana >= cost;
}

} // namespace Playerbot::V2
