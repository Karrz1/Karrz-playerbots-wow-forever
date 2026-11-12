// TownErrand - a capital's residents go about their business: each bot runs to a real place in the city (the auction house, the bank,
// its class trainer, the inn, a mailbox, the flight master, a vendor), does what it came for, and runs on to the next. The walk is one
// pathed move to the place, so the bot runs a smooth line through the streets instead of 25 y hops with a turn at each.
//
// Places are shared: a spot table caps how many bots stand at one place at once (the inn took every resting bot of the city: 48 bots
// in the Gilded Rose in 20 minutes, live 273), and a bot that finds a place full picks another.
//
// Pure part (kinds, weights, the pick, the stand slot, the spot table); the rule is idle:town_errand in Bot/States/Rules/AmbientRules.cpp.
// Tested in tests/game/ClassicTownErrand.cpp.

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Playerbot::V2::TownErrand
{

enum class Kind : std::uint8_t { None = 0, Auction, Bank, Trainer, Inn, Mailbox, Flight, Vendor, Battlemaster, Stable, Exit };

inline char const* KindName(Kind k)
{
    switch (k)
    {
        case Kind::Auction:      return "auction house";
        case Kind::Bank:         return "bank";
        case Kind::Trainer:      return "trainer";
        case Kind::Inn:          return "inn";
        case Kind::Mailbox:      return "mailbox";
        case Kind::Flight:       return "flight master";
        case Kind::Vendor:       return "vendor";
        case Kind::Battlemaster: return "battlemaster";
        case Kind::Stable:       return "stable master";
        case Kind::Exit:         return "city gate";
        default:                 return "-";
    }
}

struct Dest
{
    Kind          kind = Kind::None;
    std::uint64_t key = 0;        // the NPC or mailbox spawn id; for the gate, a fixed key
    std::uint32_t entry = 0;      // creature entry, to face the live NPC on arrival (0 = a mailbox or the gate)
    float         x = 0.f, y = 0.f, z = 0.f;
    float         weight = 0.f;   // how much this place draws a bot right now (BaseWeight, raised by a need)
};

// How much each kind of place draws a resident. The bank and the auction house are where a city's crowd is; a bot's own trainer and
// the flight master are errands with a purpose; vendors are many, so each draws little.
inline float BaseWeight(Kind k)
{
    switch (k)
    {
        case Kind::Auction:      return 3.0f;
        case Kind::Bank:         return 3.0f;
        case Kind::Trainer:      return 4.0f;
        case Kind::Inn:          return 2.0f;
        case Kind::Mailbox:      return 3.0f;
        case Kind::Flight:       return 3.0f;
        case Kind::Vendor:       return 0.6f;
        case Kind::Battlemaster: return 1.5f;
        case Kind::Stable:       return 1.5f;
        case Kind::Exit:         return 2.0f;
        default:                 return 0.0f;
    }
}

constexpr float kNearYards = 30.0f;   // a place this close is next door: rarely the next errand, so the bot crosses the city

// Distance factor: a place next door is seldom chosen, a place across the city a little more often than one round the corner.
inline float DistanceFactor(float d, float max_yards)
{
    if (d > max_yards) return 0.0f;
    if (d < kNearYards) return 0.15f;
    return 1.0f + d / 300.0f;
}

// Recently visited places, so a bot does not go back and forth between two.
struct Recent
{
    std::array<std::uint64_t, 6> keys{};
    std::size_t head = 0;
    bool has(std::uint64_t k) const
    {
        for (std::uint64_t r : keys) if (r && r == k) return true;
        return false;
    }
    void add(std::uint64_t k) { keys[head] = k; head = (head + 1) % keys.size(); }
};

// Index of the next place, drawn by weight x distance factor; `roll` in [0, 1). Places visited lately, and places `full` says are
// taken, are skipped. -1 when nothing fits.
template <typename IsFull>
inline int Pick(std::vector<Dest> const& dests, float bx, float by, float max_yards, Recent const& recent, float roll, IsFull&& full)
{
    std::vector<float> w(dests.size(), 0.0f);
    float total = 0.0f;
    for (std::size_t i = 0; i < dests.size(); ++i)
    {
        Dest const& d = dests[i];
        if (d.weight <= 0.0f || recent.has(d.key) || full(d)) continue;
        w[i] = d.weight * DistanceFactor(std::hypot(d.x - bx, d.y - by), max_yards);
        total += w[i];
    }
    if (total <= 0.0f) return -1;
    float t = roll * total;
    int last = -1;
    for (std::size_t i = 0; i < dests.size(); ++i)
    {
        if (w[i] <= 0.0f) continue;
        last = int(i);
        t -= w[i];
        if (t < 0.0f) return int(i);
    }
    return last;
}

// Where a bot stands at a place: on the side it comes from, at `ring` yards, turned by its slot so the bots at one place stand side by
// side instead of on one point. `slot` 0.. (the order the place was claimed in).
inline void StandPoint(float px, float py, float bx, float by, float ring, std::uint32_t slot, float& sx, float& sy)
{
    float const dx = bx - px, dy = by - py, len = std::hypot(dx, dy);
    float base = len > 0.5f ? std::atan2(dy, dx) : 0.0f;
    // 0, +0.55, -0.55, +1.1, -1.1 ... radians: a fan on the near side
    float const step = float((slot + 1u) / 2u) * 0.55f;
    base += (slot % 2u) ? step : -step;
    sx = px + std::cos(base) * ring;
    sy = py + std::sin(base) * ring;
}

// The places' occupancy, shared by every bot. A claim lapses when its holder stops renewing it (it walked off, logged out, died):
// each renewal holds it `hold_ms` more. A place is a cell of the map, so the five bankers of one bank are one place.
class Spots
{
public:
    static Spots& Get() { static Spots s; return s; }

    static std::uint64_t PlaceKey(std::uint32_t map, float x, float y, float cell = 20.0f)
    {
        std::int32_t const cx = std::int32_t(std::floor(x / cell)), cy = std::int32_t(std::floor(y / cell));
        return (std::uint64_t(map & 0xFFFu) << 52) ^ (std::uint64_t(std::uint32_t(cx) & 0x3FFFFFFu) << 26) ^ std::uint64_t(std::uint32_t(cy) & 0x3FFFFFFu);
    }

    // Take or renew a place for `bot`. True when the bot holds it (already did, or there was room under `cap`); `slot` is its place in
    // the order of the holders, for StandPoint.
    bool Claim(std::uint64_t place, std::uint64_t bot, std::uint32_t now_ms, std::uint32_t hold_ms, std::uint32_t cap, std::uint32_t* slot = nullptr)
    {
        std::lock_guard<std::mutex> g(m_);
        Prune(now_ms);
        auto& v = by_place_[place];
        std::uint32_t live = 0;
        for (Holder& h : v)
        {
            if (h.bot == bot) { h.until_ms = now_ms + hold_ms; if (slot) *slot = h.slot; return true; }
            if (Live(h, now_ms)) ++live;
        }
        if (live >= cap) return false;
        std::uint32_t free_slot = 0;
        for (bool taken = true; taken; )
        {
            taken = false;
            for (Holder const& h : v) if (Live(h, now_ms) && h.slot == free_slot) { taken = true; ++free_slot; break; }
        }
        std::erase_if(v, [&](Holder const& h) { return !Live(h, now_ms); });
        v.push_back({ bot, now_ms + hold_ms, free_slot });
        if (slot) *slot = free_slot;
        return true;
    }

    // Would a claim by `bot` be refused now?
    bool Full(std::uint64_t place, std::uint64_t bot, std::uint32_t now_ms, std::uint32_t cap) const
    {
        std::lock_guard<std::mutex> g(m_);
        auto const it = by_place_.find(place);
        if (it == by_place_.end()) return cap == 0;
        std::uint32_t live = 0;
        for (Holder const& h : it->second)
        {
            if (h.bot == bot) return false;
            if (Live(h, now_ms)) ++live;
        }
        return live >= cap;
    }

    void Release(std::uint64_t place, std::uint64_t bot)
    {
        std::lock_guard<std::mutex> g(m_);
        auto const it = by_place_.find(place);
        if (it == by_place_.end()) return;
        std::erase_if(it->second, [&](Holder const& h) { return h.bot == bot; });
    }

    std::uint32_t Count(std::uint64_t place, std::uint32_t now_ms) const
    {
        std::lock_guard<std::mutex> g(m_);
        auto const it = by_place_.find(place);
        if (it == by_place_.end()) return 0;
        std::uint32_t n = 0;
        for (Holder const& h : it->second) if (Live(h, now_ms)) ++n;
        return n;
    }

private:
    struct Holder { std::uint64_t bot; std::uint32_t until_ms; std::uint32_t slot; };
    static bool Live(Holder const& h, std::uint32_t now_ms) { return std::int32_t(h.until_ms - now_ms) > 0; }
    void Prune(std::uint32_t now_ms)
    {
        if (++calls_ % 512u) return;
        for (auto it = by_place_.begin(); it != by_place_.end();)
        {
            std::erase_if(it->second, [&](Holder const& h) { return !Live(h, now_ms); });
            it = it->second.empty() ? by_place_.erase(it) : std::next(it);
        }
    }
    mutable std::mutex m_;
    std::unordered_map<std::uint64_t, std::vector<Holder>> by_place_;
    std::uint32_t calls_ = 0;
};

// A bot's errand, owned by its bot tick (BotAI::town_errand()).
struct State
{
    Kind          kind = Kind::None;
    std::uint64_t key = 0;
    std::uint64_t place = 0;
    std::uint32_t entry = 0;
    std::uint32_t slot = 0;
    float         nx = 0.f, ny = 0.f, nz = 0.f;   // the NPC / mailbox / gate
    float         tx = 0.f, ty = 0.f, tz = 0.f;   // where the bot stands there
    std::uint8_t  phase = 0;                      // 0 none, 1 on the way, 2 there
    std::uint32_t phase_start_ms = 0;
    std::uint32_t linger_until_ms = 0;
    std::uint32_t last_emit_ms = 0;
    float         prog_x = 0.f, prog_y = 0.f;     // where the bot was when the progress clock last moved
    std::uint32_t prog_ms = 0;
    std::uint32_t near_player_ms = 0;             // since when a real player has stood next to the lingering bot
    std::uint32_t errands = 0;                    // done since the server started, for the log
    std::uint32_t inn_skip_until_ms = 0;          // a real player stood next to the resting bot: no inn for it until then
    bool          issued = false;                 // the move to the place has gone out
    bool          leaving = false;                // this errand is the walk out of the city (the bot's town time is over)
    std::uint32_t leave_until_ms = 0;             // out at the gate: no errands, the bot heads off to level until then
    Recent        recent;

    void Clear() { kind = Kind::None; key = 0; place = 0; entry = 0; phase = 0; near_player_ms = 0; issued = false; leaving = false; prog_ms = 0; }
};

// Has the walking bot stopped getting closer? It counts as progress to move `yards` in `window_ms`.
inline bool Stalled(State& st, float x, float y, std::uint32_t now_ms, float yards = 3.0f, std::uint32_t window_ms = 8000u)
{
    float const dx = x - st.prog_x, dy = y - st.prog_y;
    if (st.prog_ms == 0 || dx * dx + dy * dy >= yards * yards)
    {
        st.prog_x = x; st.prog_y = y; st.prog_ms = now_ms ? now_ms : 1u;
        return false;
    }
    return now_ms - st.prog_ms >= window_ms;
}

} // namespace Playerbot::V2::TownErrand
