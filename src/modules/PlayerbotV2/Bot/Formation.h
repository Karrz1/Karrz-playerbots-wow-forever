// Formation — squad-control geometry. Translates (slot, type) → a
// follow offset (distance, angle relative to leader's facing) so each
// bot in an owner's squad takes a distinct, stable position around
// the leader.
//
// Slot ids are owner-scoped: each bot remembers its slot, the formation
// type changes via /formation, and the same slot keeps the bot in the
// same relative position even as the formation changes (slot 0 is
// always the "primary" position — directly behind for Column, hard
// flank for Wedge, opposite-pole for Circle, etc).
//
// Per OWNER_SQUAD_CONTROL_PLAN.md Phase D.

#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>

namespace Playerbot {

enum class FormationType : uint8_t
{
    Free    = 0,   // Legacy — MotionMaster decides; no slot offset.
    Tight   = 1,   // All bots stack on top of leader.
    Spread  = 2,   // Ring 8y around leader, slot * 45° each.
    Line    = 3,   // Side-by-side behind leader, slots fan laterally.
    Column  = 4,   // Single-file behind leader.
    Wedge   = 5,   // V-shape behind leader; alternating flanks.
    Circle  = 6,   // Ring 12y around leader, evenly distributed.
};

struct FormationOffset
{
    float distance       = 5.0f;
    // Angle relative to leader's facing, in radians.
    // 0   = directly behind, pi/2 = right flank, pi = in front,
    // 3pi/2 = left flank.
    float angle_radians  = 0.0f;
};

// Compute the offset for `slot` in `type`. `default_distance` is the
// owner-pinned follow distance (from /follow_distance); some formations
// (Tight, Free) ignore the distance, others use it as the radius.
FormationOffset ComputeFormationOffset(
    FormationType type,
    uint8_t slot,
    float default_distance);

// Parse a token (case-insensitive) → FormationType. Returns Free for
// unrecognised input so /formation typo doesn't accidentally rearrange
// the squad.
FormationType ParseFormationType(char const* token);

// Owner spacing preset ("spacing tight|close|spread"): the yards between neighbouring bots in a
// formation and the scale applied to the raid ranged / pre-pull arc spacing (yards / 4, so Close = 1).
enum class FormationSpacing : uint8_t { Tight = 0, Close = 1, Spread = 2 };
constexpr float FormationSpacingYards(FormationSpacing s)
{
    return s == FormationSpacing::Tight ? 2.5f : s == FormationSpacing::Spread ? 8.0f : 4.0f;
}
// "tight" / "close" / "spread" (also "spaced"); returns false for anything else.
bool ParseFormationSpacing(char const* token, FormationSpacing& out);
char const* FormationSpacingName(FormationSpacing s);

// Duty row of a member for slot ordering: tanks in front, healers at the back.
enum class FormationRank : uint8_t { Tank = 0, Melee = 1, Ranged = 2, Healer = 3 };

// Offset of slot `index` of `count` (members already sorted by rank, then guid) for the owner formation
// types Circle / Wedge (arrow) / Line / Column, `spacing` yards between neighbours. rank_of_index(i) is
// the rank of member i; Line puts Tank+Melee in the front row and Ranged+Healer one row back, Wedge
// fills the V from the point back in the same order. Angle follows the core's follow generator: 0 =
// in front of the leader, pi = directly behind, pi/2 = the leader's left. Free / Tight / Spread fall
// through to ComputeFormationOffset.
FormationOffset ComputeOwnerSlotOffset(FormationType type, uint32_t index, uint32_t count, uint32_t front_row_count,
                                       float spacing, uint64_t guid = 0);

// ---- Playerbot.Behavior.SpreadOut: pure variation helpers (no snapshot deps) ----
// Stable per-(guid, salt) value in [-1, 1): the same bot always gets the same number, different bots differ.
inline float StableUnit(uint64_t guid, uint32_t salt)
{
    uint64_t h = guid * 0x9E3779B97F4A7C15ull + uint64_t(salt) * 0xBF58476D1CE4E5B9ull + 0x94D049BB133111EBull;
    h ^= h >> 31; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 29; h *= 0x94D049BB133111EBull; h ^= h >> 32;
    return float(h & 0xFFFFFFu) / float(0x800000u) - 1.0f;
}
inline float StableJitter(uint64_t guid, uint32_t salt, float radius) { return StableUnit(guid, salt) * radius; }

// Melee slot: bot `rank` of `count` same-victim melee (sorted by guid) takes an evenly spaced bearing over
// `arc_rad` (160 deg default: +-80 off the back, never the frontal half-plane) centred on the victim's back (victim_o + pi), plus a jitter of +-30% of one step,
// at `outer - (0.5 .. 2.0)` y from the victim's centre, never under `inner`. Out: planar spot.
inline void MeleeArcSpot(uint64_t guid, uint32_t rank, uint32_t count, float vx, float vy, float victim_o,
                         float inner, float outer, float& ox, float& oy, float arc_rad = 2.7925f)
{
    if (count == 0) count = 1;
    const float step = arc_rad / float(count);
    const float off = -0.5f * arc_rad + step * (float(rank) + 0.5f) + StableJitter(guid, 11, 0.3f * step);
    const float a = victim_o + 3.14159265f + off;
    float r = outer - (0.5f + 0.75f * (StableUnit(guid, 12) + 1.0f));   // 0.5 .. 2.0 inside `outer`
    if (r < inner) r = inner;
    ox = vx + std::cos(a) * r;
    oy = vy + std::sin(a) * r;
}

// Diagnostic name. Stable strings — used in /status and /history.
char const* FormationTypeName(FormationType t);

} // namespace Playerbot
