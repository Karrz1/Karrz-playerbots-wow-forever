// ReactionWake - when a bot in combat should be woken early, at the instant its global cooldown or cast ends,
// instead of waiting for its next regular tick slot.
//
// Pure arithmetic, no Player access, nothing from the game headers: unit-tested standalone (tests/game/ClassicReactionWake.cpp).
// The regular Combat tick is 100 ms and phase-aligned per bot, so a bot whose GCD ends between slots waits on average half a slot
// (50 ms) before its next decision. A wake lands the snapshot build and the bot tick on the world tick the GCD ends in.
#pragma once

#include <cstdint>

namespace Playerbot::ReactionWake {

// A busy time shorter than this is left to the regular tick: the wake would land within a world tick of it anyway.
constexpr int64_t kMinBusyMs = 15;
// A wake that would land less than this before the regular tick that follows the busy time gains nothing and is skipped.
constexpr int64_t kMinGainMs = 20;
// At least this long between two wakes of one bot, so a run of short GCDs cannot turn into a tick storm.
constexpr int64_t kMinGapMs = 40;
// A busy time longer than this (a long channel) is not waited for with a single wake; the regular ticks carry on meanwhile.
constexpr int64_t kMaxBusyMs = 5000;

struct Input
{
    int64_t busy_ms = 0;            // time until the GCD / cast ends, from the snapshot just built (max of the two)
    int64_t next_tick_in_ms = 0;    // time until the bot's next regular tick (<= 0: due now)
    int64_t period_ms = 100;        // the bot's regular tick period
    int64_t since_last_wake_ms = 1000000;
};

// Delay in ms after which to wake the bot, or -1 for no wake.
inline int64_t Delay(Input const& in)
{
    if (in.busy_ms < kMinBusyMs || in.busy_ms > kMaxBusyMs)
        return -1;
    if (in.since_last_wake_ms < kMinGapMs)
        return -1;
    if (in.period_ms <= 0)
        return -1;

    // The first regular tick at or after the moment the busy time ends.
    int64_t first_tick_after = in.next_tick_in_ms > 0 ? in.next_tick_in_ms : 0;
    if (first_tick_after < in.busy_ms)
    {
        int64_t const behind = in.busy_ms - first_tick_after;
        int64_t const steps = (behind + in.period_ms - 1) / in.period_ms;
        first_tick_after += steps * in.period_ms;
    }
    if (first_tick_after - in.busy_ms < kMinGainMs)
        return -1;
    return in.busy_ms;
}

} // namespace Playerbot::ReactionWake
