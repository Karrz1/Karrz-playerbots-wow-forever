// ReactionTiming - how long a bot takes from "it could act" to "it acted", for the three reactions that decide whether bots feel
// quick: the next cast after the global cooldown ends, an interrupt once a kickable cast is on, and the first step out of a hazard.
//
// World thread only (the publish pass and the intent executor both run there). No game headers, so it is unit-tested standalone
// (tests/game/ClassicReactionTiming.cpp). Times are GameTime milliseconds. A line per metric is logged every kFlushMs on channel
// playerbot.v2.react (shows with Logger.playerbot.v2=3): n, mean, max and a spread across 50/100/150/250/500 ms.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Playerbot {

class ReactionTiming
{
public:
    static constexpr uint32_t kFlushMs = 60000;
    // A gap above this is a bot with nothing ready to cast (cooldowns, no target), not a slow reaction: counted apart.
    static constexpr uint32_t kGcdIdleMs = 1500;
    static constexpr uint32_t kKickMaxMs = 15000;
    static constexpr uint32_t kHazardMaxMs = 5000;

    struct Hist
    {
        static constexpr std::array<uint32_t, 5> kBounds{ 50, 100, 150, 250, 500 };
        uint64_t n = 0;
        uint64_t sum = 0;
        uint32_t max = 0;
        std::array<uint64_t, 6> bucket{};
        void add(uint32_t ms)
        {
            ++n; sum += ms; if (ms > max) max = ms;
            size_t i = 0;
            while (i < kBounds.size() && ms > kBounds[i]) ++i;
            ++bucket[i];
        }
        std::string line() const;
    };

    // ---- GCD: snapshot says the global cooldown ends at ready_at; the first successful on-GCD cast after that is the sample.
    // `role`: 0 tank, 1 healer, 2 dps, anything else other (kept apart in gcd_role so a slow healer is not hidden in a fast dps mean).
    void NoteGcdReady(uint64_t bot, uint32_t ready_at_ms, uint8_t role = 3) { gcd_ready_[bot] = { ready_at_ms, role }; }
    // A cast that succeeded at now_ms. Before the GCD end it is off-GCD (a kick, a trinket): ignored.
    void OnCast(uint64_t bot, uint32_t now_ms)
    {
        auto it = gcd_ready_.find(bot);
        if (it == gcd_ready_.end() || now_ms < it->second.ready_at)
            return;
        uint32_t const gap = now_ms - it->second.ready_at;
        uint8_t const role = it->second.role < kRoles ? it->second.role : uint8_t(kRoles - 1);
        gcd_ready_.erase(it);
        if (gap > kGcdIdleMs) { ++gcd_idle_; ++gcd_idle_role_[role]; }
        else { gcd_.add(gap); gcd_role_[role].add(gap); }
    }

    // ---- Interrupt: first snapshot in which the bot could kick (ability ready, a kickable cast in range), until the kick lands.
    // Called every publish while in combat with the answer; a gap in "could kick" starts a new window.
    void NoteKickPossible(uint64_t bot, bool possible, uint32_t now_ms)
    {
        if (!possible) { kick_since_.erase(bot); return; }
        kick_since_.emplace(bot, now_ms);   // keeps the first time
    }
    void OnKick(uint64_t bot, uint32_t now_ms)
    {
        auto it = kick_since_.find(bot);
        if (it == kick_since_.end()) return;
        uint32_t const gap = now_ms >= it->second ? now_ms - it->second : 0u;
        kick_since_.erase(it);
        if (gap <= kKickMaxMs) kick_.add(gap);
    }

    // ---- Hazard: first snapshot carrying a leave-hazard order, until the bot's move lands.
    void NoteHazardOrder(uint64_t bot, bool present, uint32_t now_ms)
    {
        if (!present)
        {
            auto it = hazard_since_.find(bot);
            if (it != hazard_since_.end() && now_ms - it->second > kHazardMaxMs) hazard_since_.erase(it);
            return;
        }
        hazard_since_.emplace(bot, now_ms);
    }
    void OnMove(uint64_t bot, uint32_t now_ms)
    {
        auto it = hazard_since_.find(bot);
        if (it == hazard_since_.end()) return;
        uint32_t const gap = now_ms >= it->second ? now_ms - it->second : 0u;
        hazard_since_.erase(it);
        if (gap <= kHazardMaxMs) hazard_.add(gap);
    }

    void Forget(uint64_t bot) { gcd_ready_.erase(bot); kick_since_.erase(bot); hazard_since_.erase(bot); }

    static constexpr size_t kRoles = 4;
    Hist const& gcd() const { return gcd_; }
    Hist const& gcd_role(size_t r) const { return gcd_role_[r < kRoles ? r : kRoles - 1]; }
    uint64_t gcd_idle_role(size_t r) const { return gcd_idle_role_[r < kRoles ? r : kRoles - 1]; }
    Hist const& kick() const { return kick_; }
    Hist const& hazard() const { return hazard_; }
    uint64_t gcd_idle() const { return gcd_idle_; }

    // True once per kFlushMs; the caller logs the three lines and resets with TakeAndReset.
    bool FlushDue(uint32_t now_ms)
    {
        if (last_flush_ms_ == 0) { last_flush_ms_ = now_ms ? now_ms : 1; return false; }
        if (now_ms - last_flush_ms_ < kFlushMs) return false;
        last_flush_ms_ = now_ms;
        return true;
    }
    void ResetWindow() { gcd_ = {}; kick_ = {}; hazard_ = {}; gcd_idle_ = 0; gcd_role_ = {}; gcd_idle_role_ = {}; }

    static ReactionTiming& Instance() { static ReactionTiming t; return t; }

private:
    struct Pending { uint32_t ready_at = 0; uint8_t role = 3; };
    std::unordered_map<uint64_t, Pending> gcd_ready_;
    std::unordered_map<uint64_t, uint32_t> kick_since_, hazard_since_;
    Hist gcd_, kick_, hazard_;
    std::array<Hist, 4> gcd_role_{};
    std::array<uint64_t, 4> gcd_idle_role_{};
    uint64_t gcd_idle_ = 0;
    uint32_t last_flush_ms_ = 0;
};

inline std::string ReactionTiming::Hist::line() const
{
    std::string s = "n=" + std::to_string(n);
    if (!n) return s;
    s += " mean=" + std::to_string(sum / n) + "ms max=" + std::to_string(max) + "ms";
    char const* names[] = { "<=50", "<=100", "<=150", "<=250", "<=500", ">500" };
    for (size_t i = 0; i < bucket.size(); ++i)
        s += std::string(" ") + names[i] + ":" + std::to_string(bucket[i]);
    return s;
}

} // namespace Playerbot
