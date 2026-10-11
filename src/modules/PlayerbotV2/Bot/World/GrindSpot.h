// GrindSpot - where in a leveling zone the mobs of a bot's level actually are. Quest hubs sit in the towns, so a zone's wilds stayed
// empty (nobody stood in Wetlands' Green Belt). Spawns of hostile, ordinary creatures are bucketed into a coarse grid per map; a cell
// with enough mobs is a place to level, with the mobs' average level. Built from the world DB creature spawns, so it follows the data
// and needs no per-zone list. Pure rules, tested in tests/game/ClassicGrindSpot.cpp.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace Playerbot::V2::World {

struct GrindSample
{
    std::uint32_t map = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    std::uint32_t level = 0;
};

struct GrindSpot
{
    std::uint32_t map = 0;
    float x = 0.f, y = 0.f, z = 0.f;    // a real spawn near the middle of the cell
    std::uint32_t level = 0;            // average mob level
    std::uint32_t mobs = 0;
};

constexpr float kGrindCell = 90.0f;               // yards per grid cell
constexpr std::uint32_t kGrindMinMobs = 8;        // fewer than this is a stray, not a camp
constexpr std::uint32_t kGrindLevelSpread = 2;    // a spot suits bots within this many levels of its mobs
constexpr float kGrindMaxTownDistance = 900.0f;   // spots of the town's own region, not another continent's far end

inline std::vector<GrindSpot> BuildGrindSpots(std::vector<GrindSample> const& samples)
{
    struct Acc { std::vector<GrindSample> v; };
    std::map<std::tuple<std::uint32_t, int, int>, Acc> cells;
    for (GrindSample const& s : samples)
        cells[{ s.map, int(std::floor(s.x / kGrindCell)), int(std::floor(s.y / kGrindCell)) }].v.push_back(s);

    std::vector<GrindSpot> out;
    for (auto const& [key, acc] : cells)
    {
        if (acc.v.size() < kGrindMinMobs) continue;
        double cx = 0, cy = 0, lv = 0;
        for (GrindSample const& s : acc.v) { cx += s.x; cy += s.y; lv += s.level; }
        cx /= acc.v.size(); cy /= acc.v.size();
        GrindSample const* nearest = &acc.v[0];
        double best = 1e30;
        for (GrindSample const& s : acc.v)
        {
            double const d = (s.x - cx) * (s.x - cx) + (s.y - cy) * (s.y - cy);
            if (d < best) { best = d; nearest = &s; }
        }
        out.push_back({ std::get<0>(key), nearest->x, nearest->y, nearest->z, std::uint32_t(lv / acc.v.size() + 0.5), std::uint32_t(acc.v.size()) });
    }
    return out;
}

// Index of the spot for this bot, -1 when none suits. Round-robin on the seed over every spot of the bot's level near the town.
inline int PickGrindSpot(std::vector<GrindSpot> const& spots, std::uint32_t map, float town_x, float town_y, std::uint32_t level, std::uint64_t seed)
{
    std::vector<int> ok;
    for (std::size_t i = 0; i < spots.size(); ++i)
    {
        GrindSpot const& g = spots[i];
        if (g.map != map) continue;
        std::uint32_t const lo = g.level > level ? g.level - level : level - g.level;
        if (lo > kGrindLevelSpread) continue;
        if (std::hypot(g.x - town_x, g.y - town_y) > kGrindMaxTownDistance) continue;
        ok.push_back(int(i));
    }
    return ok.empty() ? -1 : ok[seed % ok.size()];
}

} // namespace Playerbot::V2::World
