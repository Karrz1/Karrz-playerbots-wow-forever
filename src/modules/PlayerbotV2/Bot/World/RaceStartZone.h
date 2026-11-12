// RaceStartZone - the starting zone of each vanilla race. Low-level bots stand in the start zone of their own race, as a new character
// does: orcs and trolls in Durotar, undead in Tirisfal, and so on. A mixed crowd of every Horde race in Deathknell reads as wrong.
// Pure rules, tested in tests/game/ClassicRaceStartZone.cpp.

#pragma once

#include <cstdint>

namespace Playerbot::V2::World {

constexpr std::uint32_t kStartZoneMaxLevel = 10;   // above this a bot has outgrown its start zone and may be anywhere suited

// AreaTable zone id of the race's starting zone, 0 for a race without one here (Blood Elf, Draenei and the later races).
inline std::uint32_t StartZoneOfRace(std::uint8_t race)
{
    switch (race)
    {
        case 1: return 12;    // Human: Elwynn Forest
        case 2: return 14;    // Orc: Durotar
        case 3: return 1;     // Dwarf: Dun Morogh
        case 4: return 141;   // Night Elf: Teldrassil
        case 5: return 85;    // Undead: Tirisfal Glades
        case 6: return 215;   // Tauren: Mulgore
        case 7: return 1;     // Gnome: Dun Morogh
        case 8: return 14;    // Troll: Durotar
        default: return 0;
    }
}

inline bool IsStartZone(std::uint32_t zone)
{
    return zone == 12 || zone == 14 || zone == 1 || zone == 141 || zone == 85 || zone == 215;
}

// May a bot of `race` and `level` be put in `zone`? Anywhere but a start zone. A start zone takes only bots at the start levels, and of
// those only the races that begin there (a race without a start zone of its own is not limited). Higher levels kill the valley's few
// mobs in one hit each: on the PC (26 Oct) 39 Alliance bots of level 11-60 cleared Deathknell beside 42 undead residents.
inline bool RaceFitsStartZone(std::uint8_t race, std::uint32_t level, std::uint32_t zone)
{
    if (!IsStartZone(zone))
        return true;
    if (level > kStartZoneMaxLevel)
        return false;
    return StartZoneOfRace(race) == 0 || StartZoneOfRace(race) == zone;
}

// One of the races that start in `zone`, by `seed` (two races share Dun Morogh and Durotar). 0 when `zone` is not a start zone.
inline std::uint8_t RaceForStartZone(std::uint32_t zone, std::uint64_t seed)
{
    switch (zone)
    {
        case 12:  return 1;
        case 14:  return (seed & 1u) ? 8 : 2;
        case 1:   return (seed & 1u) ? 7 : 3;
        case 141: return 4;
        case 85:  return 5;
        case 215: return 6;
        default:  return 0;
    }
}

} // namespace Playerbot::V2::World
