// SkyborneIsle - the rules for Skyborne world bots: who they are, where they level and when they leave Zephras Isle.
//
// Classic 1.60 (WoW Forever) adds two races, High Order Skyborne (95, Alliance) and Windshaper Skyborne (96, Horde), who start on
// Zephras Isle (map 2991, zone 16593), an island of their own: the server data has no walkable road, boat or portal that leaves it
// (the only portals on the island, to Valanaar and Rohashi Spires, stay on the island; the Skyborne portals of Stormwind and
// Dalaran lead between those two cities). So:
//   - a world bot of a Skyborne race is rolled with a small share of the ambient bots (RaceWeight below) and, like every other
//     ambient bot of level 10 and up, is put in a vanilla leveling town by the setup pipeline;
//   - around a real player on Zephras the spawner keeps Skyborne bots of level 1-9 who quest on the island (the other races
//     cannot be there: they would not find a quest giver who deals with them, and they would never get off);
//   - a bot that reaches level 10 on the island is moved once to a vanilla leveling town, as the pipeline does for a new
//     level-10 bot (BotSetupPipeline::Graduate). It is the one place a bot is carried between maps, and it is only there because
//     no route exists.
// Pure: no game types, tested in tests/game/ClassicSkyborneIsle.cpp.

#pragma once

#include <cstdint>

namespace Playerbot::V2::Fleet::Skyborne {

constexpr uint32_t kIsleMap  = 2991;
constexpr uint32_t kIsleZone = 16593;

constexpr uint8_t kRaceAlliance = 95;   // RACE_SKYBORNE_ALLIANCE
constexpr uint8_t kRaceHorde    = 96;   // RACE_SKYBORNE_HORDE

// Levels of the island: the quests of Zephras Isle are tuned 1-9 (sql: 72 quests, quest levels 1-14 with most below 10).
constexpr uint8_t kIsleLevelLo = 1;
constexpr uint8_t kIsleLevelHi = 9;
// The level at which a bot has outgrown the island.
constexpr uint8_t kGraduationLevel = 10;

// Share of the ambient roll, next to Human 18 / Night Elf 12 / Dwarf 6 / Gnome 4 (Alliance, 40 in all) and Orc 10 / Undead 10 /
// Tauren 8 / Troll 6 (Horde, 34): about 9% of the Alliance bots and 11% of the Horde bots.
constexpr uint16_t kRaceWeight = 4;

constexpr bool IsSkyborneRace(uint32_t race) { return race == kRaceAlliance || race == kRaceHorde; }
constexpr bool IsSkyborneAlliance(uint32_t race) { return race == kRaceAlliance; }
constexpr uint8_t RaceForTeam(bool alliance) { return alliance ? kRaceAlliance : kRaceHorde; }

// Class combinations from the client's CharBaseInfo.db2 (sql/custom/world/2026_09_27_00_world_classic_skyborne.sql).
// Class ids: 1 Warrior, 3 Hunter, 4 Rogue, 7 Shaman, 8 Mage, 11 Druid.
constexpr bool IsLegalClass(uint32_t race, uint32_t cls)
{
    if (race == kRaceAlliance)
        return cls == 1 || cls == 3 || cls == 4 || cls == 8 || cls == 11;
    if (race == kRaceHorde)
        return cls == 1 || cls == 3 || cls == 4 || cls == 7 || cls == 11;
    return false;
}

// The level window of a Zephras anchor: the island's own, never lifted to the vanilla world's MinLevel (10), which is why a
// bot of level 1-9 exists here at all. The zone's +-2 margin the vanilla zones get would put level 11 bots on the island.
struct Window { uint8_t lo; uint8_t hi; };
constexpr Window IsleWindow() { return { kIsleLevelLo, kIsleLevelHi }; }

constexpr bool IsOnIsle(uint32_t map) { return map == kIsleMap; }

// An ambient bot that stands on the island at graduation level or above, free to move (not in a group, in combat or in a
// transfer), is carried to a leveling town once. `owned` is a player's companion: never.
constexpr bool ShouldGraduate(uint32_t map, uint32_t level, bool owned, bool grouped, bool in_combat, bool in_transit)
{
    return IsOnIsle(map) && level >= kGraduationLevel && !owned && !grouped && !in_combat && !in_transit;
}

// A Skyborne pool row may wake around an anchor only where it belongs: on the island the anchor wants Skyborne bots of the
// island's levels; anywhere else a Skyborne bot is an ordinary level 10+ bot and a level 1-9 one stays asleep on the island.
constexpr bool PoolRowFitsAnchor(bool anchor_on_isle, uint32_t row_race, uint32_t row_level)
{
    if (anchor_on_isle)
        return IsSkyborneRace(row_race) && row_level >= kIsleLevelLo && row_level <= kIsleLevelHi;
    return !(IsSkyborneRace(row_race) && row_level < kGraduationLevel);
}

} // namespace Playerbot::V2::Fleet::Skyborne
