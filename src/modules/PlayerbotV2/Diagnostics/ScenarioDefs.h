// ScenarioDefs - the catalogue of bot scenario tests (`.playerbot scenario list`), as data.
//
// A scenario is: a squad (class, race, role per member), a level, where to put them, what counts as done, how long they get,
// and the limits the invariant checks hold them to (ScenarioMonitor.h). Adding a scenario is adding an entry here; the runner
// (ScenarioRunner.cpp) does the staging, grouping, entering and judging.
//
// Class and race ids are the 1.12 ones (ChrClasses / ChrRaces), written as numbers so this header needs no core includes and
// the tests can use it.

#pragma once

#include "ScenarioMonitor.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace Playerbot::Diagnostics {

namespace ScnClass { constexpr std::uint8_t Warrior = 1, Paladin = 2, Hunter = 3, Rogue = 4, Priest = 5, Shaman = 7, Mage = 8, Warlock = 9, Druid = 11; }
namespace ScnRace  { constexpr std::uint8_t Human = 1, Orc = 2, Dwarf = 3, NightElf = 4, Undead = 5, Tauren = 6, Gnome = 7, Troll = 8; }

struct ScenarioMember
{
    std::string name;                  // a letters-only character name; made on first use, reused afterwards
    std::uint8_t race;
    std::uint8_t cls;
    ScenarioRoleKind role;
};

// Parity: the damage_parity family (DamageParity.h) - one bot, a target dummy, measurements; done when its legs have run.
enum class ScenarioGoalKind : std::uint8_t { KillCreature, Parity };

struct ScenarioCheckpoint
{
    char const* key;                   // what follows the '@' in the scenario name
    std::uint32_t creature_entry;      // the squad starts in sight of this creature's first spawn on the scenario's map
    // A fixed staging spot on the normal route before the creature's room (a waypoint of the map's dungeon route chain, 0004_dungeon_route_
    // waypoints_data.sql, 33-60 y of walk before the boss). Used when set (z != 0); the squad is path-checked from it to the creature
    // once. Without one the runner searches a ring around the creature, which a pack-dense room never passes.
    float stage_x = 0.f, stage_y = 0.f, stage_z = 0.f;
};

struct ScenarioDef
{
    char const* name;
    char const* summary;
    std::uint8_t level;
    std::vector<ScenarioMember> squad;
    std::uint32_t map;                 // the instance the squad is sent to
    float x, y, z, o;                  // and where it arrives
    ScenarioGoalKind goal;
    std::uint32_t goal_entry;          // KillCreature: the creature entry that must die on `map`
    char const* goal_name;             // what that creature is called in the world DB: a wrong entry is caught at the start, not after the timeout
    std::uint32_t timeout_ms;
    ScenarioLimits limits;
    // Raids. The squad enters at (x, y, z) - the instance entrance - and is then moved to a spot in sight of the first spawn of
    // arrive_near_entry (the boss's room; the encounter is pre-spawned). order_attack: `order_delay_ms` into the run the main tank is
    // ordered onto goal_entry, which is what the owner's Attack does in a raid with a human leader. 0 / false = a dungeon scenario.
    std::uint32_t arrive_near_entry = 0;
    // Staging kills every hostile on the map but the goal boss, its formation and this entry (the boss's own adds); 0 = none.
    std::uint32_t keep_entry = 0;
    bool order_attack = false;
    std::uint32_t order_delay_ms = 0;
    // A whole-dungeon clear to the last boss: long, so `runall` with no filter leaves it out (`runall full` or `run <name>` starts it).
    bool full_clear = false;
    // The damage_parity family (goal Parity): which measurement (Parity::Kind: 1 damage legs, 2 finisher sweep, 3 talent audit) and how long a
    // damage leg measures. 0 = not a parity scenario.
    std::uint8_t parity = 0;
    std::uint32_t parity_leg_s = 180;
    // The name of a member played like chris's own character: from the start of the run its AI is detached and the runner drives it,
    // following the main tank and attacking what the tank attacks (a player who only presses "assist"). The group logic still sees a
    // bot-led group. nullptr = every member runs its own AI.
    char const* stand_in = nullptr;
    // Checkpoint starts (`scenario run <name>@<key>`): the squad enters at the entrance as usual, then is moved to a spot in sight of the first
    // spawn of the checkpoint's creature (a boss or a pack's mob) and plays on from there, bot-led and with no Hold, so a run does not replay
    // the part that is already solved. The goal and the first-death rule stay. Earlier bosses stay alive in the instance.
    std::vector<ScenarioCheckpoint> checkpoints;
};

