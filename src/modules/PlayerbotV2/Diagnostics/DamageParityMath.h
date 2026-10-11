// DamageParityMath - the arithmetic of the damage_parity scenarios (`.playerbot scenario run damage_parity_<class>`), kept free of game types so
// tests/game/ClassicDamageParity.cpp can pin it.
//
// What the scenarios measure: one level 60 character per class hits a level 63 target dummy for a fixed time; every spell's outcomes (hit,
// crit, miss, dodge, ...) and damage are tallied (SpellAgg). The checks on top:
//   * stat sensitivity   the same leg again with +100 spell power / +100 attack power: the change in a spell's average hit per point is its
//                        measured coefficient (CoefficientPerPoint), compared with the vanilla one (WithinPct).
//   * finisher sweep     a combo point / rage / energy scaled spell at 1..5 points (or increasing rage): the damage must grow with the points
//                        (ScalingVerdict). A flat or falling series is the "Eviscerate bug": the resource was spent before the damage was computed.
//   * ratios             a talent on against off, or a crit against a normal hit: the measured ratio against the expected one (RatioVerdict).
//   * reference DPS      the leg's total against the vanilla raid ranges per class (RangeVerdict).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace Playerbot::Diagnostics::Parity {

// One thing that can happen to one cast or swing.
enum class Outcome : std::uint8_t { Hit, Crit, Miss, Dodge, Parry, Block, Glancing, Crushing, Resist, Other };
constexpr std::size_t kOutcomeCount = 10;

constexpr char const* ToString(Outcome o)
{
    switch (o)
    {
        case Outcome::Hit:      return "hit";
        case Outcome::Crit:     return "crit";
        case Outcome::Miss:     return "miss";
        case Outcome::Dodge:    return "dodge";
        case Outcome::Parry:    return "parry";
        case Outcome::Block:    return "block";
        case Outcome::Glancing: return "glance";
        case Outcome::Crushing: return "crush";
        case Outcome::Resist:   return "resist";
        default:                return "other";
    }
}

// An outcome that deals damage (the others are attempts that did nothing).
constexpr bool DealsDamage(Outcome o)
{
    return o == Outcome::Hit || o == Outcome::Crit || o == Outcome::Glancing || o == Outcome::Crushing || o == Outcome::Block;
}

// Everything one spell (or the white swing, spell id 0) did in one leg.
struct SpellAgg
{
    std::uint32_t spell_id = 0;
    std::uint32_t count[kOutcomeCount] = {};       // attempts by outcome
    std::uint64_t damage[kOutcomeCount] = {};      // damage dealt by outcome
    std::uint32_t min_hit = 0, max_hit = 0;        // over the hits that dealt damage
    std::uint32_t ticks = 0;                       // periodic damage ticks
    std::uint64_t tick_damage = 0;

    void AddOutcome(Outcome o, std::uint32_t dmg)
    {
        std::size_t const i = std::size_t(o);
        ++count[i];
        damage[i] += dmg;
        if (DealsDamage(o) && dmg)
        {
            min_hit = min_hit ? std::min(min_hit, dmg) : dmg;
            max_hit = std::max(max_hit, dmg);
        }
    }
    void AddTick(std::uint32_t dmg) { ++ticks; tick_damage += dmg; }

    std::uint32_t Attempts() const { std::uint32_t n = 0; for (std::uint32_t c : count) n += c; return n; }
    std::uint32_t Landed() const { std::uint32_t n = 0; for (std::size_t i = 0; i < kOutcomeCount; ++i) if (DealsDamage(Outcome(i))) n += count[i]; return n; }
    std::uint64_t DirectDamage() const { std::uint64_t d = 0; for (std::uint64_t v : damage) d += v; return d; }
    std::uint64_t TotalDamage() const { return DirectDamage() + tick_damage; }
    std::uint32_t N(Outcome o) const { return count[std::size_t(o)]; }

