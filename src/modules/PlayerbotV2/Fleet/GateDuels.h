// GateDuels - pairs of world bots duelling outside a capital's gate while a real player is near, the classic vanilla sight; and the
// rules for a real player's duel with a bot. Pure part: the cities, the gate found from a path, pairing, layout, the board the idle
// rules read. The driver that applies it is WorldBotSpawner::DriveGateDuels; the rules are idle:gate_duel and idle:gate_challenge
// (Bot/States/Rules/AmbientRules.cpp). Tested in tests/game/ClassicGateDuels.cpp.
//
// The gate is not typed in: it is found once per city from the real navmesh, as the first walkable point outside the city on the road
// to the next town (the road targets below are points the travel graph's walk links were checked on).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Playerbot::V2::GateDuels
{

constexpr int           kMaxLevelGap     = 3;        // a pair of bots: levels this close
constexpr int           kPlayerLevelGap  = 10;       // a bot takes a real player's duel up to this many levels apart
constexpr float         kFighterGap      = 7.0f;     // yards between the two duellists at the start
constexpr float         kOutYards        = 14.0f;    // the spot is this far along the road past the city edge
constexpr std::uint32_t kStartDeadlineMs = 120000u;  // a pair that has not started a duel by then is dropped
constexpr std::uint32_t kDuelMaxMs       = 240000u;  // a duel that runs this long is left to the core
constexpr std::uint32_t kRestAfterMs     = 90000u;   // a bot that fought sits out this long (the pairs rotate)
constexpr std::uint32_t kRestFailedMs    = 300000u;  // a bot whose pair never started sits out longer

struct City
{
    char const* name;
    std::uint32_t map;
    bool alliance;
    std::uint32_t zone;                // the capital's zone id
    float cx, cy, cz;                  // inside the city
    float tx, ty, tz;                  // a point on the road to the next town, outside
};

inline constexpr City kCities[] = {
    { "Stormwind",     0, true,  1519, -8842.10f,   626.36f,   94.10f, -9465.2f,    50.8f,   57.0f },
    { "Ironforge",     0, true,  1537, -4980.00f,  -881.00f,  502.00f, -5596.2f,  -504.1f,  400.7f },
    { "Darnassus",     1, true,  1657,  9947.50f,  2482.70f, 1316.20f,  9822.2f,   952.0f, 1308.9f },
    { "Orgrimmar",     1, false, 1637,  1633.00f, -4439.00f,   16.00f,   326.0f, -4690.0f,   10.0f },
    { "Thunder Bluff", 1, false, 1638, -1196.00f,    29.00f,  175.00f, -2360.0f,  -370.0f,  -10.0f },
    { "Undercity",     0, false, 1497,  1812.00f,   239.00f,   62.00f,  2270.7f,   280.0f,   36.7f },
};
constexpr std::size_t kCityCount = sizeof(kCities) / sizeof(kCities[0]);

struct PathPt { float x, y, z; bool inside; };   // a point of a path, and whether it lies inside the city

// The gate spot from a path that starts in the city and runs out along the road: `out_yards` along the path past the first point
// outside. False when the path never leaves the city, or starts outside it.
inline bool GateFromPath(std::vector<PathPt> const& path, float out_yards, float& x, float& y, float& z)
{
    if (path.size() < 2 || !path.front().inside) return false;
    std::size_t first_out = path.size();
    for (std::size_t i = 1; i < path.size(); ++i)
        if (!path[i].inside) { first_out = i; break; }
    if (first_out >= path.size()) return false;
    float walked = 0.0f;
    std::size_t at = first_out;
    for (std::size_t i = first_out + 1; i < path.size() && walked < out_yards; ++i)
    {
        walked += std::hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
        at = i;
    }
    x = path[at].x; y = path[at].y; z = path[at].z;
    return true;
}

struct Candidate { std::uint64_t id; std::uint32_t level; int hp_pct; };
struct Pair { std::uint64_t challenger, other; };

inline std::uint32_t Mix(std::uint64_t id, std::uint32_t seed)
{
    std::uint32_t h = (std::uint32_t(id) + 1u) * 2654435761u;
    h ^= (seed + 1u) * 2246822519u;
    h ^= h >> 15;
    h *= 3266489917u;
    h ^= h >> 13;
    return h;
}

// Up to `want` pairs of healthy bots whose levels are within kMaxLevelGap, drawn in an order that changes with `seed`. The lower id
// challenges. A bot below level 6 or under 90% health is not picked.
inline std::vector<Pair> PickPairs(std::vector<Candidate> c, std::size_t want, std::uint32_t seed)
{
    std::vector<Pair> out;
    c.erase(std::remove_if(c.begin(), c.end(), [](Candidate const& k) { return k.level < 6u || k.hp_pct < 90; }), c.end());
    std::sort(c.begin(), c.end(), [&](Candidate const& a, Candidate const& b)
        { return Mix(a.id, seed) < Mix(b.id, seed); });
    std::vector<bool> used(c.size(), false);
    for (std::size_t i = 0; i < c.size() && out.size() < want; ++i)
    {
        if (used[i]) continue;
        std::size_t best = c.size();
        int best_gap = kMaxLevelGap + 1;
        for (std::size_t j = i + 1; j < c.size(); ++j)
        {
            if (used[j]) continue;
            int const gap = std::abs(int(c[i].level) - int(c[j].level));
            if (gap < best_gap) { best_gap = gap; best = j; }
        }
        if (best == c.size()) continue;
        used[i] = used[best] = true;
        out.push_back(c[i].id < c[best].id ? Pair{ c[i].id, c[best].id } : Pair{ c[best].id, c[i].id });
    }
    return out;
}

// Does a bot at `bot_level` take a real player's duel? Close enough in level; the other conditions (idle, healthy) are the caller's.
inline bool PlayerDuelOk(std::uint32_t bot_level, std::uint32_t player_level)
{
    return std::abs(int(bot_level) - int(player_level)) <= kPlayerLevelGap;
}

struct Pt { float x, y; };
struct Layout { Pt a, b; Pt onlooker[2]; };

// Where slot `slot` (0-2) of a gate stands: the pair a few yards apart, two onlookers beside them. Slots are well apart.
inline Layout LayoutFor(float gx, float gy, std::uint32_t slot)
{
    float const th = 1.0f + 2.1f * float(slot), r = 8.0f + 7.0f * float(slot);
    float const cx = gx + std::cos(th) * r, cy = gy + std::sin(th) * r;
    float const px = -std::sin(th), py = std::cos(th);   // across
    float const ux = std::cos(th), uy = std::sin(th);    // along
    Layout l;
    l.a = { cx + px * kFighterGap * 0.5f, cy + py * kFighterGap * 0.5f };
    l.b = { cx - px * kFighterGap * 0.5f, cy - py * kFighterGap * 0.5f };
    l.onlooker[0] = { cx + ux * 9.0f + px * 2.0f, cy + uy * 9.0f + py * 2.0f };
    l.onlooker[1] = { cx - ux * 9.0f - px * 3.0f, cy - uy * 9.0f - py * 3.0f };
    return l;
}

enum class Role : std::uint8_t { Challenger, Opponent, Onlooker };

struct Assignment
{
    std::uint32_t city = 0;
    Role          role = Role::Onlooker;
    std::uint64_t partner = 0;   // the other duellist (0 for an onlooker)
    float         x = 0, y = 0, z = 0;
};

struct GateSpot { bool known = false; std::uint32_t map = 0; float x = 0, y = 0, z = 0; bool alliance = true; };

// What the driver tells the idle rules: who stands where, where the gates are, and which players a bot has already challenged.
class Board
{
public:
    static Board& Get() { static Board b; return b; }

    void Set(std::uint64_t bot, Assignment const& a) { std::lock_guard<std::mutex> g(m_); by_bot_[bot] = a; }
    bool Find(std::uint64_t bot, Assignment& out) const
    {
        std::lock_guard<std::mutex> g(m_);
        auto const it = by_bot_.find(bot);
        if (it == by_bot_.end()) return false;
        out = it->second;
        return true;
    }
    void Clear(std::uint64_t bot) { std::lock_guard<std::mutex> g(m_); by_bot_.erase(bot); }
    void ClearAll() { std::lock_guard<std::mutex> g(m_); by_bot_.clear(); gates_.clear(); }

    void SetGate(std::uint32_t city, GateSpot const& s) { std::lock_guard<std::mutex> g(m_); gates_[city] = s; }
    void ClearGate(std::uint32_t city) { std::lock_guard<std::mutex> g(m_); gates_.erase(city); }
    // Is (x, y) on `map` within `radius` of a known gate of that faction?
    bool NearGate(std::uint32_t map, float x, float y, float radius, bool alliance) const
    {
        std::lock_guard<std::mutex> g(m_);
        for (auto const& [city, s] : gates_)
            if (s.known && s.map == map && s.alliance == alliance && std::hypot(s.x - x, s.y - y) <= radius) return true;
        return false;
    }

    // The known gate of that faction nearest (x, y) on `map` within `radius`, for the town errands (a resident now and then runs out to it).
    bool GateNear(std::uint32_t map, float x, float y, float radius, bool alliance, GateSpot& out, std::uint32_t& city_out) const
    {
        std::lock_guard<std::mutex> g(m_);
        float best = radius;
        bool found = false;
        for (auto const& [city, s] : gates_)
        {
            if (!s.known || s.map != map || s.alliance != alliance) continue;
            float const d = std::hypot(s.x - x, s.y - y);
            if (d <= best) { best = d; out = s; city_out = city; found = true; }
        }
        return found;
    }

    // A bot may challenge `player` when no bot has in the last `cooldown_ms`; the call records it. One bot wins the race.
    bool ChallengeTake(std::uint64_t player, std::uint32_t now_ms, std::uint32_t cooldown_ms)
    {
        std::lock_guard<std::mutex> g(m_);
        if (challenged_.size() > 256)
            for (auto it = challenged_.begin(); it != challenged_.end();)
                it = (now_ms - it->second > cooldown_ms) ? challenged_.erase(it) : std::next(it);
        auto const it = challenged_.find(player);
        if (it != challenged_.end() && now_ms - it->second < cooldown_ms) return false;
        challenged_[player] = now_ms;
        return true;
    }

private:
    mutable std::mutex m_;
    std::unordered_map<std::uint64_t, Assignment> by_bot_;
    std::unordered_map<std::uint32_t, GateSpot> gates_;
    std::unordered_map<std::uint64_t, std::uint32_t> challenged_;
};

} // namespace Playerbot::V2::GateDuels