// The party every dungeon scenario uses: the shape of chris's own (tank, healer, mage, rogue) plus a hunter for the fifth. The same
// five characters are re-levelled and re-geared for each dungeon, so a full run of the set makes five characters, not sixty.
inline std::vector<ScenarioMember> StandardParty()
{
    return {
        { "Scnwarrior", ScnRace::Human, ScnClass::Warrior, ScenarioRoleKind::Tank },
        { "Scnpriest",  ScnRace::Human, ScnClass::Priest,  ScenarioRoleKind::Healer },
        { "Scnmage",    ScnRace::Human, ScnClass::Mage,    ScenarioRoleKind::Dps },
        { "Scnrogue",   ScnRace::Human, ScnClass::Rogue,   ScenarioRoleKind::Dps },
        { "Scnhunter",  ScnRace::Dwarf, ScnClass::Hunter,  ScenarioRoleKind::Dps },
    };
}

// One vanilla 5-man, from its entrance to its first boss. Adding a dungeon is adding a row to kDungeons below, nothing else.
struct DungeonRow
{
    char const* name;                  // scenario name: <dungeon>_<boss>
    char const* summary;
    std::uint32_t map;
    float x, y, z;                     // where the entrance areatrigger puts the party (sql/custom/world/..._classic_areatrigger_teleport.sql)
    float o_deg;                       // and its facing, in degrees as that file has it
    std::uint8_t level;                // inside the dungeon's range, a little above its floor
    std::uint32_t boss_entry;
    char const* boss_name;
    std::uint32_t timeout_min;
};

// Ordered by level, lowest first: `runall` re-levels the shared party for each row, and a character is only ever levelled up.
// Entrances are the Classic areatrigger_teleport targets; levels are the dungeon's usual range; the boss is the first one on the way in.
// Dire Maul, Stratholme, Scholomance, Sunken Temple, Maraudon and Blackrock Spire are not here yet: several entrances or a first boss
// that is not on the way in (add them once their routes have been looked at).
inline constexpr DungeonRow kDungeons[] = {
    { "rfc_oggleflint",       "Ragefire Chasm: level 14 party to Oggleflint",              389,     0.797643f,   -8.23429f,  -15.5288f, 270.f, 14, 11517, "Oggleflint",          15 },
    { "wc_cobrahn",           "Wailing Caverns: level 18 party to Lord Cobrahn",           43,   -158.441f,   131.601f,  -74.2552f, 335.f, 18,  3669, "Lord Cobrahn",       20 },
    { "deadmines_rhahkzor",   "Deadmines: level 21 party to Rhahk'Zor",                    36,    -14.5732f, -385.475f,   62.4561f,  90.f, 21,   644, "Rhahk'Zor",          20 },
    { "sfk_rethilgore",       "Shadowfang Keep: level 24 party to Rethilgore",             33,   -228.191f,  2111.41f,    76.8904f,  70.f, 24,  3914, "Rethilgore",         15 },
    { "bfd_ghamoora",         "Blackfathom Deeps: level 26 party to Ghamoo-ra",            48,   -150.234f,   106.594f,  -39.779f,  255.f, 26,  4887, "Ghamoo-ra",          20 },
    { "stockade_targorr",     "Stormwind Stockade: level 27 party to Targorr the Dread",   34,     48.9849f,    0.483882f, -16.3942f, 360.f, 27,  1696, "Targorr the Dread",  15 },
    { "smgraveyard_vishas",   "Scarlet Monastery Graveyard: level 30 party to Vishas",     189,  1687.27f,   1050.09f,    18.6773f,  90.f, 30,  3983, "Interrogator Vishas",15 },
    { "rfk_aggem",            "Razorfen Kraul: level 31 party to Aggem Thorncurse",        47,   1942.27f,   1544.23f,    83.3055f,  75.f, 31,  4424, "Aggem Thorncurse",   20 },
    { "gnomeregan_grubbis",   "Gnomeregan: level 32 party to Grubbis",                     90,   -329.098f,    -3.20722f, -152.851f, 170.f, 32,  7361, "Grubbis",            20 },
    { "rfd_tutenkash",        "Razorfen Downs: level 40 party to Tuten'kash",              129,  2593.68f,   1111.23f,    50.9518f, 270.f, 40,  7355, "Tuten'kash",         25 },
    { "uldaman_revelosh",     "Uldaman: level 44 party to Revelosh",                       70,   -228.859f,    46.1018f,  -46.0186f,  90.f, 44,  6910, "Revelosh",           20 },
    { "zf_antusul",           "Zul'Farrak: level 48 party to Antu'sul",                    209,  1212.67f,    842.04f,      8.93346f, 360.f, 48,  8127, "Antu'sul",           25 },
    { "brd_roccor",           "Blackrock Depths: level 54 party to Lord Roccor",           230,   456.929f,    34.0923f,  -68.0896f, 270.f, 54,  9025, "Lord Roccor",        20 },
};

