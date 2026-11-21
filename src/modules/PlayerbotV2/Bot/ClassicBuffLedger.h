// ClassicBuffLedger - how long a raid buff a bot has cast itself is trusted without the target's aura list.
//
// MC Magmadar 2026-10-10: the paladins recast Greater Blessing of Wisdom (39 times) and of Salvation (37 + 37 single ones) every 10-15 s
// at ~190 mana each, because the group snapshot showed no tracked buff on any target (the log's "target's tracked buffs []"). The
// 30 s retry lockout of ActionKind::ClassUpkeep is shorter than the buff, so a buff the snapshot cannot see is recast every 30 s per
// class and per member. A cast the bot made itself is remembered for a part of the buff's length instead; the snapshot's own answer
// (a recorded aura with time left) still decides first. Pure: tested in tests/game/ClassicBuffLedger.cpp.

#pragma once

#include <cstdint>

namespace Playerbot::Classic {

// ClassicUpkeep.cpp pick keys carry a kind in bits 48+: 1 Greater Blessing of a class, 2 a single blessing on a member, 3 / 4 a
// group / member class buff (Prayer, Gift of the Wild), 5 a self or paced cast.
constexpr uint32_t kGreaterBlessingTrustMs = 6u * 60u * 1000u;   // lasts 15 min
constexpr uint32_t kSingleBlessingTrustMs  = 3u * 60u * 1000u;   // lasts 5 min
constexpr uint32_t kClassBuffTrustMs       = 6u * 60u * 1000u;   // 30 min and up
constexpr uint32_t kAreaBuffTrustMs        = 15u * 1000u;        // a caster-centred party / raid cast: only the snapshot's lag is covered, the aura list decides after

inline uint32_t BuffTrustMs(uint64_t key)
{
    switch (key >> 48)
    {
        case 1: return kGreaterBlessingTrustMs;
        case 2: return kSingleBlessingTrustMs;
        case 3:
        case 4: return kClassBuffTrustMs;
        case 5: return kSingleBlessingTrustMs;
        case 6: return kAreaBuffTrustMs;
        default: return 0;
    }
}

// The core refused the cast the bot noted at `cast_ms` (same spell, same target) at `refused_ms`: the buff never went out, so it is not
// trusted. Naxx Razuvious r2 af54 (2026-11-20 18:56:46): a Blessing of Wisdom refused NOT_READY (the global cooldown of the blessing before
// it) was trusted for its three minutes; the raid's attack waited 83 s on that one member until the run gave up. A refusal belongs to the
// cast only when it comes within kRefusalWindowMs after it (the executor runs the intent within a world update or two).
constexpr uint32_t kRefusalWindowMs = 10u * 1000u;
inline bool CastRefusedAfter(uint32_t cast_ms, uint32_t refused_ms)
{
    if (cast_ms == 0 || refused_ms == 0)
        return false;
    int32_t const after = int32_t(refused_ms - cast_ms);
    return after >= 0 && after < int32_t(kRefusalWindowMs);
}

// The bot cast `key` at `cast_ms` (0 = never): still trusted at `now_ms`?
inline bool BuffCastTrusted(uint64_t key, uint32_t cast_ms, uint32_t now_ms)
{
    uint32_t const trust = BuffTrustMs(key);
    return cast_ms != 0 && trust != 0 && int32_t(now_ms - cast_ms) < int32_t(trust);
}

}  // namespace Playerbot::Classic
