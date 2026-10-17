// TankMarkSwap - the automatic tank markers when one tank takes a boss over from another (Raid::ElectRetaunt).
//
// Each bot tank has at most one automatic marker (tank_mark_auto_: tank guid -> icon), and the marker pass of the plan sends the tank to the mob
// that carries it. A tank that taunts the boss back from a non-tank holds the boss; if the markers stay where they were, the next plan sends the
// previous boss tank back onto him and the retaker onto his old mob, and the boss changes hands again (Majordomo 2026-10-17: a taunt, then
// "takes Majordomo why=marker 0" a second later, five mobs loose). The swap gives the retaker the icon of the boss and the previous holder the
// retaker's icon, so the assignment the taunt made is the one the marker pass keeps.
// Pure function, covered by tests/game/ClassicRaidRetaunt.cpp.

#pragma once

#include <cstdint>
#include <utility>
#include <unordered_map>

namespace Playerbot {

// Swaps the icons of two tanks. A tank without an automatic icon receives the other's and leaves none behind. Nothing to swap if neither has one.
// Returns true when anything changed.
inline bool SwapTankMarks(std::unordered_map<uint64_t, uint8_t>& marks, uint64_t a, uint64_t b)
{
    if (a == b)
        return false;
    auto ia = marks.find(a);
    auto ib = marks.find(b);
    if (ia == marks.end() && ib == marks.end())
        return false;
    if (ia != marks.end() && ib != marks.end())
    {
        std::swap(ia->second, ib->second);
        return true;
    }
    if (ia != marks.end())
    {
        uint8_t const icon = ia->second;
        marks.erase(ia);
        marks[b] = icon;
    }
    else
    {
        uint8_t const icon = ib->second;
        marks.erase(ib);
        marks[a] = icon;
    }
    return true;
}

} // namespace Playerbot