    // Mean of the non-critical hits (hit + glancing + crushing + block), 0 when there were none. Glancing hits are included: they are a normal part of
    // a white swing, but a coefficient check should look at `AvgOf(Outcome::Hit)`.
    double AvgOf(Outcome o) const { return N(o) ? double(damage[std::size_t(o)]) / N(o) : 0.0; }
    double AvgNormal() const { return AvgOf(Outcome::Hit); }
    double AvgCrit() const { return AvgOf(Outcome::Crit); }
    // Crit chance seen by the swings that could crit (landed, or resisted in full): crits / (attempts that were not avoided).
    double CritPct() const
    {
        std::uint32_t const denom = Landed();
        return denom ? 100.0 * N(Outcome::Crit) / denom : 0.0;
    }
    // Share of attempts that did nothing (missed, dodged, parried, resisted).
    double AvoidPct() const
    {
        std::uint32_t const a = Attempts();
        std::uint32_t const avoided = N(Outcome::Miss) + N(Outcome::Dodge) + N(Outcome::Parry) + N(Outcome::Resist);
        return a ? 100.0 * avoided / a : 0.0;
    }
    // Measured crit multiplier: crit hit over normal hit. 0 when either is missing.
    double CritMultiplier() const { return AvgNormal() > 0.0 && N(Outcome::Crit) ? AvgCrit() / AvgNormal() : 0.0; }
};

// ---- Comparisons ----

// (measured - expected) / expected in percent; 0 when expected is 0 and measured too, a large number when only expected is 0.
inline double GapPct(double measured, double expected)
{
    if (expected == 0.0)
        return measured == 0.0 ? 0.0 : 1000.0;
    return 100.0 * (measured - expected) / expected;
}

inline bool WithinPct(double measured, double expected, double tolerance_pct)
{
    return std::fabs(GapPct(measured, expected)) <= tolerance_pct;
}

// Damage per point of a stat: two legs with `points` more of it (spell power, attack power) in the second. 0 when the points are 0.
inline double CoefficientPerPoint(double avg_without, double avg_with, double points)
{
    return points != 0.0 ? (avg_with - avg_without) / points : 0.0;
}

// The measured coefficient of a spell against its vanilla one. `tolerance_pct` is relative to the vanilla value; the hit averages of a few dozen
// casts carry a few percent of noise, so the sensitivity legs use legs long enough to put 100 points well above it (see kSensitivityPoints).
enum class Verdict : std::uint8_t { Ok, TooLow, TooHigh, NoData };
constexpr char const* ToString(Verdict v)
{
    switch (v)
    {
        case Verdict::Ok:      return "ok";
        case Verdict::TooLow:  return "too_low";
        case Verdict::TooHigh: return "too_high";
        default:               return "no_data";
    }
}

inline Verdict CompareValue(double measured, double expected, double tolerance_pct)
{
    if (expected <= 0.0 || measured < 0.0)
        return Verdict::NoData;
    double const gap = GapPct(measured, expected);
    if (gap < -tolerance_pct) return Verdict::TooLow;
    if (gap > tolerance_pct)  return Verdict::TooHigh;
    return Verdict::Ok;
}

// A ratio the game rules fix: crit against a normal hit (2.0, or 2.0 for spells with Ruin: its 1.5 base + the talent's +100% of the bonus),
// Shadow Mastery on against off (1.10), and so on.
inline Verdict RatioVerdict(double measured_ratio, double expected_ratio, double tolerance_pct)
{
    return CompareValue(measured_ratio, expected_ratio, tolerance_pct);
}

// Finisher sweep: the average damage at 1, 2, 3, ... points (index 0 = 1 point). Pass when the series rises with every point; a series that
// stays within `flat_pct` of its first value does not scale at all (the resource is consumed before the damage is computed).
enum class Scaling : std::uint8_t { Scales, Flat, NotMonotonic, TooFewPoints };
constexpr char const* ToString(Scaling s)
{
    switch (s)
    {
        case Scaling::Scales:       return "scales";
        case Scaling::Flat:         return "flat";
        case Scaling::NotMonotonic: return "not_monotonic";
        default:                    return "too_few_points";
    }
}

inline Scaling ScalingVerdict(std::vector<double> const& avg_by_points, double flat_pct = 3.0, double step_noise_pct = 2.0)
{
    if (avg_by_points.size() < 2)
        return Scaling::TooFewPoints;
    for (double v : avg_by_points)
        if (v <= 0.0)
            return Scaling::TooFewPoints;
    double const first = avg_by_points.front();
    double const last = avg_by_points.back();
    if (std::fabs(GapPct(last, first)) <= flat_pct)
        return Scaling::Flat;
    // Every step must not fall by more than the sampling noise; a series that overall rises but dips between two points is a bug of its own.
    for (std::size_t i = 1; i < avg_by_points.size(); ++i)
        if (avg_by_points[i] < avg_by_points[i - 1] * (1.0 - step_noise_pct / 100.0))
            return Scaling::NotMonotonic;
    return last > first ? Scaling::Scales : Scaling::NotMonotonic;
}

