// Boss-relative positioning for raid encounters (FaceAway / StandBehind in RaidEncounters.h). Pure geometry, so the
// numbers are tested without a world. Angles are radians, orientation as in the core: direction (cos o, sin o).
//
// Magmadar (MC): the main tank takes the boss to the far side from the raid, so the boss turns its back on everybody else,
// and the rest of the raid stands in the sector behind the boss.

#pragma once

#include <algorithm>
#include <cmath>

namespace Playerbot::Raid::geo {

constexpr float kPi = 3.14159265358979f;

inline float NormalizeAngle(float a)
{
    if (!std::isfinite(a)) return 0.0f;   // a bare while-loop never ends on inf
    a = std::remainder(a, 2.0f * kPi);
    return a;
}

inline float Bearing(float fromX, float fromY, float toX, float toY) { return std::atan2(toY - fromY, toX - fromX); }

// How far (0..pi) the point lies off the line straight behind the boss. 0 = directly behind, pi = directly in front.
inline float OffRear(float bossX, float bossY, float bossFacing, float px, float py)
{
    return std::fabs(NormalizeAngle(Bearing(bossX, bossY, px, py) - (bossFacing + kPi)));
}

inline bool InRearSector(float bossX, float bossY, float bossFacing, float px, float py, float halfAngle, float minDist, float maxDist)
{
    float const d = std::hypot(px - bossX, py - bossY);
    return d >= minDist && d <= maxDist && OffRear(bossX, bossY, bossFacing, px, py) <= halfAngle;
}

// The spot behind the boss a bot walks to: keeps its own side (nearest edge of the sector, pulled in to 60% of the half
// angle so a bot does not stand on the line and flip every time the boss turns a little) and its distance clamped into
// [minDist, maxDist]. A bot already inside the sector keeps its bearing.
inline void RearSpot(float bossX, float bossY, float bossFacing, float px, float py, float halfAngle, float minDist, float maxDist,
                     float& outX, float& outY)
{
    float const rear = bossFacing + kPi;
    float off = NormalizeAngle(Bearing(bossX, bossY, px, py) - rear);
    float const edge = halfAngle * 0.6f;
    if (off > edge) off = edge;
    if (off < -edge) off = -edge;
    float const d = std::clamp(std::hypot(px - bossX, py - bossY), minDist, maxDist);
    outX = bossX + d * std::cos(rear + off);
    outY = bossY + d * std::sin(rear + off);
}

// The tank's spot: <reach> yards from the boss on the line from the raid's centre through the boss, so the boss stands between.
// turn: radians off that line (a spot with no floor on the line may have one a little to the side).
inline void FaceAwaySpot(float bossX, float bossY, float raidX, float raidY, float reach, float turn, float& outX, float& outY)
{
    float const away = Bearing(raidX, raidY, bossX, bossY) + turn;
    outX = bossX + reach * std::cos(away);
    outY = bossY + reach * std::sin(away);
}

// The tank's spot on a locked axis (the bearing from the boss toward the tank's side), <turn> radians off it.
inline void AxisSpot(float bossX, float bossY, float axis, float reach, float turn, float& outX, float& outY)
{
    outX = bossX + reach * std::cos(axis + turn);
    outY = bossY + reach * std::sin(axis + turn);
}

// The tank is on the axis: its bearing from the boss within <tolerance> radians of it.
inline bool TankOnAxis(float bossX, float bossY, float tankX, float tankY, float axis, float tolerance)
{
    return std::fabs(NormalizeAngle(Bearing(bossX, bossY, tankX, tankY) - axis)) <= tolerance;
}

// A locked axis stays valid while the boss has not moved further than <maxDrift> from where it was locked.
inline bool AxisHolds(float lockX, float lockY, float bossX, float bossY, float maxDrift)
{
    return std::hypot(bossX - lockX, bossY - lockY) <= maxDrift;
}

// True when the boss is already turned away: the tank (the boss's victim) is on the opposite side of it from the raid's centre,
// within <tolerance> radians of the straight line.
inline bool TankOpposesRaid(float bossX, float bossY, float tankX, float tankY, float raidX, float raidY, float tolerance)
{
    float const toTank = Bearing(bossX, bossY, tankX, tankY);
    float const toRaid = Bearing(bossX, bossY, raidX, raidY);
    return std::fabs(NormalizeAngle(toTank - toRaid)) >= kPi - tolerance;
}

// The point lies inside the sector in front of the boss: within <radius> yards and <halfAngle> radians either side of its facing.
inline bool InFrontSector(float bossX, float bossY, float bossFacing, float px, float py, float halfAngle, float radius)
{
    return std::hypot(px - bossX, py - bossY) <= radius && std::fabs(NormalizeAngle(Bearing(bossX, bossY, px, py) - bossFacing)) < halfAngle;
}

// The straight walk from (fx,fy) to (tx,ty) passes through the front sector. Samples every 2 yards, both ends excluded: a bot that stands
// inside the sector is the cone step's business, and a target inside it is refused before this is asked.
inline bool PathCrossesFrontSector(float bossX, float bossY, float bossFacing, float halfAngle, float radius, float fx, float fy, float tx, float ty)
{
    float const length = std::hypot(tx - fx, ty - fy);
    int const steps = std::max(1, int(std::ceil(length / 2.0f)));
    for (int i = 1; i < steps; ++i)
    {
        float const t = float(i) / float(steps);
        if (InFrontSector(bossX, bossY, bossFacing, fx + (tx - fx) * t, fy + (ty - fy) * t, halfAngle, radius))
            return true;
    }
    return false;
}

} // namespace Playerbot::Raid::geo
