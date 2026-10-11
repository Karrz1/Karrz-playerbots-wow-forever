// "A healer with nothing to cast and mobs on it walks to its tank" (State_InCombat combat:healer_run_to_tank): when is that walk
// safe? Pure so the geometry is tested.
//
// MC Garr 2026-10-10: the healer ran to the tank across the Firesworn that were not on the tank, and died in their melee at 149 s.

#pragma once

#include <cmath>

namespace Playerbot {

constexpr float kRunToTankClearance = 10.0f;

// Squared distance from point P to the segment A-B.
inline float SegmentPointDist2(float ax, float ay, float bx, float by, float px, float py)
{
    float const dx = bx - ax, dy = by - ay;
    float const len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    float const cx = ax + t * dx - px, cy = ay + t * dy - py;
    return cx * cx + cy * cy;
}

// An enemy that is not fighting the tank and stands within kRunToTankClearance of the path blocks the walk (the ones on the tank
// are the tank's, and the healer arrives where its tank already holds them).
inline bool EnemyBlocksRunToTank(bool on_tank, float ax, float ay, float bx, float by, float ex, float ey)
{
    return !on_tank && SegmentPointDist2(ax, ay, bx, by, ex, ey) < kRunToTankClearance * kRunToTankClearance;
}

} // namespace Playerbot
