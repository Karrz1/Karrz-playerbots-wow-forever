// MovingBubble - the world-bot bubble of a player who rides (Playerbot.World.Moving.*).
//
// Standing still, a player's bubble is a circle of ActivityRadius (600 y on the live preset) and ZoneBots bots in it are enough. Riding
// at 11-14 y/s he crosses that circle in under a minute, and the bots behind him still count toward his target: the bubble reads full,
// nothing is spawned, and the road ahead is empty until he stops (PC, Stranglethorn, 26 Oct). While he moves, only the bots in a
// corridor along his heading count (from BehindYards behind him to ActivityRadius ahead, CorridorYards to each side), new bots are put
// ahead of him on that corridor, and the bots left far behind log out.
// Pure: no game types, tested in tests/game/ClassicMovingBubble.cpp.
#pragma once

#include <algorithm>
#include <cmath>

namespace Playerbot::V2::Fleet
{

struct MovingRule
{
    float min_speed = 9.0f;       // y/s (smoothed) from which a player counts as moving: a 60% mount is 11.2, a run 7
    float behind_yards = 150.0f;  // bots farther behind a moving player than this do not count toward his target
    float corridor_yards = 250.0f;// half width of the counted corridor
    float lead_sec = 25.0f;       // new bots are put up to this many seconds of his travel ahead
    float lead_min = 130.0f;      // and at least this far ahead (beyond the visibility range, so they do not pop into view)
};

// Smoothed velocity of one player from his tick-to-tick positions.
struct Motion
{
    float vx = 0.0f, vy = 0.0f;   // y/s
    bool  moving = false;
    float Speed() const { return std::hypot(vx, vy); }
};

// One step: (dx, dy) moved in dt_ms. A step longer than jump_yards is a teleport (not travel): the motion starts again from rest.
// Exponential smoothing over about two seconds, so one frame of strafing does not swing the heading; moving switches on at
// min_speed and off below 60% of it.
inline Motion StepMotion(Motion m, float dx, float dy, unsigned dt_ms, float min_speed, float jump_yards = 100.0f)
{
    if (dt_ms == 0)
        return m;
    if (std::hypot(dx, dy) > jump_yards)
        return Motion{};
    float const dt = float(dt_ms) / 1000.0f;
    float const a = std::min(1.0f, dt / 2.0f);
    m.vx += (dx / dt - m.vx) * a;
    m.vy += (dy / dt - m.vy) * a;
    float const s = m.Speed();
    m.moving = m.moving ? s >= min_speed * 0.6f : s >= min_speed;
    return m;
}

// (x, y) relative to a player at (ax, ay) heading along (vx, vy): `along` ahead (negative behind), `side` across.
inline void AlongSide(float ax, float ay, float vx, float vy, float x, float y, float& along, float& side)
{
    float const s = std::hypot(vx, vy);
    float const ux = s > 0.01f ? vx / s : 1.0f, uy = s > 0.01f ? vy / s : 0.0f;
    float const dx = x - ax, dy = y - ay;
    along = dx * ux + dy * uy;
    side = std::fabs(-dx * uy + dy * ux);
}

// Does a bot at (x, y) count toward the target of a player at (ax, ay)? Standing: within the radius. Moving: within the radius and
// in his corridor.
inline bool InBubble(float ax, float ay, Motion const& m, float x, float y, float radius, MovingRule const& r)
{
    float const dx = x - ax, dy = y - ay;
    if (dx * dx + dy * dy > radius * radius)
        return false;
    if (!m.moving)
        return true;
    float along = 0.0f, side = 0.0f;
    AlongSide(ax, ay, m.vx, m.vy, x, y, along, side);
    return along >= -r.behind_yards && side <= r.corridor_yards;
}

// How far ahead the new bots of a moving player go: [lo, hi] along his heading, inside the radius.
inline void LeadRange(Motion const& m, float radius, MovingRule const& r, float& lo, float& hi)
{
    lo = std::min(r.lead_min, radius * 0.6f);
    hi = std::clamp(m.Speed() * r.lead_sec, lo + 60.0f, std::max(lo + 60.0f, radius * 0.85f));
}

// A spot ahead of the player: u_along and u_side are uniform in [0, 1). The side spread is narrower than the corridor (60% of it),
// so the spot is on the road he rides, not at the corridor's edge.
inline void LeadSpot(float ax, float ay, Motion const& m, float radius, MovingRule const& r, float u_along, float u_side,
                     float& x, float& y)
{
    float lo = 0.0f, hi = 0.0f;
    LeadRange(m, radius, r, lo, hi);
    float const s = m.Speed();
    float const ux = s > 0.01f ? m.vx / s : 1.0f, uy = s > 0.01f ? m.vy / s : 0.0f;
    float const along = lo + (hi - lo) * u_along;
    float const side = (u_side * 2.0f - 1.0f) * r.corridor_yards * 0.6f;
    x = ax + ux * along - uy * side;
    y = ay + uy * along + ux * side;
}

// A bot the moving player left behind: more than `yards` behind him (0 = never).
inline bool LeftBehind(float ax, float ay, Motion const& m, float x, float y, float yards)
{
    if (!m.moving || yards <= 0.0f)
        return false;
    float along = 0.0f, side = 0.0f;
    AlongSide(ax, ay, m.vx, m.vy, x, y, along, side);
    return along < -yards;
}

} // namespace Playerbot::V2::Fleet
