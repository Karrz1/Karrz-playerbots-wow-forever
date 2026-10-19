// InstanceFullHold - a companion refused at an instance door because the instance holds its most players waits where it is and asks
// again later, instead of walking to an entrance it cannot use. Plain function so it is tested without a world
// (tests/game/ClassicInstanceFullHold.cpp). A 15-bot raid in a 10-player dungeon: a bot refused 'instance full' for
// minutes, walked the open world to the entrance and died to the Scarshield Sentries, outside the instance where nobody could raise him.

#pragma once

#include <cstdint>

namespace Playerbot::V2 {

// True while the full refusal for `anchor_map` is younger than `hold_ms`. `full_map` 0 = no refusal on record.
constexpr bool InstanceFullHolds(std::uint32_t full_map, std::uint32_t full_ms, std::uint32_t anchor_map, std::uint32_t now_ms,
                                 std::uint32_t hold_ms)
{
    return full_map != 0 && full_map == anchor_map && static_cast<std::uint32_t>(now_ms - full_ms) < hold_ms;
}

}  // namespace Playerbot::V2
