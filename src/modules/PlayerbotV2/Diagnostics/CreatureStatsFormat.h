// The [creature_stats] line `.playerbot creaturestats` logs per creature: one line, key=value pairs, read by
// tools/classic_spell_values/compare_creatures.py. Pure formatting, no server types.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "fmt/format.h"

namespace Playerbot::Diagnostics::CreatureStats
{

struct Row
{
    std::uint32_t entry = 0;
    std::string name;
    std::uint32_t level = 0;
    std::uint32_t rank = 0;
    std::uint64_t health = 0;
    std::int32_t mana = 0;
    float armor = 0.f;
    float dmg_min = 0.f, dmg_max = 0.f;   // per hit, before armor and crushing blows
    std::uint32_t atk_ms = 0;
    std::array<std::int32_t, 6> res = {};   // holy, fire, nature, frost, shadow, arcane
};

// A name goes between double quotes: a quote or a line break inside it would break the one-line, key=value format.
inline std::string CleanName(std::string const& name)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name)
        out += (c == '"' || c == '\n' || c == '\r') ? '\'' : c;
    return out;
}

inline std::string FormatLine(Row const& r)
{
    return fmt::format("[creature_stats] entry={} name=\"{}\" level={} rank={} health={} mana={} armor={:.0f} dmg_min={:.0f} dmg_max={:.0f} atk_ms={} res={}/{}/{}/{}/{}/{}",
                       r.entry, CleanName(r.name), r.level, r.rank, r.health, r.mana, r.armor, r.dmg_min, r.dmg_max, r.atk_ms,
                       r.res[0], r.res[1], r.res[2], r.res[3], r.res[4], r.res[5]);
}

} // namespace Playerbot::Diagnostics::CreatureStats
