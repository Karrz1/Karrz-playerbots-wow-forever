// WakeSelect - which sleeping residents wake for a player: the pure rule behind WorldBotSpawner::DriveWake.
//
// A sleeper wakes only when it is standing between MinDist and MaxDist of the player (not beside him: it would pop into view),
// is of the wanted faction, and - when the player is in a capital - stands in the capital's own zone, because the target of a
// capital counts only bots of that zone (WorldBotSpawner::CountAmbientNear). Without the zone rule the countryside around a
// city woke up as well, did not count toward the city's target, and the spawner kept waking until nothing was left (Karrz,
// Stormwind, 2026-10-09: 280 awake for CityBots 250). At most `want` wake.
// Pure: no game types, tested in tests/game/ClassicWakeSelect.cpp.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Playerbot::V2::Fleet {

struct WakeCandidate
{
    uint64_t guid = 0;
    float    dist = 0.0f;      // 2D distance from the player
    uint32_t zone = 0;         // zone saved at logout
    bool     faction_ok = true;// passed the FriendlyPct roll
    bool     wet = false;      // its saved position has water over it (logged out in a canal): it must not wake there
};

struct WakeRule
{
    float    min_dist = 60.0f;
    float    max_dist = 450.0f;
    bool     capital = false;  // the player stands in a capital: only sleepers of `anchor_zone` count
    uint32_t anchor_zone = 0;
};

inline bool WakeEligible(WakeCandidate const& c, WakeRule const& r)
{
    if (!c.faction_ok || c.wet) return false;
    if (c.dist < r.min_dist || c.dist > r.max_dist) return false;
    if (r.capital && c.zone != r.anchor_zone) return false;
    return true;
}

// Candidates in the order given; stops at `want`.
inline std::vector<uint64_t> SelectWake(std::vector<WakeCandidate> const& in, WakeRule const& r, uint32_t want)
{
    std::vector<uint64_t> out;
    for (WakeCandidate const& c : in)
    {
        if (out.size() >= want) break;
        if (WakeEligible(c, r)) out.push_back(c.guid);
    }
    return out;
}

} // namespace Playerbot::V2::Fleet
