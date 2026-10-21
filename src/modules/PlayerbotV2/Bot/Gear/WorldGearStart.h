#pragma once
// A world bot below this level keeps the outfit its character was created with, as a vanilla level 1-9 does: no gear is generated for
// it (a level 1 wore rings, trinkets, neck and cloak, playtest 2026-10-18). Companions of a player are not world bots and are not
// affected. Pure: tested in tests/game/ClassicWorldGearStart.cpp.

#include <cstdint>

namespace Playerbot::Gear {

constexpr std::uint8_t kWorldGearMinLevel = 10;

// replace_unrenderable_starter: the caller is swapping out a starter piece the client cannot draw, which is the one case where a
// level 1-9 world bot still gets a generated piece (otherwise it would be left bare).
constexpr bool GeneratesGear(bool world_bot, std::uint8_t level, bool replace_unrenderable_starter = false)
{
    return replace_unrenderable_starter || !world_bot || level >= kWorldGearMinLevel;
}

} // namespace Playerbot::Gear
