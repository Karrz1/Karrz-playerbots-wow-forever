#include "Formation.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Playerbot {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

FormationOffset ComputeFormationOffset(
    FormationType type, uint8_t slot, float default_distance)
{
    FormationOffset out;
    out.distance = default_distance;
    switch (type)
    {
        case FormationType::Free:
            // No offset — caller emits a plain follow.
            out.angle_radians = 0.0f;
            return out;

        case FormationType::Tight:
            // Stack on leader. Tiny distance so collision boxes don't
            // visibly clip but bots are essentially "on" the leader.
            out.distance      = 1.5f;
            out.angle_radians = 0.0f;
            return out;

        case FormationType::Spread:
        {
            // Ring 8y around leader; slots evenly spaced. With 8 slots
            // each gets 45°. Modulo 16 so >16 bots wrap (rare).
            // 16 places per ring; a raid's extra bots take wider rings, offset half a step so no one lines up behind another.
            const uint8_t ring = slot / 16;
            out.distance = 8.0f + 4.0f * float(ring);
            out.angle_radians = kPi * 2.0f * (float(slot % 16) + 0.5f * float(ring % 2)) / 16.0f;
            return out;
        }

        case FormationType::Line:
        {
            // Behind leader, lateral fan. Slot 0 = directly behind;
            // odd slots go right, even slots go left of slot 0.
            // Result: behind-centre with lateral spread of ~4y/slot.
            const int s = static_cast<int>(slot);
            const int side = (s % 2 == 1) ? 1 : -1;
            const int rank = (s + 1) / 2;
            // Offset vector in leader-local frame: (-distance, ±rank*4)
            const float dx = -default_distance;
            const float dy = static_cast<float>(side) * static_cast<float>(rank) * 4.0f;
            out.distance      = std::sqrt(dx * dx + dy * dy);
            out.angle_radians = std::atan2(dy, dx);
            // Normalise to [0, 2pi) so callers can rely on positive.
            if (out.angle_radians < 0.0f) out.angle_radians += 2.0f * kPi;
            return out;
        }

        case FormationType::Column:
        {
            // Behind leader, single file. Slot 0 immediately behind,
            // slot N is N*4y further back.
            out.distance      = default_distance + static_cast<float>(slot) * 4.0f;
            out.angle_radians = 0.0f;     // directly behind
            return out;
        }

        case FormationType::Wedge:
        {
            // V-shape behind leader, slot 0 = primary point of the V
            // (closest), then alternating flanks fanning out + back.
            const int s = static_cast<int>(slot);
            if (s == 0)
            {
                out.distance      = default_distance;
                out.angle_radians = 0.0f;
                return out;
            }
            const int side = (s % 2 == 1) ? 1 : -1;
            const int rank = (s + 1) / 2;
            const float dx = -default_distance - static_cast<float>(rank) * 2.0f;
            const float dy = static_cast<float>(side) * static_cast<float>(rank) * 3.0f;
            out.distance      = std::sqrt(dx * dx + dy * dy);
            out.angle_radians = std::atan2(dy, dx);
            if (out.angle_radians < 0.0f) out.angle_radians += 2.0f * kPi;
            return out;
        }

        case FormationType::Circle:
        {
            // Ring 12y around leader, evenly distributed across 12 slots.
            out.distance      = 12.0f;
            out.angle_radians = kPi * 2.0f * float(slot % 12) / 12.0f;
            return out;
        }
    }
    return out;
}

bool ParseFormationSpacing(char const* token, FormationSpacing& out)
{
    if (!token) return false;
    auto eq = [token](char const* s) { return std::strcmp(token, s) == 0; };
    if (eq("tight"))  { out = FormationSpacing::Tight;  return true; }
    if (eq("close"))  { out = FormationSpacing::Close;  return true; }
    if (eq("spread") || eq("spaced") || eq("spread out") || eq("spaced out")) { out = FormationSpacing::Spread; return true; }
    return false;
}

char const* FormationSpacingName(FormationSpacing s)
{
    return s == FormationSpacing::Tight ? "tight" : s == FormationSpacing::Spread ? "spread" : "close";
}

namespace {
// Forward (+) / left (+) components in the leader's frame -> core follow offset.
FormationOffset FromLocal(float forward, float left)
{
    FormationOffset o;
    o.distance = std::sqrt(forward * forward + left * left);
    o.angle_radians = std::atan2(left, forward);
    if (o.angle_radians < 0.0f) o.angle_radians += 2.0f * kPi;
    return o;
}
}

