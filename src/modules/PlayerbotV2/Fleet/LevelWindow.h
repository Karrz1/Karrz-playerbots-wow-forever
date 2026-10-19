#pragma once
// Level window of the bots a world anchor (a player) is given: the zone's own questing range, kept within a few levels of the
// player (close enough to invite as a group), so the zone he stands in is busy with his level. Pure, tested in tests/game/ClassicLevelWindow.cpp.
#include <algorithm>

namespace Playerbot::V2::Fleet {

struct LevelWindow { int lo = 1; int hi = 1; };

// zone_known: the zone has a level range (zlo..zhi); a capital has none and gets the wide spread min_level..max_level.
inline LevelWindow AnchorLevelWindow(int anchor_level, bool zone_known, int zlo, int zhi, int min_level, int max_level, int spread = 4, int zone_spread = 1)
{
    if (!zone_known)
        return { min_level, std::max(min_level, max_level) };
    int const zone_lo = std::max(1, zlo - zone_spread), zone_hi = std::min(max_level, zhi + zone_spread);
    int lo = std::max(1, anchor_level - spread), hi = std::min(max_level, anchor_level + spread);
    // The zone's range leaning toward the player's level. If he is far outside it (a level 1 sent to a level 30 zone): the nearest part
    // of the zone's range, so the bots still suit the place.
    lo = std::max(lo, zone_lo);
    hi = std::min(hi, zone_hi);
    if (lo > hi)
    {
        lo = std::min(std::max(anchor_level, zone_lo), zone_hi);
        hi = lo;
    }
    return { lo, hi };
}

} // namespace Playerbot::V2::Fleet
