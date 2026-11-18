// ClassicHealCover - when the heals already on their way cover a patient, so another healer sends none.
//
// MC Garr 2026-10-10: a raid member at 64 % with 1406 health missing, 1692 of swings coming and 2280 of heals in flight still got a
// fifth heal, because the check compared the heals in flight with missing + the whole of the swings. The swings are the next hit or
// two; the heal lands after them, so half of them count. Pure: no game types, tested in tests/game/ClassicHealCover.cpp.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Playerbot::Combat::ClassicHeal {

constexpr int32_t kHealCriticalPct      = 25;     // a patient under this asks for kHealCriticalCover times its deficit in flight
constexpr float   kHealCriticalCover    = 1.5f;
constexpr float   kHealTankTroubleCover = 1.25f;

// Damage a raid member took per second, measured from the hits that landed (Group/GroupSnapshotBuilder.cpp NoteDamageTaken) and decayed
// with kDamageRateTauMs, so it follows a change of target within a few seconds. The swings of IncomingMelee only see the mob whose victim
// the member is: Patchwerk's Hateful Strike, a breath or a spell volley never show there. Naxx Patchwerk 1115b: the off tank took Hateful
// Strikes at 63 % with no swing on him, one Greater Heal (4058 in flight, 2.4 s to land) "covered" his 3880 missing, eight healers left
// him to it, and he went on dropping while it cast.
constexpr float kDamageRateTauMs = 3000.0f;

struct DamageRate
{
    float    per_s = 0.0f;   // damage per second as of at_ms
    uint32_t at_ms = 0;      // 0: no hit yet
};

inline float DamageRateAt(DamageRate const& r, uint32_t now_ms)
{
    if (!r.at_ms || r.per_s <= 0.0f)
        return 0.0f;
    int32_t const dt = std::max<int32_t>(int32_t(now_ms - r.at_ms), 0);
    return r.per_s * std::exp(-float(dt) / kDamageRateTauMs);
}

inline void DamageRateAdd(DamageRate& r, int64_t amount, uint32_t now_ms)
{
    if (amount <= 0)
        return;
    r.per_s = DamageRateAt(r, now_ms) + float(amount) * 1000.0f / kDamageRateTauMs;
    r.at_ms = now_ms ? now_ms : 1u;
}

// The largest single hit a raid member took in the last kSpikeWindowMs (Group/GroupSnapshotBuilder.cpp NoteDamageTaken). The rate above
// smooths a boss's spike into seconds of average damage, and a heal "covered" a soaker for the average while the next hit alone was most
// of its health: Patchwerk's Hateful Strike, 9-11.7k on 10.9k soakers once a second among three, and the healers left each soaker to the
// heals in flight for a deficit counted from the rate (naxx_patchwerk 1116 rerun: 147 "leaves ... (tank) to heals in flight", 57
// stopcasts on tanks at 95%+ just before the next strike, every soaker dead by 55 s). The heals in flight have to cover the next such
// hit as well.
constexpr uint32_t kSpikeWindowMs = 10000;

struct DamageSpike
{
    int64_t  amount = 0;
    uint32_t at_ms = 0;   // 0: no hit yet
};

inline int64_t DamageSpikeAt(DamageSpike const& s, uint32_t now_ms)
{
    return s.at_ms && int32_t(now_ms - s.at_ms) <= int32_t(kSpikeWindowMs) ? s.amount : 0;
}

inline void DamageSpikeAdd(DamageSpike& s, int64_t amount, uint32_t now_ms)
{
    if (amount <= 0)
        return;
    if (amount >= DamageSpikeAt(s, now_ms))
    {
        s.amount = amount;
        s.at_ms = now_ms ? now_ms : 1u;
    }
}

// The damage the measured rate brings before a heal `land_in_ms` away lands.
inline int64_t DamageUntilLand(float per_s, int32_t land_in_ms)
{
    return per_s > 0.0f && land_in_ms > 0 ? int64_t(per_s * float(land_in_ms) / 1000.0f) : 0;
}

// The health a heal landing in a moment has to restore: what is missing now plus what comes before it lands, which is half the swings
// still coming or, when more, `until_land` (the measured damage rate times the time to the first heal in flight, DamageUntilLand). The
// two are the same damage seen two ways, so the larger counts, not their sum.
inline int64_t HealDeficitOf(int64_t max_hp, int64_t hp, int64_t incoming, int64_t until_land = 0)
{
    return std::max<int64_t>(max_hp - hp, 0) + std::max<int64_t>(incoming / 2, until_land);
}

// The heals in flight (`inflight`) cover the deficit times `margin` (a critical patient: at least kHealCriticalCover).
inline bool CoveredByHealsInFlight(int64_t max_hp, int64_t hp, int64_t incoming, int64_t inflight, int32_t actual_pct, float margin, bool in_fight = true,
                                   int64_t until_land = 0)
{
    if (inflight <= 0 || max_hp <= 0)
        return false;
    // The critical margin is for a fight: heals land after more swings and some miss their moment. With the fight over (a member rezzed
    // afterwards) the deficit is exact, and three healers started a heal on a rezzed warlock at 14 % (MC Magmadar 2026-10-10: 3984 in
    // flight for a deficit of about 2700, the third heal was pure mana).
    bool const critical = actual_pct < kHealCriticalPct && in_fight;
    float const need = float(HealDeficitOf(max_hp, hp, incoming, until_land)) * (critical ? std::max(margin, kHealCriticalCover) : margin);
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