FormationOffset ComputeOwnerSlotOffset(FormationType type, uint32_t index, uint32_t count, uint32_t front_row_count,
                                       float spacing, uint64_t guid)
{
    if (count == 0) count = 1;
    if (index >= count) index = count - 1;
    // Per-bot variation, stable for the bot's lifetime (guid 0 = none): a formation is a loose shape, not a parade ground.
    auto jit = [&](uint32_t salt, float amount) { return guid ? StableJitter(guid, salt, amount) : 0.0f; };
    switch (type)
    {
        case FormationType::Circle:
        {
            // Concentric rings round the owner, inner rings first (tanks and melee, the order the caller sorted by), each ring
            // holding as many bots as fit `spacing` apart on its circumference. 40 bots make four compact rings instead of one
            // 25 y circle. The outer ring is spread evenly over the bots it actually has; odd rings are turned half a step so
            // nobody stands directly behind another. Slot 0 is directly behind the owner.
            uint32_t ring = 0, first = 0, in_ring = 0;
            float radius = 0.0f;
            for (;; ++ring)
            {
                radius = spacing * (1.2f + 0.95f * float(ring));
                uint32_t const cap = std::max<uint32_t>(3u, uint32_t(2.0f * kPi * radius / spacing));
                in_ring = std::min<uint32_t>(cap, count - first);
                if (index < first + in_ring)
                    break;
                first += in_ring;
            }
            uint32_t const pos = index - first;
            float const step = 2.0f * kPi / float(in_ring);
            float const a = kPi + step * (float(pos) + (ring % 2 ? 0.5f : 0.0f)) + jit(21, 0.25f * step);
            float const r = radius + jit(22, 0.2f * spacing);
            return FromLocal(r * std::cos(a), r * std::sin(a));
        }
        case FormationType::Wedge:
        {
            // Arrow pointing the owner's facing: the owner is the tip, two arms trail back from him, four steps per arm and
            // layer. Further layers sit behind the first and are shifted half a step sideways, so 40 bots make a deep chevron
            // instead of one 20-row column.
            constexpr uint32_t kPerLayer = 8;
            uint32_t const layer = index / kPerLayer, i = index % kPerLayer;
            float const j = float(i / 2 + 1);
            float const side = (i % 2 == 0) ? 1.0f : -1.0f;
            float const fwd = -(spacing * 0.9f * j + spacing * 1.7f * float(layer));
            float const lat = side * (spacing * 0.85f * j + spacing * 0.45f * float(layer % 2));
            return FromLocal(fwd + jit(23, 0.15f * spacing), lat + jit(24, 0.15f * spacing));
        }
        case FormationType::Line:
        {
            // Abreast in staggered rows behind the owner: front group (tanks, melee) in the first rows, ranged and healers in
            // the rows behind. A row holds at most 8 bots, so 40 bots are five short rows, not a 160 y wall.
            uint32_t const width = std::clamp<uint32_t>(uint32_t(std::ceil(std::sqrt(float(count) * 1.5f))), 4u, 8u);
            bool const front = index < front_row_count;
            uint32_t const group_n = front ? front_row_count : count - front_row_count;
            uint32_t const pos = front ? index : index - front_row_count;
            uint32_t const front_rows = (front_row_count + width - 1) / width;
            uint32_t const row = (front ? 0u : front_rows) + pos / width;
            uint32_t const row_start = (pos / width) * width;
            uint32_t const row_n = std::min<uint32_t>(width, group_n - row_start);
            float const lat = (float(pos - row_start) - float(row_n - 1) * 0.5f) * spacing + ((row % 2) ? 0.25f * spacing : 0.0f);
            float const fwd = -spacing * (1.0f + 1.1f * float(row)) - (front ? 0.0f : spacing * 0.5f);
            return FromLocal(fwd + jit(25, 0.2f * spacing), lat + jit(26, 0.15f * spacing));
        }
        case FormationType::Column:
        {
            // Single file up to 8 bots, two abreast up to 24, three abreast beyond that.
            uint32_t const lanes = count <= 8 ? 1u : count <= 24 ? 2u : 3u;
            uint32_t const lane = index % lanes, row = index / lanes;
            float const lat = (float(lane) - float(lanes - 1) * 0.5f) * spacing * 0.9f;
            return FromLocal(-spacing * float(row + 1) + jit(27, 0.15f * spacing), lat + jit(28, 0.1f * spacing));
        }
        default:
            return ComputeFormationOffset(type, uint8_t(index > 255 ? 255 : index), spacing);
    }
}

FormationType ParseFormationType(char const* token)
{
    if (!token) return FormationType::Free;
    auto eq = [token](char const* s) { return std::strcmp(token, s) == 0; };
    if (eq("free"))    return FormationType::Free;
    if (eq("tight"))   return FormationType::Tight;
    if (eq("stack"))   return FormationType::Tight;     // alias
    if (eq("spread"))  return FormationType::Spread;
    if (eq("line"))    return FormationType::Line;
    if (eq("column"))  return FormationType::Column;
    if (eq("col"))     return FormationType::Column;    // alias
    if (eq("wedge"))   return FormationType::Wedge;
    if (eq("v"))       return FormationType::Wedge;     // alias
    if (eq("arrow"))   return FormationType::Wedge;     // alias: the arrow points the owner's facing
    if (eq("none"))    return FormationType::Free;      // alias: plain follow
    if (eq("circle"))  return FormationType::Circle;
    if (eq("ring"))    return FormationType::Circle;    // alias
    return FormationType::Free;
}

char const* FormationTypeName(FormationType t)
{
    switch (t)
    {
        case FormationType::Free:   return "free";
        case FormationType::Tight:  return "tight";
        case FormationType::Spread: return "spread";
        case FormationType::Line:   return "line";
        case FormationType::Column: return "column";
        case FormationType::Wedge:  return "wedge";
        case FormationType::Circle: return "circle";
    }
    return "?";
}

} // namespace Playerbot
