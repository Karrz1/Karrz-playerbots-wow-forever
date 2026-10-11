// WanderRule - whether a bot with nothing to do may walk about. A rested bot (rest bonus XP, banked while logged out in a town or inn)
// with no quests, offers or objective used to be held still so it could sit and emote. For a companion that is fine, but a bot of
// the world population that stands in one place looks dead, and a hub full of them is a crowd of statues. Only a bot that belongs to
// a player may stand and rest. Tested in tests/game/ClassicWanderRule.cpp.

#pragma once

#include <cstdint>

namespace Playerbot::V2 {

constexpr bool WanderAllowedByRest(std::uint32_t rest_bonus_xp, bool truly_idle, bool has_owner)
{
    return !has_owner || rest_bonus_xp == 0 || !truly_idle;
}

// A world bot waiting out resurrection sickness must not freeze like a statue: once every six seconds (a different second per bot)
// a standing one takes a short step. A companion or grouped bot keeps still (its owner and group decide where it goes).
constexpr bool ResSickStepNow(bool owned_or_grouped, bool moving, std::uint32_t now_ms, std::uint32_t bot_id)
{
    return !owned_or_grouped && !moving && ((now_ms / 1000u + bot_id) % 6u) == 0u;
}

} // namespace Playerbot::V2
