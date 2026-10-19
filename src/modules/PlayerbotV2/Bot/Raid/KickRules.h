// KickRules - the pure part of the PvE coordinator's kick election (PveGroupCoordinator::PlanInterrupts): which cast the ready kickers go on
// first, and which kicker goes first on it. No world access, so tests/game/ClassicRaidFightScript.cpp covers it.

#pragma once

#include <cstddef>
#include <cstdint>

namespace Playerbot::Raid::Kicks {

// One interruptible cast the election saw. order: its place on the map's interruptFirst list (RaidEncounters.h InterruptOrder), the
// not-listed value for every other cast.
struct Cast
{
    size_t   order;
    int64_t  remainingMs;
    uint64_t guid;
};

// Listed casts first, in the list's order (Dark Mending before Shadow Bolt: a Healer's heal is never left to the late backups while
// the ready kickers go on a bolt); then the cast nearest to landing; then the lowest guid, so two plans agree.
inline bool CastBefore(Cast const& a, Cast const& b)
{
    if (a.order != b.order) return a.order < b.order;
    if (a.remainingMs != b.remainingMs) return a.remainingMs < b.remainingMs;
    return a.guid < b.guid;
}

// One kicker in reach of a cast. busy: in the middle of its own cast (a mage's Frostbolt), so it cannot kick before that finishes.
struct Kicker
{
    bool     busy;
    float    dist;
    uint64_t low;
};

// The free kickers first (rank 0 kicks on sight, which a bot mid-cast cannot: Majordomo fights showed most kicks landed
// in the last 550 ms by the backups), then the nearest, then the lowest guid.
inline bool KickerBefore(Kicker const& a, Kicker const& b)
{
    if (a.busy != b.busy) return !a.busy;
    if (a.dist != b.dist) return a.dist < b.dist;
    return a.low < b.low;
}

} // namespace Playerbot::Raid::Kicks
