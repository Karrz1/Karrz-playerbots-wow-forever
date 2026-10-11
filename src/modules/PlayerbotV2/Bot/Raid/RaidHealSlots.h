// RaidHealSlots - which tank each tank-healer slot of a raid is for, when the healers on tank duty are fewer than the
// tanks' full quota (main tank 3, off tank 2, every spare tank 1). Pure, tested in tests/game/ClassicRaidHealSlots.cpp.

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Playerbot::Raid {

// `slots` healer slots in priority order. The main tank takes the first two (it holds the boss and takes most of the damage;
// a plain round-robin gave it ONE healer when the raid had four tanks and five healers, and it died in 12 s), then the off
// tank and every spare tank one each, then the main tank's third and the off tank's second. A tank id of 0 is absent.
inline std::vector<uint64_t> HealSlotOrder(uint64_t main_tank, uint64_t off_tank, std::vector<uint64_t> const& spare_tanks, uint32_t slots)
{
    std::vector<uint64_t> order;
    if (main_tank)
        order.insert(order.end(), 2, main_tank);
    if (off_tank)
        order.push_back(off_tank);
    for (uint64_t t : spare_tanks)
        order.push_back(t);
    if (main_tank)
        order.push_back(main_tank);
    if (off_tank)
        order.push_back(off_tank);
    if (order.size() > slots)
        order.resize(slots);
    return order;
}

} // namespace Playerbot::Raid
