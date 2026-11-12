// Playerbot.AlwaysBuffs: every bot of the raiding levels carries its role's flask or elixirs, scrolls, food, weapon stone and
// world buffs all the time, put on straight (aura or temporary weapon enchant; no item used, no cast time, no GCD), and loses
// them on a battleground or arena map. One table: role -> rows.
//
// A row is a spell (the aura's own id, checked against the client spell data: the ids below were read from SpellName.db2) or an
// item (the buff is whatever the item's on-use spell leaves: its triggered spell for food, its own aura for a scroll, the
// temporary weapon enchant for a stone). `name` is a part of the spell's or item's own name, checked when the row is resolved;
// a row that does not match is logged "[always_buff] ... skipped" and left out, so a wrong id never gives the wrong buff.
// spell_group on this server (checked 2026-10-23): the flasks 17626/17627/17628 are in battle group 1 and guardian group 2 with
// Mongoose 17538 and Greater Firepower 26276 (exclusive), so those two are not used on a flask. Greater Arcane 16889, Elixir of
// Shadow Power 11476, Elixir of Frost Power 21923, Mageblood 24365 and Giants 16883 are in no group and stack with a flask.
#pragma once

#include "Define.h"
#include <span>

namespace Playerbot {

enum class BuffSet : uint8 { Tank, MeleeStrength, MeleeAgility, Caster, Healer, CasterShadow, CasterFrost };
constexpr uint8 kBuffSetCount = 7;

enum class AlwaysKind : uint8 { Spell, Item, Stone };

struct AlwaysAura
{
    AlwaysKind  kind;
    uint32      id;        // spell id (Spell) or item id (Item, Stone)
    char const* name;
    bool        horde_only = false;
    bool        world = false;   // a world buff (switch WorldBuffs); every other row is a consumable (switch Consumables)
};

// The switches, Playerbot.AlwaysBuffs.<Switch>.<Place> (playerbot.conf.dist). Place: Raid = raids and dungeons, World = everywhere
// else, Battleground = battlegrounds and arenas.
enum class AlwaysSwitch : uint8 { Supplies, Repair, WorldBuffs, Consumables };
enum class AlwaysPlace  : uint8 { Raid, World, Battleground };
constexpr uint8 kAlwaysSwitchCount = 4;
constexpr uint8 kAlwaysPlaceCount  = 3;

// The mask an AlwaysBuffIntent carries: which kinds are on where the bot stands.
constexpr uint8 kAlwaysMaskConsumables = 1;
constexpr uint8 kAlwaysMaskWorldBuffs  = 2;
constexpr uint8 kAlwaysMaskRepair      = 4;

constexpr uint8 kAlwaysBuffMinLevel = 58;

// Also under Playerbot.AlwaysBuffs: gear is kept whole (durability back to full, no repair bill) and a bot never has less gold than this.
constexpr int32 kAlwaysGoldFloor = 200;   // gold: enough for any trainer

// World buffs every role gets.
constexpr AlwaysAura kWorldBuffs[] = {
    { AlwaysKind::Spell, 22888, "Rallying Cry of the Dragonslayer", false, true },
    { AlwaysKind::Spell, 24425, "Spirit of Zandalar", false, true },
    { AlwaysKind::Spell, 15366, "Songflower Serenade", false, true },
    { AlwaysKind::Spell, 16609, "Warchief's Blessing", true, true },
};
constexpr AlwaysAura kTankBuffs[] = {
    { AlwaysKind::Spell, 17626, "Flask of the Titans" },          // +1200 health
    { AlwaysKind::Spell, 22818, "Mol'dar's Moxie", false, true },
    { AlwaysKind::Item,  10307, "Scroll of Stamina IV" },
    { AlwaysKind::Item,  10310, "Scroll of Strength IV" },
    { AlwaysKind::Item,  18045, "Tender Wolf Steak" },
    { AlwaysKind::Stone, 18262, "Elemental Sharpening Stone" },
};
constexpr AlwaysAura kMeleeStrengthBuffs[] = {
    { AlwaysKind::Spell, 17538, "Elixir of the Mongoose" },
    { AlwaysKind::Spell, 16883, "Elixir of the Giants" },
    { AlwaysKind::Spell, 22817, "Fengus' Ferocity", false, true },
    { AlwaysKind::Item,  10310, "Scroll of Strength IV" },
    { AlwaysKind::Item,  20452, "Smoked Desert Dumplings" },
    { AlwaysKind::Stone, 18262, "Elemental Sharpening Stone" },
};
constexpr AlwaysAura kMeleeAgilityBuffs[] = {
    { AlwaysKind::Spell, 17538, "Elixir of the Mongoose" },
    { AlwaysKind::Spell, 16883, "Elixir of the Giants" },
    { AlwaysKind::Spell, 22817, "Fengus' Ferocity", false, true },
    { AlwaysKind::Item,  10309, "Scroll of Agility IV" },
    { AlwaysKind::Item,  13928, "Grilled Squid" },
    { AlwaysKind::Stone, 18262, "Elemental Sharpening Stone" },
};
constexpr AlwaysAura kCasterBuffs[] = {
    { AlwaysKind::Spell, 17628, "Supreme Power" },                // Flask of Supreme Power: +150 spell damage
    { AlwaysKind::Spell, 16889, "Greater Arcane Elixir" },
    { AlwaysKind::Spell, 24365, "Mageblood Elixir" },
    { AlwaysKind::Spell, 22820, "Slip'kik's Savvy", false, true },
    { AlwaysKind::Item,  10308, "Scroll of Intellect IV" },
    { AlwaysKind::Item,  10306, "Scroll of Spirit IV" },
    { AlwaysKind::Item,  18254, "Runn Tum Tuber Surprise" },
};
// Warlock and shadow priest: the caster rows plus the Shadow Power elixir.
constexpr AlwaysAura kCasterShadowBuffs[] = {
    { AlwaysKind::Spell, 17628, "Supreme Power" },
    { AlwaysKind::Spell, 16889, "Greater Arcane Elixir" },
    { AlwaysKind::Spell, 11476, "Elixir of Shadow Power" },
    { AlwaysKind::Spell, 24365, "Mageblood Elixir" },
    { AlwaysKind::Spell, 22820, "Slip'kik's Savvy", false, true },
    { AlwaysKind::Item,  10308, "Scroll of Intellect IV" },
    { AlwaysKind::Item,  10306, "Scroll of Spirit IV" },
    { AlwaysKind::Item,  18254, "Runn Tum Tuber Surprise" },
};
// Frost mage: the caster rows plus the Frost Power elixir.
constexpr AlwaysAura kCasterFrostBuffs[] = {
    { AlwaysKind::Spell, 17628, "Supreme Power" },
    { AlwaysKind::Spell, 16889, "Greater Arcane Elixir" },
    { AlwaysKind::Spell, 21923, "Elixir of Frost Power" },
    { AlwaysKind::Spell, 24365, "Mageblood Elixir" },
    { AlwaysKind::Spell, 22820, "Slip'kik's Savvy", false, true },
    { AlwaysKind::Item,  10308, "Scroll of Intellect IV" },
    { AlwaysKind::Item,  10306, "Scroll of Spirit IV" },
    { AlwaysKind::Item,  18254, "Runn Tum Tuber Surprise" },
};
constexpr AlwaysAura kHealerBuffs[] = {
    { AlwaysKind::Spell, 17627, "Distilled Wisdom" },             // Flask of Distilled Wisdom: +2000 mana
    { AlwaysKind::Spell, 24365, "Mageblood Elixir" },
    { AlwaysKind::Spell, 22820, "Slip'kik's Savvy", false, true },
    { AlwaysKind::Item,  10308, "Scroll of Intellect IV" },
    { AlwaysKind::Item,  10306, "Scroll of Spirit IV" },
    { AlwaysKind::Item,  13931, "Nightfin Soup" },
};

constexpr std::span<AlwaysAura const> AlwaysBuffRoleRows(BuffSet set)
{
    switch (set)
    {
        case BuffSet::Tank:          return kTankBuffs;
        case BuffSet::MeleeStrength: return kMeleeStrengthBuffs;
        case BuffSet::MeleeAgility:  return kMeleeAgilityBuffs;
        case BuffSet::Caster:        return kCasterBuffs;
        case BuffSet::Healer:        return kHealerBuffs;
        case BuffSet::CasterShadow:  return kCasterShadowBuffs;
        case BuffSet::CasterFrost:   return kCasterFrostBuffs;
    }
    return {};
}

// What a row leaves on the bot, resolved once from the client data: the aura spell ids for a Spell or Item row (empty when the
// name check failed), the temporary enchantment id for a Stone row (empty = none). Bot/ClassicUpkeep.cpp.
std::span<uint32 const> AlwaysAuraEffects(AlwaysAura const& row);

class BotSnapshotView;
class GroupSnapshotView;

// The role's set and the kinds on where the bot stands, as idle:always_buff gives them to the AlwaysBuffIntent.
// Bot/States/Rules/OocConsumableRules.cpp.
BuffSet AlwaysBuffSetOf(BotSnapshotView const& s, GroupSnapshotView const& g);
uint8 AlwaysMask(BotSnapshotView const& s, GroupSnapshotView const& g);

} // namespace Playerbot
