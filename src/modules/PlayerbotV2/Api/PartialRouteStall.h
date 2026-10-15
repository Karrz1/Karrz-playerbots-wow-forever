// PartialRouteStall - stops a bot from walking partial routes toward a far goal forever without getting closer.
// A route past the path cap comes back as a partial ending at the reachable polygon nearest the goal. From one pose that end can lie
// down a ramp, from the next one back up it, so every partial is "reachable" and the bot walks between two local minima.
// Judged on net progress, not on the shape of a single partial: every driven partial counts, and only genuinely closing in on the
// goal gives the budget back. Tested in tests/game/ClassicPartialRouteStall.cpp.

#pragma once

#include <cstdint>

namespace Playerbot::BotMovement {

struct PartialRouteStall
{
    std::uint64_t goal_key = 0;
    float best_dist = 1e9f;         // closest the bot has been to the goal since the budget was last refilled
    std::uint32_t stalled = 0;      // partials driven since then without closing in
};

constexpr float kStallMinGainYards = 5.0f;          // closing in by less than this is not progress
constexpr std::uint32_t kStallMaxPartials = 8u;     // a pocket needs a few legs to climb out of; a loop never closes in

// Call before driving a partial route. `dist_to_goal` is the bot's current distance to the goal.
// Returns true when the partial may be driven, false when the bot has walked the budget without getting closer.
inline bool AllowPartialRoute(PartialRouteStall& st, std::uint64_t goal_key, float dist_to_goal)
{
    if (st.goal_key != goal_key || dist_to_goal + kStallMinGainYards < st.best_dist)
    {
        st.goal_key = goal_key;
        st.best_dist = dist_to_goal;
        st.stalled = 0;
        return true;
    }
    if (dist_to_goal < st.best_dist)
        st.best_dist = dist_to_goal;
    ++st.stalled;
    return st.stalled <= kStallMaxPartials;
}

} // namespace Playerbot::BotMovement
