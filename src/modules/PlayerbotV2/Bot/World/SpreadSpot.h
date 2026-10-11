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

} // namespace Playerbot::V2::World
