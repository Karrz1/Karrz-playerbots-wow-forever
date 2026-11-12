// OwnedGearPolicy - the picking rules for the gear of a player's own companions (owned bots), kept free of game types so
// tests/game/ClassicOwnedGearPolicy.cpp can pin them.
//
// Below the level cap an owned bot wears the best quest and dungeon gear for its level: green and blue pieces whose effective
// item level is at most `level + band`, ranked by the role stat weights (Playerbot::Gear::ScoreItemRaid) with a small bonus
// for blue. At the cap it gets the raid-prep picks of RaidGear, but never above the owner: the item level ceiling follows the
// owner's average equipped item level (a fresh 60 owner in blues gets pre-raid blues, an owner in tier 3 gets Molten Core
// level pieces), bounded by Playerbot.Gear.MaxTier / RaidIlvlCap. World, ambient and battleground bots are not affected: they
// keep Playerbot.World.Gear.*.

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Playerbot::V2::Gear::Owned {

constexpr int32_t kDefaultIlvlBand   = 12;   // a level-L bot may wear pieces up to effective item level L + this
constexpr int32_t kDefaultOwnerSlack = 6;    // a level-cap bot may wear pieces this far above the owner's average
constexpr int32_t kPreRaidFloor      = 63;   // ...but never be held below pre-raid (dungeon) gear by a poorly geared owner
constexpr int32_t kRareBonusPct      = 12;   // a blue outscores an equally good green by this much
constexpr int32_t kCommonPenaltyPct  = 30;   // a white is only worn when nothing green or blue fits
constexpr int32_t kUpgradePct        = 110;  // a worn piece is replaced only by one that scores this percent of it or more

constexpr int32_t kQualityCommon   = 1;
constexpr int32_t kQualityUncommon = 2;
constexpr int32_t kQualityRare     = 3;

// Whether a piece is a candidate at all: white to blue (an epic below the cap is a raid or world drop, not quest or dungeon
// gear) and no more than `band` item levels above the wearer's level.
constexpr bool PieceAllowed(int32_t quality, int32_t eff_ilvl, int32_t level, int32_t band)
{
    return quality >= kQualityCommon && quality <= kQualityRare && eff_ilvl <= level + band;
}

// The role score with the quality adjustment: blue +12 %, white -30 %, green as scored. A non-positive score (a wrong stat
// for the role) is never improved by a bonus.
constexpr int32_t QualityAdjusted(int32_t fit, int32_t quality)
{
    if (fit <= 0)
        return fit;
    if (quality >= kQualityRare)
        return int32_t(int64_t(fit) * (100 + kRareBonusPct) / 100);
    if (quality <= kQualityCommon)
        return int32_t(int64_t(fit) * (100 - kCommonPenaltyPct) / 100);
    return fit;
}

// Whether a pick replaces the worn piece: an empty slot or a worn piece the role scores at nothing always; otherwise the pick
// must score kUpgradePct percent of it or more, so a swap is never a downgrade or a coin toss between peers.
constexpr bool IsUpgrade(bool slot_empty, int32_t worn_adjusted, int32_t pick_adjusted)
{
    if (slot_empty || worn_adjusted <= 0)
        return pick_adjusted > worn_adjusted;
    return int64_t(pick_adjusted) * 100 >= int64_t(worn_adjusted) * kUpgradePct;
}

// The mean item level of what is worn: the pieces with a level (empty slots and cosmetics count 0 and are skipped).
// 0 when nothing counts.
inline int32_t AverageItemLevel(std::vector<uint32_t> const& levels)
{
    uint64_t sum = 0, n = 0;
    for (uint32_t l : levels)
        if (l > 0) { sum += l; ++n; }
    return n ? int32_t(sum / n) : 0;
}

// The effective item level ceiling for a level-cap companion. `configured_cap` is the operator's ceiling
// (Playerbot.Gear.MaxTier / RaidIlvlCap); the owner's average plus `slack` lowers it, but not below `floor`; an unknown owner
// (offline, 0) leaves the configured ceiling. A configured ceiling under the floor wins: the operator's word is final.
constexpr int32_t OwnerBasedCap(int32_t owner_avg, int32_t slack, int32_t floor, int32_t configured_cap)
{
    if (owner_avg <= 0)
        return configured_cap;
    int32_t const follow = std::max(owner_avg + slack, floor);
    return std::min(configured_cap, follow);
}

// What an owner asks of the gear of the companions it owns (per owner account, kept across restarts).
//   Match  the default: the best green / blue for the bot's level below the cap; at the cap pieces up to the owner's own gear level.
//   Best   the best gear for the bot's level; at the cap the full configured tier ceiling (Playerbot.Gear.MaxTier) whatever the owner wears.
//   Bis    best in slot, at the cap: the highest scoring raid piece for the role in every slot up to Naxxramas item level (tier 3's
//          ceiling), whatever the owner wears or MaxTier says; no per-bot variety. Below the cap it acts as Match.
//   T1/T2/T3  at the cap: the class tier set (Molten Core / Blackwing Lair / Naxxramas) plus raid pieces up to that tier's item
//          level for the other slots. Below the cap they act as Match.
enum class GearMode : uint8_t { Match = 0, Best = 1, T1 = 2, T2 = 3, T3 = 4, Bis = 5 };
constexpr uint8_t kGearModeCount = 6;

constexpr bool IsTierMode(GearMode m) { return m == GearMode::T1 || m == GearMode::T2 || m == GearMode::T3; }
// Modes that only exist at the level cap.
constexpr bool IsCapMode(GearMode m) { return IsTierMode(m) || m == GearMode::Bis; }
constexpr int32_t TierNumber(GearMode m) { return m == GearMode::T1 ? 1 : m == GearMode::T2 ? 2 : m == GearMode::T3 ? 3 : 0; }
constexpr char const* ModeName(GearMode m)
{
    switch (m)
    {
        case GearMode::Best: return "best";
        case GearMode::Bis:  return "bis";
        case GearMode::T1:   return "t1";
        case GearMode::T2:   return "t2";
        case GearMode::T3:   return "t3";
        default:             return "match";
    }
}

// The mode an owner's word names: "match", "best", "t1" / "tier 1" / "tier1", ... Case and spacing as the order parser leaves them
// (lower case). False when the word is no mode.
inline bool ParseGearMode(std::string_view w, GearMode& out)
{
    while (!w.empty() && w.front() == ' ') w.remove_prefix(1);
    while (!w.empty() && w.back() == ' ') w.remove_suffix(1);
    struct Entry { std::string_view word; GearMode mode; };
    static constexpr Entry kWords[] = {
        { "match", GearMode::Match }, { "me", GearMode::Match }, { "default", GearMode::Match }, { "best", GearMode::Best },
        { "bis", GearMode::Bis }, { "best in slot", GearMode::Bis }, { "best in slots", GearMode::Bis }, { "best in slot gear", GearMode::Bis },
        { "t1", GearMode::T1 }, { "tier1", GearMode::T1 }, { "tier 1", GearMode::T1 }, { "tier one", GearMode::T1 },
        { "t2", GearMode::T2 }, { "tier2", GearMode::T2 }, { "tier 2", GearMode::T2 }, { "tier two", GearMode::T2 },
        { "t3", GearMode::T3 }, { "tier3", GearMode::T3 }, { "tier 3", GearMode::T3 }, { "tier three", GearMode::T3 } };
    for (Entry const& e : kWords)
        if (w == e.word) { out = e.mode; return true; }
    return false;
}

// The server's allowed modes (Playerbot.OwnedGear.AllowedModes, a comma / space separated list of single words (bis, not "best in slot")). Bit i = GearMode(i). Match is always
// allowed, so a request that is refused has somewhere to fall back to. An empty or unreadable list allows everything.
inline uint32_t ParseAllowedModes(std::string_view list)
{
    uint32_t mask = 0;
    size_t i = 0;
    while (i <= list.size())
    {
        size_t j = list.find_first_of(", ", i);
        if (j == std::string_view::npos) j = list.size();
        GearMode m;
        if (j > i && ParseGearMode(list.substr(i, j - i), m))
            mask |= 1u << uint8_t(m);
        i = j + 1;
    }
    constexpr uint32_t kAll = (1u << kGearModeCount) - 1;
    return mask == 0 ? kAll : (mask | 1u);
}

constexpr bool ModeAllowed(uint32_t allowed_mask, GearMode m) { return (allowed_mask >> uint8_t(m)) & 1u; }

// The mode a companion actually gears by: the tier sets only exist at the level cap, and only when the server allows the mode.
constexpr GearMode EffectiveMode(GearMode requested, uint32_t allowed_mask, bool at_cap)
{
    if (!ModeAllowed(allowed_mask, requested))
        return GearMode::Match;
    if (IsCapMode(requested) && !at_cap)
        return GearMode::Match;
    return requested;
}

// The class tier set's name in ItemSet.db2 (Classic: one set per class and tier, worn by every spec of the class). Empty for a class
// without one. cls = the class id (1 warrior ... 11 druid).
inline std::string_view TierSetName(uint8_t cls, int32_t tier)
{
    // [class][tier 1..3]
    static constexpr std::string_view kSets[12][3] = {
        { {}, {}, {} },
        { "Battlegear of Might", "Battlegear of Wrath", "Dreadnaught's Battlegear" },                     // 1 warrior
        { "Lawbringer Armor", "Judgement Armor", "Redemption Armor" },                                     // 2 paladin
        { "Giantstalker Armor", "Dragonstalker Armor", "Cryptstalker Armor" },                             // 3 hunter
        { "Nightslayer Armor", "Bloodfang Armor", "Bonescythe Armor" },                                    // 4 rogue
        { "Vestments of Prophecy", "Vestments of Transcendence", "Vestments of Faith" },                   // 5 priest
        { {}, {}, {} },                                                                                    // 6 (death knight: none in Classic)
        { "The Earthfury", "The Ten Storms", "The Earthshatterer" },                                         // 7 shaman
        { "Arcanist Regalia", "Netherwind Regalia", "Frostfire Regalia" },                                 // 8 mage
        { "Felheart Raiment", "Nemesis Raiment", "Plagueheart Raiment" },                                  // 9 warlock
        { {}, {}, {} },                                                                                    // 10 (monk: none in Classic)
        { "Cenarion Raiment", "Stormrage Raiment", "Dreamwalker Raiment" } };                              // 11 druid
    if (cls >= 12 || tier < 1 || tier > 3)
        return {};
    return kSets[cls][tier - 1];
}

// Gear refresh points: a companion is regeared when it levels up, when it enters an instance (or moves to another) and when its
// owner changes the gear mode. The key packs level, mode and instance id (0 in the open world).
constexpr uint64_t RefreshKey(uint32_t level, uint32_t instance_id, GearMode mode = GearMode::Match)
{
    return (uint64_t(level & 0xFF) << 56) | (uint64_t(uint8_t(mode)) << 48) | instance_id;
}

} // namespace Playerbot::V2::Gear::Owned
