#pragma once
// Pure positioning decisions for melee-behind stepping, flee-angle memory and dungeon disperse / kite steps.
//
// Ideas from the generic layers of two other bot projects, rewritten against our snapshot (no code copied):
//   * Karrz's VMaNGOS bots (the ike3-based playerbot code, playerbot/values/StanceReposition.cpp, Stances.cpp): the melee "behind" step
//     fires only for a mob a tank holds, inside a 135 degree front arc, never on the mob's own victim, never on Onyxia.
//   * AzerothCore mod-playerbots (MovementActions.cpp CheckLastFlee / CombatFormationMoveAction): a flee step that reverses a
//     recent flee is rejected, and ranged / melee keep 5 / 2 yards from the nearest member.
// State_Idle.cpp calls these; keeping them here keeps that file's diff small.

#include "Bot/BotSnapshot.h"
#include <cmath>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace Playerbot::Positioning {

constexpr uint32 kOnyxiaEntry = 10184;   // tail sweep behind: standing behind is not safe there
constexpr float  kBehindFrontHalfAngle = 1.178097f;   // 67.5 deg either side of the facing = a 135 degree front arc

// A melee bot standing in the front arc of `mob` should step behind it. False when the bot is the mob's victim (it is the tank
// or has aggro: moving it is a plain bug), when no tank holds the mob (the mob is on a squishy: run, do not circle), or on a tail-sweep boss.
inline bool MeleeBehindWanted(NearbyUnit const& mob, ObjectGuid me, float bot_x, float bot_y)
{
    if (mob.victim == me || mob.victim_kind != uint8(ThreatVictim::Tank) || mob.entry == kOnyxiaEntry)
        return false;
    const float dx = bot_x - mob.x, dy = bot_y - mob.y;
    const float d2 = dx * dx + dy * dy;
    if (d2 > 5.0f * 5.0f || d2 <= 0.25f)
        return false;
    // Angle between the mob's facing and the bot's bearing from it.
    const float dot = (dx * std::cos(mob.o) + dy * std::sin(mob.o)) / std::sqrt(d2);
    return dot > std::cos(kBehindFrontHalfAngle);
}

// Recent flee bearings per bot, newest last. A step whose bearing reverses a recent one (within 45 degrees of the opposite)
// is the oscillation between two overlapping step-out rules.
class FleeMemory
{
public:
    static constexpr uint32 kWindowMs = 5000;
    static constexpr size_t kMax = 10;

    static void Note(uint64 bot, float angle, uint32 now_ms)
    {
        std::lock_guard<std::mutex> lk(Mu());
        auto& q = Map()[bot];
        q.push_back({ angle, now_ms });
        while (q.size() > kMax) q.pop_front();
    }

    static bool Reverses(uint64 bot, float angle, uint32 now_ms)
    {
        std::lock_guard<std::mutex> lk(Mu());
        auto it = Map().find(bot);
        if (it == Map().end()) return false;
        for (auto const& e : it->second)
        {
            if (now_ms - e.second > kWindowMs) continue;
            float d = std::fabs(std::remainder(angle - (e.first + 3.14159265f), 6.28318531f));
            if (d < 0.785398f) return true;
        }
        return false;
    }

private:
    using Q = std::deque<std::pair<float, uint32>>;
    static std::mutex& Mu() { static std::mutex m; return m; }
    static std::unordered_map<uint64, Q>& Map() { static std::unordered_map<uint64, Q> m; return m; }
};

// Distance to keep from the nearest group member in a dungeon fight (AzerothCore disperse: ranged 5, melee 2), scaled by the
// owner's formation spacing.
constexpr float kRangedDisperse = 5.0f;
constexpr float kMeleeDisperse  = 2.0f;
// A mob already fighting the group closer than this to a ranged bot or healer (AzerothCore "enemy too close").
constexpr float kRangedTooCloseToMob = 5.0f;

} // namespace Playerbot::Positioning
