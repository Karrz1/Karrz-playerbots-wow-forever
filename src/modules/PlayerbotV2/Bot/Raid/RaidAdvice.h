// RaidAdvice — lets the raid rules drive the dungeon advice for raid maps.

#pragma once

#include <cstdint>

namespace Playerbot {
struct DungeonAdvice;
}

namespace Playerbot::Raid {

// For a map with raid rules: kill order (high_priority_kill_entries, which
// feeds the PvE coordinator's synchronized kill focus) and the boss list come
// from the rule data, and the generic movement/CC/dispel fields are cleared
// because the raid brain owns those mechanics there (the generic "step 14y
// away from the caster of any listed aura" rule walked cursed bots away from
// Lucifron and fought the brain's hazard moves). Interrupts and progression
// waypoints are left as the dungeon script set them. No-op for other maps.
void ApplyRaidAdvice(uint32_t mapId, DungeonAdvice& advice);

} // namespace Playerbot::Raid