inline ScenarioDef MakeDungeonScenario(DungeonRow const& r)
{
    ScenarioDef d{};
    d.name = r.name;
    d.summary = r.summary;
    d.level = r.level;
    d.squad = StandardParty();
    d.map = r.map;
    d.x = r.x; d.y = r.y; d.z = r.z;
    d.o = r.o_deg * 3.14159265f / 180.f;
    d.goal = ScenarioGoalKind::KillCreature;
    d.goal_entry = r.boss_entry;
    d.goal_name = r.boss_name;
    d.timeout_ms = r.timeout_min * 60u * 1000u;
    d.limits.expected_map = r.map;
    // No per-dungeon floor plan: a wide box around the entrance still catches a fall through the map and a walk out of the dungeon.
    d.limits.bounds = { true, r.x - 600.f, r.x + 600.f, r.y - 600.f, r.y + 600.f, r.z - 300.f, r.z + 300.f };
    return d;
}

// ---- Molten Core: a 40-bot raid, level 60, to one boss at a time ----
// One row per kind of raider: the name prefix (a suffix letter makes each name), race, class, the role pinned on it and how many.
// 4 tanks, 12 healers, 24 DPS (five warlocks for Banish). Alliance only: a raid group does not take both factions.
struct RaidRow
{
    char const* prefix;
    std::uint8_t race;
    std::uint8_t cls;
    ScenarioRoleKind role;
    std::uint8_t count;
};

inline constexpr RaidRow kMcRaid[] = {
    { "Mctankw",  ScnRace::Human,    ScnClass::Warrior, ScenarioRoleKind::Tank,   3 },
    { "Mctankd",  ScnRace::NightElf, ScnClass::Druid,   ScenarioRoleKind::Tank,   1 },
    { "Mcheals",  ScnRace::Human,    ScnClass::Priest,  ScenarioRoleKind::Healer, 5 },
    { "Mchealp",  ScnRace::Human,    ScnClass::Paladin, ScenarioRoleKind::Healer, 4 },
    { "Mchealdr", ScnRace::NightElf, ScnClass::Druid,   ScenarioRoleKind::Healer, 3 },
    { "Mcmage",   ScnRace::Human,    ScnClass::Mage,    ScenarioRoleKind::Dps,    5 },
    { "Mclock",   ScnRace::Human,    ScnClass::Warlock, ScenarioRoleKind::Dps,    5 },
    { "Mcrogue",  ScnRace::Human,    ScnClass::Rogue,   ScenarioRoleKind::Dps,    4 },
    { "Mchunt",   ScnRace::Dwarf,    ScnClass::Hunter,  ScenarioRoleKind::Dps,    4 },
    { "Mcwarr",   ScnRace::Dwarf,    ScnClass::Warrior, ScenarioRoleKind::Dps,    3 },
    { "Mcret",    ScnRace::Human,    ScnClass::Paladin, ScenarioRoleKind::Dps,    1 },
    { "Mcshad",   ScnRace::Human,    ScnClass::Priest,  ScenarioRoleKind::Dps,    2 },
};

inline std::vector<ScenarioMember> RaidSquad(RaidRow const* rows, std::size_t n)
{
    std::vector<ScenarioMember> squad;
    for (std::size_t i = 0; i < n; ++i)
        for (std::uint8_t k = 0; k < rows[i].count; ++k)
            squad.push_back({ std::string(rows[i].prefix) + char('a' + k), rows[i].race, rows[i].cls, rows[i].role });
    return squad;
}

