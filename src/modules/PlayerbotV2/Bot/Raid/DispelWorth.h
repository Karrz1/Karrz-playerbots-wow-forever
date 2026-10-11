// Is a raid dispel worth the mana? (RaidBrain ElectDispels.) Pure so the thresholds are tested.
//
// MC Garr 2026-10-10 (mana audit): Cleanse x170 and Dispel Magic x160 in one fight, more casts than every heal together, on the
// Firesworn's Immolate. Healers were out of mana at 227 s. A dispel costs about what a big heal does, so a debuff that only ticks
// for a little is not worth one.

#pragma once

namespace Playerbot::Raid {

// A non-urgent dispel needs a healer with at least this much mana left; a mage or a non-healing druid has no heals to save it for.
constexpr int kNonUrgentDispelHealerManaPct = 40;

// A non-urgent dispel rule hands out at most this many orders per plan: an aura on forty members is not forty casts in a tick.
constexpr unsigned kNonUrgentDispelOrdersPerPlan = 2;

// A rule with hurtBelowPct > 0 (RaidEncounters.h Dispel) is only cast once the carrier is at or under that health: the debuff then
// really costs the raid something. 0 = always (curses and other debuffs that do harm at full health). Urgent rules always.
inline bool DispelWorthIt(bool urgent, int hurtBelowPct, int carrierHpPct)
{
    return urgent || hurtBelowPct <= 0 || carrierHpPct <= hurtBelowPct;
}

inline bool HealerMayDispel(bool urgent, bool healer, int manaPct)
{
    return urgent || !healer || manaPct >= kNonUrgentDispelHealerManaPct;
}

} // namespace Playerbot::Raid
