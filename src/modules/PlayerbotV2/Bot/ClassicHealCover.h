// ClassicHealCover - when the heals already on their way cover a patient, so another healer sends none.
//
// MC Garr 2026-10-10: a raid member at 64 % with 1406 health missing, 1692 of swings coming and 2280 of heals in flight still got a
// fifth heal, because the check compared the heals in flight with missing + the whole of the swings. The swings are the next hit or
// two; the heal lands after them, so half of them count. Pure: no game types, tested in tests/game/ClassicHealCover.cpp.

#pragma once

#include <algorithm>
#include <cstdint>

namespace Playerbot::Combat::ClassicHeal {

constexpr int32_t kHealCriticalPct      = 25;     // a patient under this asks for kHealCriticalCover times its deficit in flight
constexpr float   kHealCriticalCover    = 1.5f;
constexpr float   kHealTankTroubleCover = 1.25f;

// The health a heal landing in a moment has to restore: what is missing now plus half the swings still coming.
inline int64_t HealDeficitOf(int64_t max_hp, int64_t hp, int64_t incoming)
{
    return std::max<int64_t>(max_hp - hp, 0) + incoming / 2;
}

// The heals in flight (`inflight`) cover the deficit times `margin` (a critical patient: at least kHealCriticalCover).
inline bool CoveredByHealsInFlight(int64_t max_hp, int64_t hp, int64_t incoming, int64_t inflight, int32_t actual_pct, float margin)
{
    if (inflight <= 0 || max_hp <= 0)
        return false;
    float const need = float(HealDeficitOf(max_hp, hp, incoming)) * (actual_pct < kHealCriticalPct ? std::max(margin, kHealCriticalCover) : margin);
    return float(inflight) >= need;
}

// A warlock's Life Tap is damage he did to himself on purpose: no heal for it until he is under kLifeTapHealCeilingPct, unless something
// is hitting him. Pure: tested in tests/game/ClassicHealCover.cpp.
constexpr int32_t kLifeTapHealCeilingPct = 60;

inline bool LifeTapSkip(bool is_warlock, bool tank, int32_t actual_pct, int64_t incoming)
{
    return is_warlock && !tank && incoming <= 0 && actual_pct >= kLifeTapHealCeilingPct;
}

// The raid subgroups bit mask (RaidHealGroups.h) covers `subgroup`.
inline bool HealGroupHas(uint16_t mask, uint8_t subgroup)
{
    return subgroup < 16 && (mask & (1u << subgroup)) != 0;
}

}  // namespace Playerbot::Combat::ClassicHeal
