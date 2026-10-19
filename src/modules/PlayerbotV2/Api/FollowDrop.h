// Following an owner who left the bot's level by dropping or jumping: where the bot leaves the ledge and where it lands.
//
// the owner: he jumped down into the Blackrock Stadium (about 11 y lower) and his bots stayed on the level above, 26 to 46 y
// away. The follow only knows the walking route, which here leads round through the Blackhand packs, so they walked into them and died.
// A player takes the short way: off the ledge. This is that decision as a pure function over a ground-height probe and a line-of-sight
// probe (templated, so tests/game/ClassicFollowDrop.cpp runs it on a plain height map). Used by API::move_to with follow_drop set.
//
// The caller has already found that the walking route to the owner is missing or a long detour (`walk_is_fine` false) and that the
// owner is within reach. The plan fans bearings from the bot toward the owner, walks each ray over ground that stays level until it
// drops off, then looks for a landing on the owner's floor just past that edge:
//   Drop: the landing is 4 y or more below the edge (a committed drop, API::move_to drop=true).
//   Leap: the landing is level with the edge but past a gap that is at most kLeapMax wide (a jump a player makes across a gap).
// Of all the candidates the one that ends nearest the owner (plus half the walk to its edge) wins.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Playerbot::FollowDrop
{
    struct Vec { float x = 0.f, y = 0.f, z = 0.f; };

    enum class Mode : uint8_t { None, Walk, Drop, Leap };

    inline char const* ModeName(Mode m)
    {
        switch (m)
        {
            case Mode::Walk: return "walk";
            case Mode::Drop: return "drop";
            case Mode::Leap: return "leap";
            default:         return "none";
        }
    }

    struct Plan
    {
        Mode  mode = Mode::None;
        Vec   ledge;            // where the bot leaves the ground (the last level point on the ray)
        Vec   land;             // where it comes down
        float walk = 0.f;       // yards from the bot to the ledge
        bool  leap = false;     // the crossing is a Leap (level jump over a gap), otherwise a Drop
    };

    constexpr float kMaxOwnerDistXY = 50.0f;   // how far from the owner a bot still looks for a ledge
    constexpr float kMaxRay         = 30.0f;   // how far the bot looks for an edge along a bearing
    constexpr float kRayStep        = 1.0f;
    constexpr float kLedgeDrop      = 3.0f;    // ground this much lower than the last step is an edge
    constexpr float kStepUp         = 1.5f;    // ground this much higher is a wall or a rail: the bearing is blocked
    constexpr float kMinFall        = 4.0f;    // a Drop falls at least this far (API::move_to drop=true refuses less)
    constexpr float kMaxFall        = 40.0f;
    constexpr float kOwnerFloorTol  = 3.5f;    // a landing counts as the owner's floor within this much height
    constexpr float kLeapMax        = 8.0f;    // the widest gap a bot leaps (a player's jump)
    constexpr float kLeapLevelTol   = 3.0f;    // a Leap lands within this much of the edge height
    constexpr float kArriveYd       = 2.5f;    // this close to the ledge the bot leaves it
    constexpr float kWalkWeight     = 0.5f;

    // ground(x, y, z_hint, z_out) -> bool: the highest walkable surface at or a little above z_hint, searching down; false when none.
    // clear(a, b) -> bool: the straight line from a to b is free of walls.
    template <class GroundFn, class ClearFn>
    Plan PlanFollowDrop(Vec const& bot, Vec const& owner, bool walk_is_fine, GroundFn&& ground, ClearFn&& clear)
    {
        Plan best;
        if (walk_is_fine)
            return best;
        float const odx = owner.x - bot.x, ody = owner.y - bot.y;
        float const owner_dxy = std::sqrt(odx * odx + ody * ody);
        if (owner_dxy > kMaxOwnerDistXY || owner_dxy < 1.0f)
            return best;

        constexpr float kPi = 3.14159265358979f;
        constexpr float kDeg = kPi / 180.0f;
        float const base = std::atan2(ody, odx);
        float offsets[15];
        {
            int n = 0;
            offsets[n++] = 0.f;
            for (int k = 1; k <= 7; ++k) { offsets[n++] = float(k) * 15.f * kDeg; offsets[n++] = -float(k) * 15.f * kDeg; }
        }
        float const landing_d[] = { 2.f, 3.f, 4.f, 5.f, 6.f, 8.f, 10.f, 12.f, 15.f };
        float best_cost = 1e9f;

        for (float off : offsets)
        {
            float const b = base + off;
            float const cx = std::cos(b), cy = std::sin(b);
            float cz = bot.z;
            Vec prev = bot;
            bool edge = false;
            int const steps = int(std::min(kMaxRay, owner_dxy + 6.0f) / kRayStep);
            for (int i = 1; i <= steps; ++i)
            {
                float const qx = bot.x + cx * kRayStep * float(i);
                float const qy = bot.y + cy * kRayStep * float(i);
                float g = 0.f;
                bool const ok = ground(qx, qy, cz, g);
                if (!ok || cz - g > kLedgeDrop) { edge = true; break; }
                if (g - cz > kStepUp)           break;               // a wall or a rail ahead
                cz = g;
                prev = { qx, qy, g };
            }
            if (!edge)
                continue;
            Vec const ledge = prev;
            float const wdx = ledge.x - bot.x, wdy = ledge.y - bot.y;
            float const walk = std::sqrt(wdx * wdx + wdy * wdy);
            // The way to the edge must be open (a wall between would have been a step up, but a thin one can slip between the samples).
            if (walk > 0.5f && !clear(Vec{ bot.x, bot.y, bot.z + 1.5f }, Vec{ ledge.x, ledge.y, ledge.z + 1.5f }))
                continue;

            for (float d : landing_d)
            {
                Vec land{ ledge.x + cx * d, ledge.y + cy * d, 0.f };
                float lz = 0.f;
                if (!ground(land.x, land.y, owner.z + 1.0f, lz))
                    continue;
                if (std::fabs(lz - owner.z) > kOwnerFloorTol)
                    continue;                                    // not the owner's floor
                land.z = lz;
                float const fall = ledge.z - lz;
                bool leap = false;
                if (fall >= kMinFall && fall <= kMaxFall)
                    leap = false;
                else if (std::fabs(fall) <= kLeapLevelTol && d <= kLeapMax)
                    leap = true;
                else
                    continue;
                // The chord leaves the lip a little ahead of it so the edge itself does not count as a wall.
                Vec const from{ ledge.x + cx * 0.5f, ledge.y + cy * 0.5f, ledge.z + 1.5f };
                if (!clear(from, Vec{ land.x, land.y, land.z + 1.5f }))
                    continue;
                float const ldx = land.x - owner.x, ldy = land.y - owner.y;
                float const cost = std::sqrt(ldx * ldx + ldy * ldy) + kWalkWeight * walk;
                if (cost < best_cost)
                {
                    best_cost = cost;
                    best.ledge = ledge;
                    best.land = land;
                    best.walk = walk;
                    best.leap = leap;
                    best.mode = walk <= kArriveYd ? (leap ? Mode::Leap : Mode::Drop) : Mode::Walk;
                }
                break;   // the nearest valid landing on this bearing
            }
        }
        return best;
    }
}
