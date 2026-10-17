// SoakBoard - the assignments of the travel soak (Playerbot.World.TravelSoak): a few world bots are each sent to a place of the travel graph
// (a town, flight master, quest hub or dock, on their own map or another), and the board keeps what each one was asked, how it ended
// (arrived, timed out where, no route) and the totals, so an overnight run says which legs never arrive and in which 50 y cell they stop.
// The world thread assigns and drains; the idle rule on a bot worker reads its assignment and reports the ending, so the board is locked.
// Pure: no game types, tested in tests/game/ClassicSoakBoard.cpp.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Playerbot::V2::Travel {

enum class SoakResult : std::uint8_t { Arrived, TimedOut, NoRoute };

inline char const* SoakResultName(SoakResult r)
{
    switch (r)
    {
        case SoakResult::Arrived:  return "arrived";
        case SoakResult::TimedOut: return "timeout";
        case SoakResult::NoRoute:  return "no_route";
    }
    return "?";
}

struct SoakGoal
{
    std::uint32_t id = 0;
    std::uint32_t map = 0;
    float         x = 0, y = 0, z = 0;
    std::string   name;
    std::uint8_t  node_kind = 0;      // UnifiedTravelGraph NodeKind of the goal
    std::uint32_t start_map = 0;
    float         sx = 0, sy = 0, sz = 0;
    std::uint32_t started_ms = 0;
    std::uint32_t timeout_ms = 0;
};

struct SoakOutcome
{
    std::uint64_t guid = 0;
    SoakGoal      goal;
    SoakResult    result = SoakResult::TimedOut;
    std::uint32_t duration_ms = 0;
    std::uint32_t end_map = 0;
    float         ex = 0, ey = 0, ez = 0;
    std::uint32_t plan_legs = 0;
    std::uint32_t leg_index = 0;
};

constexpr float kSoakArriveYds = 25.0f;

// On the goal's map and within kSoakArriveYds of it in x, y.
inline bool SoakArrived(SoakGoal const& g, std::uint32_t map, float x, float y)
{
    return map == g.map && std::hypot(x - g.x, y - g.y) <= kSoakArriveYds;
}

inline bool SoakTimedOut(SoakGoal const& g, std::uint32_t now_ms)
{
    return std::uint32_t(now_ms - g.started_ms) >= g.timeout_ms;
}

// "map/cx/cy" of the 50 y cell: where a leg ended, to group the legs that stop in one place.
inline std::string SoakCell(std::uint32_t map, float x, float y)
{
    return std::to_string(map) + "/" + std::to_string(int(std::floor(x / 50.0f))) + "/" + std::to_string(int(std::floor(y / 50.0f)));
}

class SoakBoard
{
public:
    static SoakBoard& Instance()
    {
        static SoakBoard b;
        return b;
    }

    bool Assign(std::uint64_t guid, SoakGoal const& g)
    {
        std::lock_guard<std::mutex> lk(mx_);
        return active_.emplace(guid, g).second;
    }

    bool Get(std::uint64_t guid, SoakGoal& out) const
    {
        std::lock_guard<std::mutex> lk(mx_);
        auto it = active_.find(guid);
        if (it == active_.end()) return false;
        out = it->second;
        return true;
    }

    bool Has(std::uint64_t guid) const
    {
        std::lock_guard<std::mutex> lk(mx_);
        return active_.count(guid) != 0;
    }

    std::vector<std::uint64_t> ActiveGuids() const
    {
        std::lock_guard<std::mutex> lk(mx_);
        std::vector<std::uint64_t> v;
        v.reserve(active_.size());
        for (auto const& [g, goal] : active_) v.push_back(g);
        return v;
    }

    std::size_t Active() const
    {
        std::lock_guard<std::mutex> lk(mx_);
        return active_.size();
    }

    // The bot's leg is over. A second call for the same bot (the rule runs again before the driver drains) is ignored.
    void Finish(std::uint64_t guid, SoakResult r, std::uint32_t now_ms, std::uint32_t end_map, float ex, float ey, float ez,
                std::uint32_t plan_legs, std::uint32_t leg_index)
    {
        std::lock_guard<std::mutex> lk(mx_);
        auto it = active_.find(guid);
        if (it == active_.end()) return;
        SoakOutcome o;
        o.guid = guid;
        o.goal = it->second;
        o.result = r;
        o.duration_ms = std::uint32_t(now_ms - o.goal.started_ms);
        o.end_map = end_map; o.ex = ex; o.ey = ey; o.ez = ez;
        o.plan_legs = plan_legs; o.leg_index = leg_index;
        active_.erase(it);
        ++totals_[std::size_t(r)];
        if (r != SoakResult::Arrived)
            ++stops_[SoakCell(end_map, ex, ey)];
        done_.push_back(std::move(o));
    }

    // The bot is gone (logged out, died for good): its leg is dropped without a verdict.
    void Cancel(std::uint64_t guid)
    {
        std::lock_guard<std::mutex> lk(mx_);
        active_.erase(guid);
    }

    std::vector<SoakOutcome> Drain()
    {
        std::lock_guard<std::mutex> lk(mx_);
        std::vector<SoakOutcome> out;
        out.swap(done_);
        return out;
    }

    std::uint32_t Total(SoakResult r) const
    {
        std::lock_guard<std::mutex> lk(mx_);
        return totals_[std::size_t(r)];
    }

    // The cells where the most legs stopped without arriving, most first.
    std::vector<std::pair<std::string, std::uint32_t>> TopStops(std::size_t n) const
    {
        std::lock_guard<std::mutex> lk(mx_);
        std::vector<std::pair<std::string, std::uint32_t>> v(stops_.begin(), stops_.end());
        std::sort(v.begin(), v.end(), [](auto const& a, auto const& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
        if (v.size() > n) v.resize(n);
        return v;
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lk(mx_);
        active_.clear(); done_.clear(); stops_.clear();
        totals_[0] = totals_[1] = totals_[2] = 0;
    }

private:
    mutable std::mutex mx_;
    std::unordered_map<std::uint64_t, SoakGoal> active_;
    std::vector<SoakOutcome> done_;
    std::map<std::string, std::uint32_t> stops_;
    std::uint32_t totals_[3] = {0, 0, 0};
};

} // namespace Playerbot::V2::Travel
