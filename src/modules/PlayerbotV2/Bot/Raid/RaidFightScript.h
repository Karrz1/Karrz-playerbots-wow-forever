// RaidFightScript - a boss script that owns the DPS side of one fight.
//
// Not a second rule engine: the raid rule tables, the tank assignment (PveGroupCoordinator::PlanRaidTanks) and the healing stay
// exactly as they are. A fight script only decides two things for the DPS bots of its boss, and switches off the rules that would
// decide them a second time:
//   1. WHICH enemy every DPS bot attacks: the kill order, one enemy at a time, the same one for everybody.
//   2. WHEN the DPS start: a fixed wait after the fight begins, then no threat holds at all.
// The owner's skull (else cross) always wins over the script's pick.
// Pure functions only (no world access), so tests/game/ClassicRaidFightScript.cpp covers them.

#pragma once

#include "../ThreatLimits.h"

#include <cstddef>
#include <cstdint>

namespace Playerbot::Raid::Script {

struct Fight
{
    uint32_t map;
    uint32_t boss;                 // creature entry that names the fight
    uint32_t killOrder[4];         // creature entries, first dies first, 0 ends the list
    uint32_t startWaitMs;          // DPS wait this long from the first engaged enemy, then attack
    char const* name;              // log only
};

// Lucifron (MC 409): the two Flamewaker Protectors first, then Lucifron. The start wait is the raid's one boss opening wait (Playerbot::kRaidBossOpenWaitMs, 8 s: the main tank takes a while to run in); there is no threat cap after
// that (a bot that pulls a Protector is taunted back by the loose-mob rule).
inline constexpr Fight kFights[] = {
    { 409, 12118, { 12119, 12118, 0, 0 }, kRaidBossOpenWaitMs, "Lucifron" },
};

// The fight an engaged enemy belongs to on this map (nullptr: none).
inline Fight const* FightFor(uint32_t map, uint32_t entry)
{
    for (Fight const& f : kFights)
    {
        if (f.map != map)
            continue;
        if (f.boss == entry)
            return &f;
        for (uint32_t e : f.killOrder)
            if (e && e == entry)
                return &f;
    }
    return nullptr;
}

// Where an entry stands in the kill order (0 = first); a large number when it is not on the list.
inline size_t RankOf(Fight const& f, uint32_t entry)
{
    for (size_t i = 0; i < 4; ++i)
        if (f.killOrder[i] && f.killOrder[i] == entry)
            return i;
    return 99;
}

struct Enemy
{
    uint64_t guid = 0;
    uint32_t entry = 0;
    float hpPct = 100.f;
    bool eligible = false;     // alive, engaged, not crowd-controlled or reflecting, not forbidden for this bot
};

// The enemy every DPS bot attacks now, 0 = none (the generic rules pick).
//   ownerMark: the owner's skull or cross, when it is an eligible enemy it wins.
//   previous : last pick, kept while it is still eligible and still the best rank (no flip-flop between two equal Protectors).
// Lowest rank first; inside a rank the most hurt one (finish what was started), then the lowest guid.
inline uint64_t PickFocus(Fight const& f, Enemy const* enemies, size_t n, uint64_t ownerMark, uint64_t previous)
{
    if (ownerMark)
        for (size_t i = 0; i < n; ++i)
            if (enemies[i].eligible && enemies[i].guid == ownerMark)
                return ownerMark;
    size_t bestRank = 99;
    for (size_t i = 0; i < n; ++i)
        if (enemies[i].eligible)
            bestRank = bestRank < RankOf(f, enemies[i].entry) ? bestRank : RankOf(f, enemies[i].entry);
    if (bestRank == 99)
        return 0;
    if (previous)
        for (size_t i = 0; i < n; ++i)
            if (enemies[i].eligible && enemies[i].guid == previous && RankOf(f, enemies[i].entry) == bestRank)
                return previous;
    Enemy const* best = nullptr;
    for (size_t i = 0; i < n; ++i)
    {
        Enemy const& e = enemies[i];
        if (!e.eligible || RankOf(f, e.entry) != bestRank)
            continue;
        if (!best || e.hpPct < best->hpPct || (e.hpPct == best->hpPct && e.guid < best->guid))
            best = &e;
    }
    return best ? best->guid : 0;
}

// True while the DPS still wait for the tank to get in: from the first engaged enemy (sinceMs, 0 = none yet) for waitMs.
inline bool StartWait(uint32_t sinceMs, uint32_t nowMs, uint32_t waitMs)
{
    return sinceMs != 0 && nowMs >= sinceMs && nowMs - sinceMs < waitMs;
}

} // namespace Playerbot::Raid::Script