struct RaidBossRow
{
    char const* name;
    char const* summary;
    std::uint32_t boss_entry;
    char const* boss_name;
    std::uint32_t timeout_min;
    std::uint32_t keep_entry = 0;        // the boss's own adds (staging leaves them alive); 0 = none
    std::uint32_t min_tranq_shots = 0;   // a kill must have seen at least this many Tranquilizing Shots (Magmadar's Frenzy); 0 = not checked
    std::uint32_t min_decurses = 0;      // ... and this many curses removed (Lucifron's, Gehennas'); 0 = not checked
};

// In the order of the raid. Each starts at the boss's room with the encounter pre-spawned (the earlier bosses alive too).
inline constexpr RaidBossRow kMcBosses[] = {
    { "mc_garr",      "Molten Core: 40 level 60 raiders to Garr",      12057, "Garr",      15, 12099 },   // 12099 Firesworn
    { "mc_lucifron",  "Molten Core: 40 level 60 raiders to Lucifron",  12118, "Lucifron",  15, 12119, 0, 1 },   // Flamewaker Protector; Lucifron's Curse must be removed
    { "mc_magmadar",  "Molten Core: 40 level 60 raiders to Magmadar",  11982, "Magmadar",  15, 0, 1 },          // Frenzy must be tranquilized
    { "mc_gehennas",  "Molten Core: 40 level 60 raiders to Gehennas",  12259, "Gehennas",  15, 11661, 0, 1 },   // Flamewaker; Gehennas' Curse must be removed
    { "mc_geddon",    "Molten Core: 40 level 60 raiders to Baron Geddon", 12056, "Baron Geddon", 15 },   // Inferno (19698, 20 y): the raid must leave his range; more than 6 deaths fails it
    { "mc_ragnaros",  "Molten Core: 40 level 60 raiders to Ragnaros",  11502, "Ragnaros",  20 },   // Elemental Fire on the tank, Wrath threat reset and knockback, Magma Blast on a target out of melee reach, submerge at 3 min (Sons of Flame)
};

inline ScenarioDef MakeMcScenario(RaidBossRow const& r)
{
    ScenarioDef d{};
    d.name = r.name;
    d.summary = r.summary;
    d.level = 60;
    d.squad = RaidSquad(kMcRaid, sizeof(kMcRaid) / sizeof(kMcRaid[0]));
    d.map = 409;
    d.x = 1091.89f; d.y = -466.985f; d.z = -105.084f;               // the Molten Core entrance (areatrigger_teleport 1102886)
    d.o = 180.f * 3.14159265f / 180.f;
    d.goal = ScenarioGoalKind::KillCreature;
    d.goal_entry = r.boss_entry;
    d.goal_name = r.boss_name;
    d.timeout_ms = r.timeout_min * 60u * 1000u;
    d.limits.expected_map = 409;
    d.limits.bounds = { true, d.x - 800.f, d.x + 800.f, d.y - 800.f, d.y + 800.f, d.z - 250.f, d.z + 250.f };
    d.limits.expected_group = std::uint32_t(d.squad.size());
    d.limits.tolerated_deaths = 5;                                   // more than five dead raiders fails the run
    d.limits.max_deaths = 6;
    d.limits.dps_hold_ms = 20000;
    d.limits.tanks_dead_is_wipe = true;
    d.limits.min_tranq_shots = r.min_tranq_shots;
    d.limits.min_decurses = r.min_decurses;
    d.limits.stray_yards = 120.f;                                    // a raid spreads over a room
    d.arrive_near_entry = r.boss_entry;
    d.keep_entry = r.keep_entry;
    d.order_attack = true;
    d.order_delay_ms = 20000;
    return d;
}

// ---- damage_parity: one level 60 bot per class against a level 63 target dummy ----
// damage_parity_<class>   seven legs of `leg_s` seconds (no gear / tier 1 / tier 3 / +spell power / +attack power / 0 armor / five Sunders)
// finisher_sweep_<class>  rogue, druid, warrior: every combo point or rage scaled spell at 1-5 points / rising rage, the damage must grow
// talent_audit_<class>    the class's key talents in the spellbook with their effects, and (warlock) Improved Shadow Bolt's +20 % after a crit
struct ParityRow
{
    char const* cls_name;              // the scenario name suffix
    char const* member;                // the character: letters only
    std::uint8_t race;
    std::uint8_t cls;
};

