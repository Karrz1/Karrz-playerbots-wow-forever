// TrackedBuffMatch.h - which tracked raid buff an applied aura is.
//
// The group snapshot records a long-duration raid buff (Mark of the Wild, a blessing, a Fortitude) under the id of its rank 1. An
// aura is that buff when its spell is the same spell in any rank. The rank chains and families of the loaded spell data do not
// cover every rank of every buff: a bot casts the highest rank it knows, and when that rank is in neither the chain nor the
// family of the rank-1 id the aura is never recorded (MC Garr 2026-10-10: Mark of the Wild 1126 and Blessing of Might 19740 never
// appeared in a target's tracked list, so every druid and paladin recast them every 30 s through the whole staging and the
// healers pulled at 43% mana). Ranks of one spell share a name, so the name is the second test after the id.
//
// Pure: the id test and the name lookup are passed in, so the rule is unit-tested without spell data.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace Playerbot {

struct TrackedBuffEntry
{
    std::uint32_t chain_first;   // rank 1 of the chain, the spell the table is written against
    std::uint32_t recorded_as;   // the id the buff is recorded under
};

enum class TrackedHow : std::uint8_t { None, ById, ByName };

struct TrackedMatch
{
    std::uint32_t recorded_as = 0;
    TrackedHow    how = TrackedHow::None;
};

// `same_spell(a, b)`: a and b are ranks of one spell by id (chain / family). `name_of(id)`: the spell's name, empty when unknown.
// The id test wins; the name test is the fallback, and an empty name never matches.
template <typename SameFn, typename NameFn>
TrackedMatch MatchTrackedBuff(std::uint32_t spell_id, std::span<TrackedBuffEntry const> table, SameFn&& same_spell, NameFn&& name_of)
{
    for (TrackedBuffEntry const& e : table)
        if (spell_id == e.chain_first || same_spell(spell_id, e.chain_first))
            return { e.recorded_as, TrackedHow::ById };
    std::string_view const name = name_of(spell_id);
    if (name.empty())
        return {};
    for (TrackedBuffEntry const& e : table)
        if (name == std::string_view(name_of(e.chain_first)))
            return { e.recorded_as, TrackedHow::ByName };
    return {};
}

} // namespace Playerbot
