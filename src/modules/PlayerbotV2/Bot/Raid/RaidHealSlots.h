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

// A melee-soak boss (RaidRules::meleeSoak, Patchwerk's Hateful Strike on the off tanks): the strike goes to the soakers, never the main
// tank while they are in melee, so they take the healers. The main tank gets one, then the soakers (off tank and spare tanks) one each
// in turns, up to three each, then the main tank a second. naxx_patchwerk 1115 night: with main 3 / off 2 / spare 1 the soakers stood at
// 50-65 % when the next 9-11k strike came and one died every 5-10 s (41 deaths in 143 s).
inline std::vector<uint64_t> SoakHealSlotOrder(uint64_t main_tank, uint64_t off_tank, std::vector<uint64_t> const& spare_tanks, uint32_t slots)
{
    std::vector<uint64_t> soakers;
    if (off_tank)
        soakers.push_back(off_tank);
    for (uint64_t t : spare_tanks)
        if (t)
            soakers.push_back(t);
    std::vector<uint64_t> order;
    if (main_tank)
        order.push_back(main_tank);
    for (int round = 0; round < 3; ++round)
        for (uint64_t t : soakers)
            order.push_back(t);
    if (main_tank)
        order.push_back(main_tank);
    if (order.size() > slots)
        order.resize(slots);
    return order;
}

} // namespace Playerbot::Raid