inline constexpr ParityRow kParityClasses[] = {
    { "warrior", "Parwarrior", ScnRace::Human,    ScnClass::Warrior },
    { "paladin", "Parpaladin", ScnRace::Human,    ScnClass::Paladin },
    { "hunter",  "Parhunter",  ScnRace::Dwarf,    ScnClass::Hunter },
    { "rogue",   "Parrogue",   ScnRace::Human,    ScnClass::Rogue },
    { "priest",  "Parpriest",  ScnRace::Human,    ScnClass::Priest },
    { "shaman",  "Parshaman",  ScnRace::Orc,      ScnClass::Shaman },
    { "mage",    "Parmage",    ScnRace::Human,    ScnClass::Mage },
    { "warlock", "Parwarlock", ScnRace::Human,    ScnClass::Warlock },
    { "druid",   "Pardruid",   ScnRace::NightElf, ScnClass::Druid },
};

// economy_audit_<priest|paladin|shaman|druid|mage|warlock>  max mana against the table, regen out of combat / idle in combat / casting, costs, heals
// incoming_damage_<warrior|paladin>  a level 63 dummy hits a tank at bare / T1 / T3 armor, then Firesworn, Magmadar and a Deadmines mob
// `kind` is Parity::Kind (1 damage, 2 finisher, 3 talent audit, 4 economy, 5 incoming).
inline ScenarioDef MakeParityScenario(ParityRow const& r, char const* name, char const* summary, std::uint8_t kind, std::uint32_t timeout_min)
{
    ScenarioDef d{};
    d.name = name;
    d.summary = summary;
    d.level = 60;
    d.squad = { { r.member, r.race, r.cls, ScenarioRoleKind::Dps } };
    d.map = 0;
    d.x = 0.f; d.y = 0.f; d.z = 0.f; d.o = 0.f;
    d.goal = ScenarioGoalKind::Parity;
    d.goal_entry = 0;
    d.goal_name = "";
    d.timeout_ms = timeout_min * 60u * 1000u;
    d.limits.expected_map = 0;
    d.parity = kind;
    d.full_clear = true;               // long: `runall` with no filter leaves it out, `runall damage_parity` runs the family
    return d;
}

