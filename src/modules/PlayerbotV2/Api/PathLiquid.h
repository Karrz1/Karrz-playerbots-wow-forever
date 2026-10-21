// PathLiquid - pure rules for keeping town movement out of canals. The navmesh happily wades them, and the path's corner points
// are often dry on both banks, so checking the corners alone misses a straight crossing: the whole segment is sampled.
// Tested in tests/game/ClassicPathLiquid.cpp.

#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace Playerbot::BotMovement {

struct PathPoint { float x, y, z; };

// How a bot's ground path treats liquid. Set per bot by the module's policy hook.
enum class LiquidPolicy : unsigned char
{
    None,        // liquid is not a concern (open world, groups in the field, instances, a bot already in the water)
    PreferDry,   // a follower in a town: take a dry route when one exists, otherwise follow through the water
    RequireDry   // town life of an ungrouped bot: no dry route, no move
};

// The policy a bot gets from where it is and whose it is. Open-world leveling zones, dungeons, battlegrounds and a bot already in the
// water are unrestricted; in a town an ungrouped world bot must stay dry, while a follower (grouped or owned) only prefers it: when no
// dry route exists it follows through the water, so a companion is never left behind at a canal, Blackfathom or Deadmines.
constexpr LiquidPolicy PickLiquidPolicy(bool in_water, bool in_bg_or_instance, bool leveling_zone, bool grouped, bool owned)
{
    if (in_water || in_bg_or_instance || leveling_zone) return LiquidPolicy::None;
    return (grouped || owned) ? LiquidPolicy::PreferDry : LiquidPolicy::RequireDry;
}

// True when any point of the path past the first corner, sampled every `step` yards along each segment, is wet.
// `wet(x, y, z)` answers for one point.
template <typename WetFn>
bool PathTouchesLiquid(std::vector<PathPoint> const& pts, WetFn wet, float step = 2.0f)
{
    for (size_t i = 1; i < pts.size(); ++i)
    {
        PathPoint const& a = pts[i - 1];
        PathPoint const& b = pts[i];
        float const len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        size_t const n = step > 0.0f ? size_t(len / step) : 0;
        // The start itself (i == 1, t == 0) is where the bot stands: a bot leaving the water must be able to start.
        for (size_t k = (i == 1 ? 1 : 0); k <= n; ++k)
        {
            float const t = n ? float(k) / float(n) : 1.0f;
            if (wet(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t))
                return true;
        }
        if (n == 0 && wet(b.x, b.y, b.z))
            return true;
    }
    return false;
}

// Magma, slime and deep water only hurt the living: Player::EnvironmentalDamage returns at once for a dead player
// (IsImmuneToEnvironmentalDamage = !isTargetableForAttack), and the breath timer is disabled for a ghost. A ghost running to
// its corpse therefore walks over liquid; refusing a corpse run because the corpse lies in lava left the ghost standing at the
// graveyard (live 132g, Burning Steppes: path_fail Magma for the same corpse every 8 s, [move_abandon] ghost=true).
constexpr bool LiquidMattersFor(bool alive)
{
    return alive;
}

enum class LiquidAction : unsigned char { Go, UseDryRoute, Refuse };

// path_wet: the first route touches liquid; dry_found: a route that avoids it exists.
constexpr LiquidAction DecideLiquid(LiquidPolicy policy, bool path_wet, bool dry_found)
{
    if (policy == LiquidPolicy::None || !path_wet)
        return LiquidAction::Go;
    if (dry_found)
        return LiquidAction::UseDryRoute;
    return policy == LiquidPolicy::RequireDry ? LiquidAction::Refuse : LiquidAction::Go;
}

} // namespace Playerbot::BotMovement
