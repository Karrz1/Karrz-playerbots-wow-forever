#pragma once
// A world bot below this level keeps the outfit its character was created with, as a vanilla level 1-9 does: no gear is generated for
// it (a level 1 wore rings, trinkets, neck and cloak, playtest 2026-10-18). Companions of a player are not world bots and are not
// affected. Pure: tested in tests/game/ClassicWorldGearStart.cpp.

#include <cstdint>

namespace Playerbot::Gear {

constexpr std::uint8_t kWorldGearMinLevel = 10;

constexpr bool GeneratesGear(bool world_bot, std::uint8_t level)
{
    return !world_bot || level >= kWorldGearMinLevel;
}

} // namespace Playerbot::Gear
