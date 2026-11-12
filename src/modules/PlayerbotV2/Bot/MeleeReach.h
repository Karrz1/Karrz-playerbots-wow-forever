// MeleeReach - how far from a victim's centre a melee bot already hits it (State_InCombat melee gap-close).
//
// Pure function on plain values, so the rule is unit-tested without a server.
//
// The server's own test (Unit::IsWithinMeleeRange / GetMeleeRange): 3D centre distance up to both combat reaches plus 4/3 y, never
// less than the nominal 5 y. The gap-close used a flat 5 y, so a bot swinging at a big creature from 6-8 y (a dragon, a giant, a
// golem) counted as "not in melee": it kept walking at the centre, ran into the stuck-chase cap and dropped its target, or stopped
// on the companion leash and stood (UBRS 2026-10-27, the first pull through the door: the rogue stood for the whole first
// Rage Talon dragon and joined on the second).

#pragma once

#include <algorithm>

namespace Playerbot::MeleeReach {

constexpr float kNominalYd = 5.0f;                 // NOMINAL_MELEE_RANGE
constexpr float kPlayerCombatReachYd = 1.5f;       // DEFAULT_PLAYER_COMBAT_REACH, every bot is a player
constexpr float kMarginYd = 0.5f;                  // stay this much inside the server's limit: positions move between snapshots

// Centre distance at which the bot is in melee of a victim with this combat reach.
inline float Of(float victim_combat_reach)
{
    return std::max(kNominalYd, kPlayerCombatReachYd + std::max(0.0f, victim_combat_reach) + 4.0f / 3.0f - kMarginYd);
}

inline bool InReach(float dist2, float victim_combat_reach)
{
    float const r = Of(victim_combat_reach);
    return dist2 <= r * r;
}

} // namespace Playerbot::MeleeReach
