// IdleWatch - a world bot that has not moved for a long time with no reason to stand there is given a new activity, wherever it is
// in the world and whichever rule is holding it. The rule cascade has many places that end a tick without moving (a yield to an
// offer it cannot take, a settled sit, a blocked goal, a wait that never ends); instead of finding each, the bot's own position
// is watched. Pure decision logic, tested in tests/game/ClassicIdleWatch.cpp.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Playerbot::V2 {

struct IdleWatch
{
    float x = 0.f, y = 0.f;               // where the bot stood when the window opened
    std::uint32_t since_ms = 0;           // since when it has stood there
    std::uint32_t strikes = 0;            // times in a row it was found standing; picks which new activity to try
    std::uint32_t forced_until_ms = 0;    // the new activity runs until then
    float tx = 0.f, ty = 0.f, tz = 0.f;   // the place it was sent to
    bool has_target = false;
    char const* prev_rule = nullptr;      // the last rule other than the watch's own walk that moved or held the bot
};

// The rules that walk a bot toward its quest objective. A bot found standing again and again while one of them was the last to hold
// it cannot close that objective from here.
inline bool IsObjectiveRule(char const* rule)
{
    if (!rule) return false;
    for (char const* name : { "idle:pursue_quest_goal", "idle:far_same_map_travel" })
    {
        char const* a = rule;
        char const* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (!*b) return true;
    }
    return false;
}

constexpr float kIdleMoveYards = 3.0f;               // less than this is still standing
constexpr float kIdleResetStrikesYards = 20.0f;      // really went somewhere: the next stall starts from the first activity
constexpr std::uint32_t kIdleStuckMs = 45000u;       // standing this long with no reason is stuck
constexpr std::uint32_t kIdleForcedMs = 90000u;      // a new activity is followed this long
constexpr float kIdleMinHubYards = 60.0f;            // a hub the bot is not already standing in
constexpr float kIdleMaxHubYards = 2500.0f;
constexpr std::size_t kIdleHubChoices = 6;           // the nearest few; strikes and the bot id pick among them

// Call every tick. `exempt` = a good reason to stand (fighting, casting, resting at low health, grouped, owned, in an instance,
// riding). Returns true once when the bot has stood still for `stuck_ms` (kIdleStuckMs; shorter in a capital) without a reason.
inline bool IdleStuck(IdleWatch& w, float x, float y, std::uint32_t now_ms, bool exempt, std::uint32_t stuck_ms = kIdleStuckMs)
{
    float const dx = x - w.x, dy = y - w.y;
    float const moved2 = dx * dx + dy * dy;
    if (exempt || w.since_ms == 0 || moved2 >= kIdleMoveYards * kIdleMoveYards)
    {
        if (moved2 >= kIdleResetStrikesYards * kIdleResetStrikesYards) w.strikes = 0;
        w.x = x; w.y = y; w.since_ms = now_ms ? now_ms : 1u;
        return false;
    }
    if (now_ms - w.since_ms >= stuck_ms)
    {
        w.since_ms = now_ms;
        ++w.strikes;
        return true;
    }
    return false;
}

// A walk toward a far goal that keeps moving but never gets closer: the bot runs back and forth (a party in Ironforge's Military Ward
// chunk-walked for half an hour toward a Searing Gorge objective that has no walking route out of the room, live 275). The idle watch
// does not see it, the bot is never still. `key` names the goal; the best distance resets whenever the goal changes.
struct GoalProgress
{
    std::uint64_t key = 0;
    float best = 0.f;
    std::uint32_t since_ms = 0;   // when `best` last improved by kGoalProgressYards
    std::uint32_t last_ms = 0;    // the last call: a walk broken off (a fight, a meal, another rule) starts the clock again
};

constexpr float kGoalProgressYards = 30.0f;
constexpr std::uint32_t kGoalProgressMs = 180000u;   // a walking bot covers 1200 y in that time: any real route gains 30 y
constexpr std::uint32_t kGoalProgressGapMs = 20000u;

// Call each time the bot walks toward the goal. True once it has gone kGoalProgressMs without getting kGoalProgressYards closer.
inline bool GoalStalled(GoalProgress& g, std::uint64_t key, float dist, std::uint32_t now_ms)
{
    bool const broken_off = g.last_ms != 0 && now_ms - g.last_ms > kGoalProgressGapMs;
    g.last_ms = now_ms ? now_ms : 1u;
    if (g.key != key || g.since_ms == 0 || broken_off || dist <= g.best - kGoalProgressYards)
    {
        g.key = key; g.best = dist; g.since_ms = now_ms ? now_ms : 1u;
        return false;
    }
    if (now_ms - g.since_ms >= kGoalProgressMs)
    {
        g.since_ms = now_ms ? now_ms : 1u;
        g.best = dist;
        return true;
    }
    return false;
}

// Which of the `count` candidate hubs (nearest first) a stuck bot is sent to: a different one at each strike, a different one per bot.
inline std::size_t IdlePickHub(std::size_t count, std::uint32_t strikes, std::uint32_t bot_id)
{
    std::size_t const n = count < kIdleHubChoices ? count : kIdleHubChoices;
    return n ? (std::size_t(strikes) + bot_id) % n : 0;
}

} // namespace Playerbot::V2
