// NoMotionGuard - a goal a bot keeps being given but never moves toward is dropped. Whatever the reason (a spot off the navmesh, a
// partial route that degenerates to a zero move, a ledge it cannot climb), re-issuing the same move for ever looks like a statue and
// starves every other goal. Judged on the bot's own position: the same goal asked again and again while the bot has not moved
// kMinMoveYards over kWindowMs, kMaxWindows times in a row, is abandoned. Tested in tests/game/ClassicNoMotionGuard.cpp.

#pragma once

#include <cmath>
#include <cstdint>

namespace Playerbot::BotMovement {

struct NoMotionGuard
{
    std::uint64_t goal_key = 0;
    float x = 0.f, y = 0.f;          // where the bot stood when the current window opened
    std::uint32_t since_ms = 0;
    std::uint32_t windows = 0;       // windows in a row without motion
};

constexpr float kNoMotionMinMoveYards = 3.0f;
constexpr std::uint32_t kNoMotionWindowMs = 8000u;
constexpr std::uint32_t kNoMotionMaxWindows = 4u;     // ~32 s of asking without moving

// Call on every move request toward `goal_key` from (x, y) at `now_ms`. True = carry on, false = abandon this goal.
inline bool NoMotionAllows(NoMotionGuard& g, std::uint64_t goal_key, float x, float y, std::uint32_t now_ms)
{
    float const dx = x - g.x, dy = y - g.y;
    if (g.goal_key != goal_key || g.since_ms == 0 || dx * dx + dy * dy >= kNoMotionMinMoveYards * kNoMotionMinMoveYards)
    {
        g.goal_key = goal_key; g.x = x; g.y = y; g.since_ms = now_ms ? now_ms : 1u; g.windows = 0;
        return true;
    }
    if (now_ms - g.since_ms >= kNoMotionWindowMs)
    {
        g.since_ms = now_ms;
        if (++g.windows >= kNoMotionMaxWindows)
        {
            g.windows = 0;
            return false;
        }
    }
    return true;
}

} // namespace Playerbot::BotMovement
