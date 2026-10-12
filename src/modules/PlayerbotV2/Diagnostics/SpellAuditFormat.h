// The line `.playerbot spellaudit` writes per spell to SpellAudit.tsv (in the server's logs folder): tab separated, one spell per line, the
// final numbers this server uses for the spell (client data with the classic_spell_value overrides applied), read by
// tools/classic_spell_values/compare_spells.py against the VMaNGOS spell_template row. Pure formatting, no server types.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "fmt/format.h"

namespace Playerbot::Diagnostics::SpellAudit
{

struct EffectRow
{
    std::uint32_t effect = 0;
    std::uint32_t aura = 0;
    float base_points = 0.f;        // BasePoints as the client holds it
    float classic_mean = 0.f;       // the mean value of the classic_spell_value row when there is one (what the server uses instead of base_points)
    float points_per_level = 0.f;
    std::int32_t misc = 0;
    std::uint32_t amplitude_ms = 0;
    std::uint32_t trigger_spell = 0;
    std::int32_t chain_targets = 0;
    float radius_max = 0.f;         // TargetA radius
    float radius_b_max = 0.f;       // TargetB radius
    bool classic_value = false;     // the classic_spell_value override was applied to this effect
};

struct Row
{
    std::uint32_t id = 0;
    std::string name;
    std::uint32_t school_mask = 0;
    std::int32_t cast_ms = 0;
    std::uint32_t recovery_ms = 0;
    std::uint32_t category_recovery_ms = 0;
    std::int32_t duration_ms = 0;
    float range_max = 0.f;
    std::uint32_t proc_chance = 0;
    std::uint32_t proc_charges = 0;
    std::uint32_t stack = 0;
    std::uint32_t max_targets = 0;
    std::uint32_t base_level = 0;
    std::uint32_t max_level = 0;
    std::uint32_t spell_level = 0;
    std::int32_t mana_cost = 0;           // the client's flat mana cost (SpellPower row of the mana power), before classic_spell_cost
    float mana_cost_pct = 0.f;            // and its percent of base mana
    std::uint32_t power_row_id = 0;       // that SpellPower DB2 row (a hotfix replaces it by id)
    std::uint32_t family_name = 0;
    std::array<std::uint32_t, 4> family_flags{};   // SpellFamilyFlags, the mask a talent's class mask is matched against
    std::array<EffectRow, 3> effects;
};

inline std::string Header()
{
    return "id\tname\tschool\tcast_ms\trecovery_ms\tcategory_recovery_ms\tduration_ms\trange_max\tproc_chance\tproc_charges\tstack\tmax_targets\tbase_level\tmax_level\tspell_level"
           "\te1_effect\te1_aura\te1_base\te1_classic_mean\te1_ppl\te1_misc\te1_amp\te1_trigger\te1_chain\te1_radius\te1_classic"
           "\te2_effect\te2_aura\te2_base\te2_classic_mean\te2_ppl\te2_misc\te2_amp\te2_trigger\te2_chain\te2_radius\te2_classic"
           "\te3_effect\te3_aura\te3_base\te3_classic_mean\te3_ppl\te3_misc\te3_amp\te3_trigger\te3_chain\te3_radius\te3_classic"
           "\tfamily_name\tfamily_0\tfamily_1\tfamily_2\tfamily_3\te1_radius_b\te2_radius_b\te3_radius_b\tmana_cost\tmana_cost_pct\tpower_row_id";
}

inline std::string FormatLine(Row const& r)
{
    std::string name = r.name;
    for (char& c : name)
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
    std::string out = fmt::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{:.1f}\t{}\t{}\t{}\t{}\t{}\t{}\t{}", r.id, name, r.school_mask, r.cast_ms, r.recovery_ms,
        r.category_recovery_ms, r.duration_ms, r.range_max, r.proc_chance, r.proc_charges, r.stack, r.max_targets, r.base_level, r.max_level, r.spell_level);
    for (EffectRow const& e : r.effects)
        out += fmt::format("\t{}\t{}\t{:.2f}\t{:.2f}\t{:.3f}\t{}\t{}\t{}\t{}\t{:.1f}\t{}", e.effect, e.aura, e.base_points, e.classic_mean, e.points_per_level, e.misc, e.amplitude_ms,
            e.trigger_spell, e.chain_targets, e.radius_max, e.classic_value ? 1 : 0);
    out += fmt::format("\t{}\t{}\t{}\t{}\t{}", r.family_name, r.family_flags[0], r.family_flags[1], r.family_flags[2], r.family_flags[3]);
    for (EffectRow const& e : r.effects)
        out += fmt::format("\t{:.1f}", e.radius_b_max);
    out += fmt::format("\t{}\t{:.1f}\t{}", r.mana_cost, r.mana_cost_pct, r.power_row_id);
    return out;
}

} // namespace Playerbot::Diagnostics::SpellAudit
