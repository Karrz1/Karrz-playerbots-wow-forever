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
inline bool CoveredByHealsInFlight(int64_t max_hp, int64_t hp, int64_t incoming, int64_t inflight, int32_t actual_pct, float margin, bool in_fight = true)
{
    if (inflight <= 0 || max_hp <= 0)
        return false;
    // The critical margin is for a fight: heals land after more swings and some miss their moment. With the fight over (a member rezzed
    // afterwards) the deficit is exact, and three healers started a heal on a rezzed warlock at 14 % (MC Magmadar 2026-10-10: 3984 in
    // flight for a deficit of about 2700, the third heal was pure mana).
    bool const critical = actual_pct < kHealCriticalPct && in_fight;
    float const need = float(HealDeficitOf(max_hp, hp, incoming)) * (critical ? std::max(margin, kHealCriticalCover) : margin);
    return float(inflight) >= need;
}

// Raid HoTs (Renew, Rejuvenation) on a member who is not a tank: only when the member is really hurt and the heals already on their way
// do not cover it. Magmadar 2026-10-10: 56 Renews and 27 Rejuvenations in the first minute, most on members at 70-85 % that a single
// direct heal, or nothing, would have covered. Outside a raid the old 85 % line stays.
constexpr int32_t kRaidHotMaxPct    = 70;
constexpr int32_t kPartyHotMaxPct   = 85;

inline bool HotWorthCasting(bool raid, int32_t actual_pct, int64_t deficit, int64_t inflight)
{
    if (!raid)
        return actual_pct <= kPartyHotMaxPct;
    return actual_pct <= kRaidHotMaxPct && inflight < deficit;
}

// Power Word: Shield is claimed per patient: Weakened Soul keeps every other priest's shield off for 15 s, but the debuff is only seen
// after the first shield lands, so several priests started one on the same tank in the same moment (43 shield casts in a minute).
constexpr uint32_t kShieldClaimMs = 15000;

inline bool ShieldClaimFresh(uint32_t claimed_at_ms, uint32_t now_ms)
{
    return claimed_at_ms != 0 && int32_t(now_ms - claimed_at_ms) < int32_t(kShieldClaimMs);
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

// A refused cast releases its healer's claim only when that claim is from the same patient and this tick (a claim older than the
// window is a heal still running, which an unrelated refused cast must not drop).
constexpr uint32_t kClaimReleaseWindowMs = 1000;

inline bool ClaimReleasable(bool any_patient, bool same_patient, uint32_t claim_start_ms, uint32_t now_ms)
{
    return (any_patient || same_patient) && uint32_t(now_ms - claim_start_ms) <= kClaimReleaseWindowMs;
}

}  // namespace Playerbot::Combat::ClassicHeal