// Per-point increment of a series that should be linear (Eviscerate, Ferocious Bite: base + k * points): the mean step over the mean of the
// series, so 1.0 means "each point adds as much as the average hit". 0 for a short or non-positive series.
inline double LinearStepShare(std::vector<double> const& avg_by_points)
{
    if (avg_by_points.size() < 2)
        return 0.0;
    double mean = 0.0;
    for (double v : avg_by_points) mean += v;
    mean /= double(avg_by_points.size());
    double const step = (avg_by_points.back() - avg_by_points.front()) / double(avg_by_points.size() - 1);
    return mean > 0.0 ? step / mean : 0.0;
}

// ---- Vanilla formulas ----

// Share of physical damage armor takes off: armor / (armor + 400 + 85 * attacker level), at most 75 %. Retail replaced the constant with a
// per-level table and a higher cap, which is what the armor legs (0 / 3731 / sundered armor) are there to catch.
inline double VanillaArmorReduction(double armor, int attacker_level)
{
    if (armor <= 0.0)
        return 0.0;
    double const r = armor / (armor + 400.0 + 85.0 * attacker_level);
    return std::min(r, 0.75);
}

// Whether a measured crit rate fits the one the character sheet promises: within `slack_pts` points plus three standard deviations of a
// binomial sample of n landed attacks. False when n is too small to say anything (fewer than 20 hits counts as consistent, nothing to judge).
inline bool CritPctConsistent(double measured_pct, double expected_pct, std::uint32_t n, double slack_pts = 2.0)
{
    if (n < 20)
        return true;
    double const p = std::clamp(expected_pct / 100.0, 0.0, 1.0);
    double const sigma_pts = 100.0 * std::sqrt(p * (1.0 - p) / double(n));
    return std::fabs(measured_pct - expected_pct) <= slack_pts + 3.0 * sigma_pts;
}

// Vanilla spell power coefficients of the direct damage of the main damage spells (the spell's cast time / 3.5 for its school, with the level
// and double-effect adjustments, as the community tables list them). Matched by spell name: a rank's id differs between data sets. A spell
// missing here has no reference and is only measured.
struct SpCoefficient { char const* name; double coeff; };
inline constexpr SpCoefficient kSpCoefficients[] = {
    { "Shadow Bolt", 0.857 }, { "Fireball", 1.0 }, { "Frostbolt", 0.814 }, { "Scorch", 0.429 }, { "Fire Blast", 0.429 },
    { "Pyroblast", 1.15 }, { "Mind Blast", 0.429 }, { "Smite", 0.714 }, { "Holy Fire", 0.571 }, { "Lightning Bolt", 0.794 },
    { "Chain Lightning", 0.651 }, { "Earth Shock", 0.386 }, { "Frost Shock", 0.386 }, { "Wrath", 0.571 }, { "Starfire", 1.0 },
    { "Arcane Missiles", 0.714 }, { "Arcane Explosion", 0.214 }, { "Searing Pain", 0.429 }, { "Soul Fire", 1.15 },
};

inline double VanillaSpCoefficient(std::string const& spell_name)
{
    for (SpCoefficient const& c : kSpCoefficients)
        if (spell_name == c.name)
            return c.coeff;
    return 0.0;
}

// ---- Reference damage ----

// Level 60 raid DPS per class and spec, from the research note (class-damage-vanilla-vs-server.md section 2): Molten Core gear (T1/T2) and Naxxramas
// gear with raid buffs (T3). Approximate, from vanilla raid logs; a class outside its range is a lead, not a verdict on its own.
struct DpsRange
{
    char const* key;
    int mc_lo, mc_hi, naxx_lo, naxx_hi;
};

inline constexpr DpsRange kDpsRanges[] = {
    { "warrior",   350, 500, 800, 1100 },
    { "rogue",     400, 550, 800, 1000 },
    { "hunter",    350, 500, 700, 950 },
    { "cat",       300, 450, 600, 800 },
    { "enhance",   300, 450, 600, 750 },
    { "ret",       250, 350, 450, 600 },
    { "mage",      350, 550, 700, 950 },
    { "warlock",   350, 550, 700, 950 },
    { "shadow",    250, 400, 450, 600 },
    { "elemental", 300, 450, 500, 650 },
    { "moonkin",   250, 400, 450, 600 },
};

inline DpsRange const* FindRange(std::string const& key)
{
    for (DpsRange const& r : kDpsRanges)
        if (key == r.key)
            return &r;
    return nullptr;
}

