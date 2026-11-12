#pragma once
// BisPools - best-in-slot candidates of a raid tier for the slots a class tier set does not cover (neck, back, rings, trinkets), by raid role.
//
// The names come from the Icy Veins Season of Mastery Phase 6 (Naxxramas) raid lists, fetched 2026-10-22: warrior dps and tank, rogue, hunter,
// mage, warlock, shadow priest, balance druid, retribution paladin, enhancement shaman, feral tank, and the priest, druid, paladin and shaman
// healer pages. The tier of an entry is where the item is first available. Each name is a
// CANDIDATE, never a command. The gear generator resolves a name to an item in the item data, drops a name the data
// lacks (logged once as "[bis] unresolved"), and takes a candidate for a slot only when the role-weighted raid score
// (Gear::ScoreItemRaid) beats what the generator picked there, under the tier's item level ceiling. A wrong or missing name therefore costs
// nothing. Pure data: tested in tests/game/ClassicBisPools.cpp.

#include "RaidLoadout.h"

namespace Playerbot::Gear::Bis {

enum class Kind : std::uint8_t { Neck, Back, Ring, Trinket, Offhand, Ranged };

struct PoolEntry
{
    Kind kind;
    std::uint8_t tier;        // first tier (1 = Molten Core / Onyxia, 2 = Blackwing Lair, 3 = Ahn'Qiraj / Naxxramas) the item is available in
    std::string_view name;    // exact English item name
};

// All entries a role may use in `tier` (those with entry.tier <= tier), in the order of the table.
std::span<PoolEntry const> PoolFor(Loadout::Role role);

constexpr bool InTier(PoolEntry const& e, int tier) { return e.tier <= tier; }

} // namespace Playerbot::Gear::Bis