inline std::vector<ScenarioDef> const& ScenarioCatalogue()
{
    static std::vector<ScenarioDef> const defs = []
    {
        std::vector<ScenarioDef> v;
        for (DungeonRow const& r : kDungeons)
        {
            ScenarioDef d = MakeDungeonScenario(r);
            // The Deadmines have a known floor plan: the box is the part of the instance the route to the first boss may use.
            if (std::string(r.name) == "deadmines_rhahkzor")
                d.limits.bounds = { true, -420.f, 100.f, -900.f, -340.f, 0.f, 110.f };
            v.push_back(std::move(d));
        }
        // Deadmines, entrance to Edwin VanCleef: Rhahk'Zor, Sneed, Gilnid, the door and cannon, Mr. Smite, Greenskin, VanCleef. The
        // floor plan box covers the whole instance (VanCleef's ship is at y -877). Bot-led, the standard five; a run of 45 minutes.
        {
            static constexpr DungeonRow kFull = { "deadmines_full", "Deadmines: level 21 party, entrance to Edwin VanCleef",
                                                  36, -14.5732f, -385.475f, 62.4561f, 90.f, 21, 639, "Edwin VanCleef", 45 };
            ScenarioDef d = MakeDungeonScenario(kFull);
            d.limits.bounds = { true, -420.f, 100.f, -900.f, -340.f, 0.f, 110.f };
            d.full_clear = true;
            // In the order of the clear (creature entries from the Deadmines creature table: a wrong one is reported when the run starts).
            // Staging spots: waypoints of the Deadmines route chain (map 36, seq in brackets) 33-55 y of walk before each boss room.
            d.checkpoints = { { "rhahkzor", 644, -172.036f, -411.199f, 56.910f },   // [7] the corridor before the Rhahk'Zor room
                              { "sneed", 642 /* Sneed's Shredder */, -257.382f, -486.271f, 49.768f },   // [14] the tunnel before the lumber mill
                              { "gilnid", 1763, -209.837f, -568.622f, 21.462f },   // [28] the foundry floor
                              { "smite", 646, -2.585f, -766.284f, 6.172f },   // [41] the ramp up to the ship
                              { "greenskin", 647, -89.643f, -781.119f, 29.200f },   // [47] the gangway
                              { "vancleef", 639, -49.167f, -795.944f, 38.956f } };   // [50] the deck
            // The same clear with the rogue played like chris's own: it follows the tank and hits the tank's target, nothing else.
            ScenarioDef p = d;
            p.name = "deadmines_player";
            p.summary = "Deadmines: level 21 party with a rogue played like a player, entrance to Edwin VanCleef";
            p.stand_in = "Scnrogue";
            v.push_back(std::move(d));
            v.push_back(std::move(p));
        }
        for (RaidBossRow const& r : kMcBosses)
            v.push_back(MakeMcScenario(r));
        // The names are built once and kept: ScenarioDef carries char const*.
        static std::deque<std::string> s_parity_names;
        for (ParityRow const& r : kParityClasses)
        {
            s_parity_names.push_back(std::string("damage_parity_") + r.cls_name);
            s_parity_names.push_back("Damage parity, " + std::string(r.cls_name) + ": seven legs against a level 63 dummy, per-spell outcomes, stat sensitivity, armor, DPS against the vanilla ranges");
            v.push_back(MakeParityScenario(r, s_parity_names[s_parity_names.size() - 2].c_str(), s_parity_names.back().c_str(), 1, 45));
            if (r.cls == ScnClass::Rogue || r.cls == ScnClass::Druid || r.cls == ScnClass::Warrior)
            {
                s_parity_names.push_back(std::string("finisher_sweep_") + r.cls_name);
                s_parity_names.push_back("Finisher sweep, " + std::string(r.cls_name) + ": combo point / rage scaled spells at 1-5 points, the damage must grow");
                v.push_back(MakeParityScenario(r, s_parity_names[s_parity_names.size() - 2].c_str(), s_parity_names.back().c_str(), 2, 20));
            }
            if (r.cls == ScnClass::Priest || r.cls == ScnClass::Paladin || r.cls == ScnClass::Shaman || r.cls == ScnClass::Druid ||
                r.cls == ScnClass::Mage || r.cls == ScnClass::Warlock)
            {
                s_parity_names.push_back(std::string("economy_audit_") + r.cls_name);
                s_parity_names.push_back("Economy audit, " + std::string(r.cls_name) + ": mana pool, regen out of combat / idle in combat / casting, spell costs, heal sizes");
                v.push_back(MakeParityScenario(r, s_parity_names[s_parity_names.size() - 2].c_str(), s_parity_names.back().c_str(), 4, 8));
            }
            if (r.cls == ScnClass::Warrior || r.cls == ScnClass::Paladin)
            {
                s_parity_names.push_back(std::string("incoming_damage_") + r.cls_name);
                s_parity_names.push_back("Incoming damage, " + std::string(r.cls_name) + " tank: a level 63 dummy at bare / T1 / T3 armor, then Firesworn, Magmadar and a Deadmines mob");
                v.push_back(MakeParityScenario(r, s_parity_names[s_parity_names.size() - 2].c_str(), s_parity_names.back().c_str(), 5, 15));
            }
            if (r.cls == ScnClass::Hunter)
            {
                s_parity_names.push_back("trap_audit_hunter");
                s_parity_names.push_back("Trap audit, hunter: Explosive, Immolation, Frost and Freezing Trap out of combat, a dummy walked onto each, the trap must fire and do its effect");
                v.push_back(MakeParityScenario(r, s_parity_names[s_parity_names.size() - 2].c_str(), s_parity_names.back().c_str(), 6, 8));
            }
            s_parity_names.push_back(std::string("talent_audit_") + r.cls_name);
            s_parity_names.push_back("Talent audit, " + std::string(r.cls_name) + ": key talents in the spellbook with their effects" +
                                     (r.cls == ScnClass::Warlock ? ", Improved Shadow Bolt +20 % after a crit" : ""));
            v.push_back(MakeParityScenario(r, s_parity_names[s_parity_names.size() - 2].c_str(), s_parity_names.back().c_str(), 3, 10));
        }
        return v;
    }();
    return defs;
}

inline ScenarioDef const* FindScenario(std::string const& name)
{
    for (ScenarioDef const& d : ScenarioCatalogue())
        if (name == d.name)
            return &d;
    return nullptr;
}

} // namespace Playerbot::Diagnostics