// The range keys a class can be (the damage spec the bot ended up with picks one; a class with two DPS specs lists both and the verdict
// takes the widest range). Class ids are the 1.12 ones.
inline std::vector<std::string> RangeKeysForClass(std::uint8_t cls)
{
    switch (cls)
    {
        case 1:  return { "warrior" };
        case 2:  return { "ret" };
        case 3:  return { "hunter" };
        case 4:  return { "rogue" };
        case 5:  return { "shadow" };
        case 7:  return { "enhance", "elemental" };
        case 8:  return { "mage" };
        case 9:  return { "warlock" };
        case 11: return { "cat", "moonkin" };
        default: return {};
    }
}

// The tier of the gear a leg wore: 0 none, 1 = Molten Core (T1), 3 = Naxxramas (T3).
enum class RangeVerdict : std::uint8_t { In, Below, Above, NoReference };
constexpr char const* ToString(RangeVerdict v)
{
    switch (v)
    {
        case RangeVerdict::In:    return "in_range";
        case RangeVerdict::Below: return "below_range";
        case RangeVerdict::Above: return "above_range";
        default:                  return "no_reference";
    }
}

// `raid_buffs`: a leg with no raid buffs is allowed to sit `kUnbuffedAllowancePct` under the low end (the reference numbers are logged with
// buffs); it is never allowed above the high end.
constexpr double kUnbuffedAllowancePct = 15.0;

inline RangeVerdict DpsVerdict(std::uint8_t cls, int gear_tier, bool raid_buffs, double dps, std::string* range_text = nullptr)
{
    if (gear_tier != 1 && gear_tier != 3)
        return RangeVerdict::NoReference;
    int lo = 0, hi = 0;
    for (std::string const& key : RangeKeysForClass(cls))
    {
        DpsRange const* r = FindRange(key);
        if (!r)
            continue;
        int const l = gear_tier == 1 ? r->mc_lo : r->naxx_lo;
        int const h = gear_tier == 1 ? r->mc_hi : r->naxx_hi;
        lo = lo ? std::min(lo, l) : l;
        hi = std::max(hi, h);
    }
    if (!lo || !hi)
        return RangeVerdict::NoReference;
    if (range_text)
        *range_text = std::to_string(lo) + "-" + std::to_string(hi);
    double const floor_dps = raid_buffs ? double(lo) : double(lo) * (1.0 - kUnbuffedAllowancePct / 100.0);
    if (dps < floor_dps) return RangeVerdict::Below;
    if (dps > double(hi)) return RangeVerdict::Above;
    return RangeVerdict::In;
}

// ---- Output ----

inline std::string FormatSpellLine(char const* label, std::string const& cls, std::string const& spell_name, SpellAgg const& a, double seconds)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "[dmg_parity] spell leg=%s class=%s spell=%u name=\"%s\" attempts=%u hit=%u crit=%u miss=%u dodge=%u parry=%u resist=%u glance=%u "
                  "min=%u avg=%.0f crit_avg=%.0f max=%u ticks=%u tick_total=%llu total=%llu per_min=%.1f crit_pct=%.1f avoid_pct=%.1f",
                  label, cls.c_str(), a.spell_id, spell_name.c_str(), a.Attempts(), a.N(Outcome::Hit), a.N(Outcome::Crit), a.N(Outcome::Miss),
                  a.N(Outcome::Dodge), a.N(Outcome::Parry), a.N(Outcome::Resist), a.N(Outcome::Glancing), a.min_hit, a.AvgNormal(), a.AvgCrit(),
                  a.max_hit, a.ticks, static_cast<unsigned long long>(a.tick_damage), static_cast<unsigned long long>(a.TotalDamage()),
                  seconds > 0.0 ? 60.0 * a.Attempts() / seconds : 0.0, a.CritPct(), a.AvoidPct());
    return buf;
}

// The summary table is one markdown row per leg; the header is HeaderRow().
inline std::string HeaderRow()
{
    return "| class | leg | gear | buffs | dps | range | verdict | top gap spells |\n|---|---|---|---|---|---|---|---|";
}

inline std::string SummaryRow(std::string const& cls, char const* leg, char const* gear, bool buffs, double dps, std::string const& range,
                              RangeVerdict v, std::string const& top_gaps)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf), "| %s | %s | %s | %s | %.0f | %s | %s | %s |", cls.c_str(), leg, gear, buffs ? "on" : "off", dps,
                  range.empty() ? "-" : range.c_str(), ToString(v), top_gaps.empty() ? "-" : top_gaps.c_str());
    return buf;
}

} // namespace Playerbot::Diagnostics::Parity
