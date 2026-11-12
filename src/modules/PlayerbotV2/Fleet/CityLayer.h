// Two small decisions for bots in a capital, kept free of game types so they can be unit tested.
//
// A capital is built in layers (Stormwind: the city floor at z 94 to 100, other walkable layers under it at z 75 and z 59, joined to
// the city only by a few steep faces). A bot that ends up on a lower layer cannot reach the places the town rules send it to.
//  * AcrossDrop: a move that asks for the bot's own level (the rule passed the bot's z, as the chunked walks and the wanderers do)
//    whose destination has its ground far below. The ground probe would put the goal on the lower layer and the path would take the
//    bot down the face. That is not a place the rule meant.
//  * NoPathRun: how many times in a row of minutes the path calls from one spot failed with no route. A bot whose every path fails
//    is as cut off as one the stranded watch finds by its own probe (a pocket with void all round it).
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Playerbot::V2::Fleet
{

constexpr float kSameLevelYards = 3.0f;    // a destination hint this close to the bot's own z is a request for the same level
constexpr float kCityDropYards  = 20.0f;   // ground this far under such a hint is across a drop (Stormwind: 23 and 38 y)

inline bool AcrossDrop(float bot_z, float hint_z, float ground_z)
{
    return std::fabs(hint_z - bot_z) < kSameLevelYards && hint_z - ground_z > kCityDropYards;
}

constexpr float kLiquidDropYards = 6.0f;   // a liquid floor this far under a same-level ask is a pit, not the street

// The same for a pit of liquid under the asked point: the Great Forge of Ironforge is lava at z 483, 18.5 y under the walkways at
// 501.7, less than kCityDropYards, so a step over its rim was snapped down into it and every path there ended in the liquid
// (outcome=Liquid, live 275: a bot party ran back and forth at the Military Ward for half an hour).
inline bool IntoLiquidPit(float bot_z, float hint_z, float ground_z, bool ground_in_liquid)
{
    return ground_in_liquid && std::fabs(hint_z - bot_z) < kSameLevelYards && hint_z - ground_z > kLiquidDropYards;
}

constexpr float kCliffDropYards = 15.0f;   // a path segment falling this far over no more ground than it falls is a face, not a ramp

// Is a path segment a cliff face? Detour joins the layers of a capital over steep faces that a bot cannot walk; a ramp or a stair falls
// slower than it runs. `run` is the ground covered, `drop` how far the segment falls (positive = down).
inline bool IsCliffSegment(float run, float drop)
{
    return drop >= kCliffDropYards && run <= drop;
}

// Does a path (points with x, y, z) cross a cliff face, going down or up? Detour joins layers over steep faces that a bot cannot walk.
template <typename Points>
inline bool PathHasCliff(Points const& pts)
{
    for (std::size_t i = 1; i < pts.size(); ++i)
    {
        float const run = std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        float const dz = pts[i].z - pts[i - 1].z;
        if (IsCliffSegment(run, -dz) || IsCliffSegment(run, dz))
            return true;
    }
    return false;
}

constexpr std::uint32_t kNoPathWindowMs = 180000u;   // failures are counted per window of three minutes
constexpr std::uint32_t kNoPathStorm    = 30u;       // this many in one window (a bot in a pocket fails once a second)

struct NoPathRun
{
    std::uint32_t n = 0;
    std::uint32_t since_ms = 0;   // when the current window opened; 0 = none yet
};

inline void NoteNoPath(NoPathRun& r, std::uint32_t now_ms)
{
    if (r.since_ms == 0 || std::uint32_t(now_ms - r.since_ms) > kNoPathWindowMs)
    {
        r.n = 0;
        r.since_ms = now_ms ? now_ms : 1u;
    }
    ++r.n;
}

inline bool NoPathStorm(NoPathRun const& r, std::uint32_t now_ms)
{
    return r.since_ms != 0 && std::uint32_t(now_ms - r.since_ms) <= kNoPathWindowMs && r.n >= kNoPathStorm;
}

} // namespace Playerbot::V2::Fleet
