// SpreadSpot - where in a leveling zone a freshly placed bot stands. Every bot of a level used to be teleported onto the
// town's flight master point, so dozens stood on the same coordinates. The spot is now a quest hub of the zone that suits
// the bot's level and faction, with a per-bot offset inside the hub. Pure rules, tested in tests/game/ClassicSpreadSpot.cpp.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Playerbot::V2::World {

struct SpreadHub
{
    float x = 0.f, y = 0.f, z = 0.f;
    float radius = 0.f;                 // the hub's own radius
    std::uint32_t min_level = 0, max_level = 0;
    std::uint32_t faction_mask = 0;     // bit0 Alliance, bit1 Horde, bit2 neutral; 0 = unknown, any
    float dist_to_town = 0.f;           // from the town point the bot would otherwise stand on
};

constexpr float kSpreadHubMaxTownDistance = 350.0f;   // a hub of the same zone, not one a valley away
constexpr float kSpreadMaxOffset = 12.0f;             // keep to the hub's ground: a hub centre is an average of giver positions

inline bool SpreadHubSuits(SpreadHub const& h, std::uint32_t level, bool alliance)
{
    if (h.dist_to_town > kSpreadHubMaxTownDistance) return false;
    if (h.max_level != 0 && (level < h.min_level || level > h.max_level)) return false;
    if (h.faction_mask != 0 && !(h.faction_mask & (alliance ? 1u : 2u)) && !(h.faction_mask & 4u)) return false;
    return true;
}

// Index of the hub for this bot, -1 when none suits (the caller keeps the town point). Round-robin on the bot's seed, so the same
// roster spreads evenly over the hubs and a bot picks the same hub each time.
inline int PickSpreadHub(std::vector<SpreadHub> const& hubs, std::uint32_t level, bool alliance, std::uint64_t seed)
{
    std::vector<int> ok;
    for (std::size_t i = 0; i < hubs.size(); ++i)
        if (SpreadHubSuits(hubs[i], level, alliance))
            ok.push_back(int(i));
    return ok.empty() ? -1 : ok[seed % ok.size()];
}

// Offset of the bot from the hub centre: golden-angle direction and a radius that fills the disc evenly, both from the seed,
// so neighbouring seeds never share a spot. Bounded by half the hub radius and kSpreadMaxOffset.
inline void SpreadOffset(std::uint64_t seed, float hub_radius, float& dx, float& dy)
{
    float const bound = std::min(kSpreadMaxOffset, std::max(2.0f, hub_radius * 0.5f));
    float const angle = float(std::fmod(double(seed) * 2.399963229728653, 6.283185307179586));
    float const frac = float(std::fmod(double(seed % 1000003u) * 0.6180339887498949, 1.0));
    float const r = bound * std::sqrt(0.15f + 0.85f * frac);
    dx = std::cos(angle) * r;
    dy = std::sin(angle) * r;
}

// Where a bot walking to a hub aims: the hub centre moved by the bot's own offset. A hub of a single giver has that giver's
// spot as its centre, and every bot sent there walked onto the same coordinates (six to eight bots in one pile at Calvin
// Montague in Deathknell). Same offset rule as the placement above, so a walk and a placement agree.
inline void HubStandPoint(float hub_x, float hub_y, float hub_radius, std::uint64_t seed, float& out_x, float& out_y)
{
    float dx = 0.f, dy = 0.f;
    SpreadOffset(seed, hub_radius, dx, dy);
    out_x = hub_x + dx;
    out_y = hub_y + dy;
}

// Where a bot stands to talk to a quest giver: on a ring 2 to 4 yd from the giver (inside the accept reach), on the side the bot
// comes from. The side is the bearing from the giver to the bot rounded to one of eight sectors and moved by a per-bot angle inside the
// sector, so a bot does not change its spot step by step as it walks, and bots from one direction fan out over the arc. Every bot used to
// walk to the giver's own coordinates (or to the same point 2.5 yd from him) and stood inside him (Calvin Montague, Grelin Whitebeard).
inline void GiverStandPoint(float giver_x, float giver_y, float from_x, float from_y, std::uint64_t seed, float& out_x, float& out_y)
{
    constexpr float kPi = 3.14159265358979f;
    float const bearing = std::atan2(from_y - giver_y, from_x - giver_x);
    float const sector = std::round(bearing / (kPi / 4.0f)) * (kPi / 4.0f);
    float const jit = (float(std::fmod(double(seed % 1000003u) * 0.6180339887498949, 1.0)) - 0.5f) * 1.5f;      // +-0.75 rad, sector half-width is 0.785
    float const r = 2.0f + 2.0f * float(std::fmod(double(seed % 999983u) * 0.7548776662466927, 1.0));
    out_x = giver_x + std::cos(sector + jit) * r;
    out_y = giver_y + std::sin(sector + jit) * r;
}

} // namespace Playerbot::V2::World
