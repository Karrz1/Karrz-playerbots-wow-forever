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
#include <string_view>
#include <utility>
#include <array>
#include <initializer_list>
#include <vector>

namespace Playerbot::Diagnostics {

namespace ScnClass { constexpr std::uint8_t Warrior = 1, Paladin = 2, Hunter = 3, Rogue = 4, Priest = 5, Shaman = 7, Mage = 8, Warlock = 9, Druid = 11; }
namespace ScnRace  { constexpr std::uint8_t Human = 1, Orc = 2, Dwarf = 3, NightElf = 4, Undead = 5, Tauren = 6, Gnome = 7, Troll = 8; }
namespace ScnSchool { constexpr std::uint8_t Fire = 2, Nature = 3, Frost = 4, Shadow = 5; }   // SpellSchools values
namespace ScnItem { constexpr std::uint32_t OnyxiaScaleCloak = 15138; }   // its aura 22683 is what Shadow Flame 22539 checks (SpellEffects.cpp)

struct ScenarioMember
{
    std::string name;                  // a letters-only character name; made on first use, reused afterwards
    std::uint8_t race;
    std::uint8_t cls;
    ScenarioRoleKind role;
    // The talent build the member runs (Classic virtual spec id, the retail ids of Api/ClassicTalentBuilds.cpp: 71 Arms, 253 Beast Mastery ...).
    // 0 = the role's default spec. Set after the role is pinned and applied as a respec, so a reused character changes build.
    std::uint32_t spec = 0;
    bool melee = false;                // the dps_scorecard raid stands a melee member at the boss, everyone else at casting range
};

// Parity: the damage_parity family (DamageParity.h) - one bot, a target dummy, measurements; done when its legs have run.
// FollowRoute: the stand-in walks ScenarioDef::route like a player and the squad must be with him at every point (Fleet/OwnerCatchUp.h).
enum class ScenarioGoalKind : std::uint8_t { KillCreature, Parity, FollowRoute };

// A point of a FollowRoute walk. The stand-in walks to it; where no walking route leads there he is set down on it, as a player who takes
// a jump the navmesh does not have, and the squad has to come after him.
struct ScenarioRoutePoint
{
    char const* label;
    float x, y, z;
    // water: the point is the nearest deep water (deeper than a bot is tall) within 150 y of (x, y) that he can walk to, found when the
    // leg begins; he goes there at the swim z (lake_swim). hold_s: once the squad is with him he stays this long before going on.
    bool water = false;
    std::uint32_t hold_s = 0;
    // attack_entry: once he is at the point with the squad, he selects the nearest living creature of this entry within 80 y, says
    // "attack" as a player does and fights it; the leg goes on when it is dead. Every 5 s of the fight each member's state is logged
    // ([scenario_water_fight]); no kill in 90 s fails the run (vile_reef_fight). 0 = no fight.
    std::uint32_t attack_entry = 0;
};

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
    // The raid lands at least this far from that creature (0 = the default 40 y). A boss that aggroes on sight (Onyxia: 58 y) is not held, so a
    // landing inside its reach starts the fight on whoever stands nearest instead of on the tank walking in (onyxia_full n6, 2026-10-14).
    float arrive_min_yards = 0.f;
    // A fixed landing spot instead of the ring search (z != 0), path-checked to the creature once, as a checkpoint's staging spot: a room
    // whose own corners are the danger (Kel'Thuzad's alcoves full of phase 1 adds that a raid on the rings walks into).
    float stage_x = 0.f, stage_y = 0.f, stage_z = 0.f;
    // The point the staging spot's route is checked to instead of the boss (z != 0): a boss who stands where no raider walks until his
    // fight brings him down (Gothik on his balcony, 18 y above his room: naxx22 failed its setup at 0 s, no route from the floor to him).
    float route_x = 0.f, route_y = 0.f, route_z = 0.f;
    // The school the squad gears resistance for, as a vanilla raid dressed for the fight (nature for Princess Huhuran); 0 = none. Every
    // armour and jewellery slot takes the piece with the most of it under the tier's item level (GearGenerationContext::resist_school).
    std::uint8_t resist_school = 0;
    // A piece every raider wears for the fight whatever it scores (the Onyxia Scale Cloak against the drakes' Shadow Flame); 0 = none.
    std::uint32_t wear_entry = 0;
    // An item the tank carries into the run, as a party that has done the dungeon's key quest or an earlier wing comes with it; 0 = none.
    // A dungeon door key: The Scarlet Key (7146, Doan's Strongbox in the Library) for Herod's Door, the Chapel Door and Stratholme's
    // Scarlet Bastion, the Key to the City for Stratholme's gates, the Shadowforge Key, the Skeleton Key, the Staff of Prehistoria. A
    // party without it stops at the door, as players do; the bot that carries it opens the door (BotSnapshotBuilder KeyForShutDoor).
    // carry_entry_b: a second item, carried the same way (Blackrock Depths: the Shadowforge Torch for the Lyceum's braziers).
    std::uint32_t carry_entry = 0, carry_entry_b = 0;
    // Staging kills every hostile on the map but the goal boss, its formation and these entries (the boss's own adds); 0 = none.
    std::uint32_t keep_entry = 0;
    std::uint32_t keep_entry_b = 0, keep_entry_c = 0;
    // Staging leaves the boss's formation alone (Garr's Firesworn stand in it round him). A world DB that links the room's trash, or a whole
    // wing's, into the boss's formation (Faerlina's Acolytes 31 y off, Patchwerk's Bile Retchers 200 y down the hall) leaves that trash for
    // the fight: members further than this from the boss's spawn are killed like any trash, kept entries excepted. 0 = the whole formation stays.
    float clear_formation_beyond = 0.f;
    // A fight with no boss death to wait for (Majordomo Executus yields when his eight adds are dead): the goal is reached when this many
    // creatures of these two entries have died on the map. 0 = the goal boss's death.
    std::uint32_t goal_adds_a = 0, goal_adds_b = 0, goal_adds_needed = 0;
    // An add that must not die before the goal boss (Garr's Firesworn): each one that does is reported when the boss is dead. 0 = not checked.
    std::uint32_t spare_entry = 0;
    bool order_attack = false;
    std::uint32_t order_delay_ms = 0;
    // A whole-dungeon clear to the last boss: long, so `runall` with no filter leaves it out (`runall full` or `run <name>` starts it).
    bool full_clear = false;
    // A whole-dungeon clear whose bosses can fall in any order: the goal is reached when the goal boss and every checkpoint boss are
    // dead, not at the goal boss's death. The Scarlet Monastery Cathedral: Mograine and Whitemane are one fight (she raises him at
    // half health, either can fall last), and the hall's trash, fleeing, brings Mograine on before the tank has been to Fairbanks
    // behind the secret door (smcathedral_full af56: Whitemane fell at 1554 s with Mograine back up and Fairbanks not yet visited,
    // the run ended there and failed both as skipped). false = the goal boss's death ends the run.
    bool goal_after_checkpoints = false;
    // The damage_parity family (goal Parity): which measurement (Parity::Kind: 1 damage legs, 2 finisher sweep, 3 talent audit) and how long a
    // damage leg measures. 0 = not a parity scenario.
    std::uint8_t parity = 0;
    // A damage_parity run played as this specialization (ChrSpecialization id, warrior Fury 72, Protection 73); 0 = what the talents say.
    std::uint32_t parity_spec = 0;
    std::uint32_t parity_leg_s = 180;
    // The name of a member played like the owner's own character: from the start of the run its bot logic is detached and the runner drives it,
    // following the main tank and attacking what the tank attacks (a player who only presses "assist"). The group logic still sees a
    // bot-led group. nullptr = every member runs its own bot logic.
    char const* stand_in = nullptr;
    // A player-led run: the stand-in is made the group leader and counts as a player (Scenario::PlaysAsHuman), so the squad plays as the
    // owner's group does - the dungeon autopilot stands down, the bots follow him, the group fight joins his fights. The runner plays him
    // like the owner: he walks the dungeon's route, stops at the next pack, says "pull back" with it selected (the tank pulls it to him),
    // "attack" once the tank has it, fights the tank's target, and walks on when the group has its health and mana back.
    bool player_lead = false;
    // Checkpoint starts (`scenario run <name>@<key>`): the squad enters at the entrance as usual, then is moved to a spot in sight of the first
    // spawn of the checkpoint's creature (a boss or a pack's mob) and plays on from there, bot-led and with no Hold, so a run does not replay
    // the part that is already solved. The goal and the first-death rule stay. Earlier bosses stay alive in the instance.
    std::vector<ScenarioCheckpoint> checkpoints;
    // A goal boss with no spawn row in the creature table (a script summons it: Ragnaros, by Majordomo): the lair it is summoned at
    // (lair_z != 0). The runner finds the room from it and summons the boss itself, as the instance script does. No DB change.
    float lair_x = 0.f, lair_y = 0.f, lair_z = 0.f, lair_o = 0.f;
    // A game object the raid leader uses to start the fight (Chromaggus' lever 179148 opens his side door; until then the script keeps him
    // unattackable): when the attack is due the main tank uses the nearest one within 200 y, and the attack waits for the boss to become
    // attackable (up to 30 s). 0 = none.
    std::uint32_t start_object = 0;
    // A game event the raid leader sends to bring the boss out (Gahz'ranka: event 9104, the Mudskunk Lure's; until it the instance
    // script despawns him the moment he spawns): when the attack is due the main tank sends it, and the attack waits for the boss to
    // be alive within 400 y (up to 30 s). 0 = none.
    std::uint32_t start_event = 0;
    // A goal boss that a creature of the room summons when a player comes near it (Ouro's Spawner 15957 casts Summon Ouro 26061 on a player
    // within 25 y; Ouro has no spawn row): the raid is placed from the summoner's spawn as from a boss's, staging leaves the summoner alive,
    // and when the attack is due the summoner casts summoner_spell, as its script does when the main tank walks up, and the attack waits
    // for the boss (up to 30 s). 0 = none.
    std::uint32_t summoner_entry = 0;
    std::uint32_t summoner_spell = 0;
    // A fight the raid opens on one of the boss's adds (Bloodlord Mandokir waits on his raptor, immune, until his Vilebranch Speaker dies):
    // when the attack is due and a living one stands within 400 y of the main tank, the order is onto the nearest one instead of the boss.
    // The goal stays the boss's death. 0 = the order is onto the boss.
    std::uint32_t start_add = 0;
    // A fight the raid leader starts by talking to a creature (Vaelastrasz's gossip; Lord Victor Nefarius's "Attack them, minions!"): when the
    // attack is due, the instance first gets start_instance_data (SetData type, value: the steps a raid has done before, which the runner
    // skips - Razorgore dead, the Vaelastrasz room entered), then the main tank picks the first gossip option of start_npc
    // start_gossip_steps times, as a player clicks through the menu, once the creature offers gossip (up to 60 s: Vaelastrasz's intro), or
    // the creature's script gets start_npc_action (CreatureAI::DoAction, what the gossip option's DB script does). Then the attack waits
    // for the boss to turn hostile and attackable (up to 60 s: Vaelastrasz's speech is 36 s). 0 = none.
    std::uint32_t start_npc = 0;
    std::uint8_t start_gossip_steps = 0;
    std::int32_t start_npc_action = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> start_instance_data;
    // A boss whose way in a raid opens on its way there (Thaddius: Gluth's death unlocks his door, 181121, and the first raider at it opens
    // it): once the trash is cleared and before the landing, the instance gets entry_instance_data (SetData type, value: the earlier bosses'
    // deaths, which the clearing's despawns never report), then the door entry_door nearest the landing is opened, as that raider
    // does. Without it the door stays shut and locked, and a route through it is no route (bot paths are kept clear of closed doors).
    // Empty / 0 = none.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> entry_instance_data;
    std::uint32_t entry_door = 0;
    // A fight of several bosses pulled together, each by its own tank (the Four Horsemen): at the attack order the next tanks are sent onto
    // the kept companions (keep_entry, keep_entry_b, keep_entry_c) with the main tank, one each, as a raid's tanks charge together. false =
    // the main tank opens alone.
    bool open_each = false;
    // A goal boss the fight summons later (Nefarian, once 40 drakonids of Victor Nefarius's phase 1 are dead): after the start above the raid
    // fights on its own, with no Hold, and the attack order waits up to this long for the boss to be alive. 0 = the boss is there at the order.
    std::uint32_t goal_wait_ms = 0;
    // FollowRoute: the points the stand-in walks, in order (on `map`, starting from x, y, z).
    std::vector<ScenarioRoutePoint> route;
    // Orders an owner gives that a bot-only squad has nobody to give. yes_fear: the squad was told "yes fear" (Fear and Scare Beast allowed
    // in the dungeon; Scenario::MarkYesFear). focus_heal_tank: the healer was told "focus heal <tank>" (BotAI::heal_only); it heals the tank
    // and itself only. Both are taken back when the run ends, since the squad characters are reused.
    bool yes_fear = false;
    bool focus_heal_tank = false;
};

// The party every dungeon scenario uses: the shape of the owner's own (tank, healer, mage, rogue) plus a hunter for the fifth. The same
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
// The two Forever dungeons enter by their server-side area triggers (world_safe_locs 1104100 / 1104102). In a dungeon with several wings
// or entrances each wing gets its own row from the entrance nearest its first fixed-spawn boss: a boss that flees (Pusillin), a rare on a
// pool (Skul, Hearthsinger Forresten), a summon (Timmy, the Ramstein waves) or one hidden until an event (the Sunken Temple dragons wait
// for the six trolls) is never the goal. Upper Blackrock Spire is not here: its door needs the Seal of Ascension, which no bot carries.
inline constexpr DungeonRow kDungeons[] = {
    { "rfc_oggleflint",       "Ragefire Chasm: level 14 party to Oggleflint",              389,     0.797643f,   -8.23429f,  -15.5288f, 270.f, 14, 11517, "Oggleflint",          15 },
    { "hot_faldrim",          "Hall of Thanes: level 16 party to Faldrim Anvilmar",        3065,   -95.62f,     -0.16f,    510.94f,     0.f, 16, 261306, "Faldrim Anvilmar",  20 },
    { "rol_witherfang",       "Ruins of Lordaeron: level 18 party to Witherfang",          2999,  1804.15f,   149.174f,    70.399f,  266.f, 18, 250483, "Witherfang",        20 },
    { "wc_cobrahn",           "Wailing Caverns: level 18 party to Lord Cobrahn",           43,   -158.441f,   131.601f,  -74.2552f, 335.f, 18,  3669, "Lord Cobrahn",       25 },
    { "deadmines_rhahkzor",   "Deadmines: level 21 party to Rhahk'Zor",                    36,    -14.5732f, -385.475f,   62.4561f,  90.f, 21,   644, "Rhahk'Zor",          20 },
    { "sfk_rethilgore",       "Shadowfang Keep: level 24 party to Rethilgore",             33,   -228.191f,  2111.41f,    76.8904f,  70.f, 24,  3914, "Rethilgore",         15 },
    { "bfd_ghamoora",         "Blackfathom Deeps: level 26 party to Ghamoo-ra",            48,   -150.234f,   106.594f,  -39.779f,  255.f, 26,  4887, "Ghamoo-ra",          20 },
    { "stockade_targorr",     "Stormwind Stockade: level 27 party to Targorr the Dread",   34,     48.9849f,    0.483882f, -16.3942f, 360.f, 27,  1696, "Targorr the Dread",  15 },
    // Excavation Site: Wetlands (Forever, map 2998): the entrance landing (world_safe_locs 1104104) to Saltspine, the nearest of its three bosses.
    { "exc_saltspine",        "Excavation Site: Wetlands: level 28 party to Saltspine",   2998, -3698.41f, -2355.64f, 165.531f, 296.f, 28, 260322, "Saltspine",          20 },
    { "smgraveyard_vishas",   "Scarlet Monastery Graveyard: level 30 party to Vishas",     189,  1687.27f,   1050.09f,    18.6773f,  90.f, 30,  3983, "Interrogator Vishas",15 },
    { "rfk_aggem",            "Razorfen Kraul: level 31 party to Aggem Thorncurse",        47,   1942.27f,   1544.23f,    83.3055f,  75.f, 31,  4424, "Aggem Thorncurse",   20 },
    { "gnomeregan_electrocutioner", "Gnomeregan: level 32 party to Electrocutioner 6000", 90, -329.098f, -3.20722f, -152.851f, 170.f, 32, 6235, "Electrocutioner 6000", 30 },
    // City of Dalaran (Forever, map 2959): the Sewer Grate lands the party at the sewer start next to Rexxie Copperclutch
    // (classic_instance_city_of_dalaran.cpp CodSewerLanding); the two sewer bosses first, the city opens after both.
    { "cod_atrexis",          "City of Dalaran: level 33 party to Atrexis the Grave Knight", 2959, 309.8f, 384.0f, 67.5f, 223.f, 33, 247126, "Atrexis the Grave Knight", 25 },
    { "cod_lyn",              "City of Dalaran: level 33 party to Lyn the Ignored", 2959, 309.8f, 384.0f, 67.5f, 223.f, 33, 247032, "Lyn the Ignored", 25 },
    { "smlibrary_loksey",     "Scarlet Monastery Library: level 33 party to Houndmaster Loksey", 189, 253.672f, -206.624f, 18.6773f, 270.f, 33, 3974, "Houndmaster Loksey", 20 },
    // The same run at level 36, the Library's own level: a failure at 33 that passes here is the low end of the level range, not a bot bug.
    { "smlibrary_loksey36",   "Scarlet Monastery Library: level 36 party to Houndmaster Loksey", 189, 253.672f, -206.624f, 18.6773f, 270.f, 36, 3974, "Houndmaster Loksey", 20 },
    { "smarmory_herod",       "Scarlet Monastery Armory: level 36 party to Herod",         189,  1608.1f,   -318.919f,   18.6714f, 270.f, 36,  3975, "Herod",              25 },
    { "smcathedral_mograine", "Scarlet Monastery Cathedral: level 38 party to Mograine",   189,   853.179f, 1319.18f,    18.6714f,  90.f, 38,  3976, "Scarlet Commander Mograine", 25 },
    // The Drowned City (Forever, map 2970; the spawns are the live kill spots of 2026_11_19_67): the cave landing (world_safe_locs 1104120)
    // to the first boss up the climb.
    { "dc_zulalai",           "The Drowned City: level 38 party to Zul'Alai",            2970, -13296.7f, 2232.3f, -47.5f, 91.f, 38, 258905, "Zul'Alai",           25 },
    { "rfd_glutton",          "Razorfen Downs: level 40 party to Glutton",                 129,  2593.68f,   1111.23f,    50.9518f, 270.f, 40,  8567, "Glutton",            25 },
    // Krol'dok Stronghold (Forever, map 3039; the trash and its spots are the live ones of 2026_11_19_66): the cave landing (world_safe_locs
    // 1104122) to the first boss on the walk up.
    { "kroldok_grox",         "Krol'dok Stronghold: level 43 party to Slavecrusher Grox", 3039, -7659.97f, -3713.1f, 157.51f, 18.f, 43, 258966, "Slavecrusher Grox", 25 },
    { "uldaman_revelosh",     "Uldaman: level 44 party to Revelosh",                       70,   -228.859f,    46.1018f,  -46.0186f,  90.f, 44,  6910, "Revelosh",           20 },
    { "maraudon_noxxion",     "Maraudon (orange): level 46 party to Noxxion",              349,  1016.83f,  -458.52f,   -43.4737f, 360.f, 46, 13282, "Noxxion",            25 },
    { "zf_antusul",           "Zul'Farrak: level 48 party to Antu'sul",                    209,  1212.67f,    842.04f,      8.93346f, 360.f, 48,  8127, "Antu'sul",           25 },
    { "st_mijan",             "Sunken Temple: level 50 party to Mijan",                    109,  -315.903f,  100.197f, -131.849f,  180.f, 50,  5717, "Mijan",              25 },
    { "brd_roccor",           "Blackrock Depths: level 54 party to Lord Roccor",           230,   456.929f,    34.0923f,  -68.0896f, 270.f, 54,  9025, "Lord Roccor",        20 },
    { "lbrs_omokk",           "Lower Blackrock Spire: level 56 party to Highlord Omokk",   229,    78.3534f, -226.841f,   49.7662f, 270.f, 56,  9196, "Highlord Omokk",     25 },
    { "dmeast_zevrim",        "Dire Maul East: level 56 party to Zevrim Thornhoof",        429,  -203.166f, -322.997f,   -2.72467f, 270.f, 56, 11490, "Zevrim Thornhoof",   25 },
    { "dmwest_tendris",       "Dire Maul West: level 57 party to Tendris Warpwood",        429,    33.1083f,  158.977f,  -3.47126f,  45.f, 57, 11489, "Tendris Warpwood",   25 },
    { "dmnorth_moldar",       "Dire Maul North: level 57 party to Guard Mol'dar",          429,   254.92f,   -19.463f,   -2.5596f,  315.f, 57, 14326, "Guard Mol'dar",      20 },
    { "scholo_jandice",       "Scholomance: level 58 party to Jandice Barov",              289,   190.819f,   126.329f,  137.227f,  360.f, 58, 10503, "Jandice Barov",      25 },
    { "stratlive_galford",    "Stratholme (main gate): level 58 party to Archivist Galford", 329, 3392.92f, -3395.03f, 143.135f,  90.f, 58, 10811, "Archivist Galford",  30 },
    { "stratud_barthilas",    "Stratholme (service gate): level 58 party to Magistrate Barthilas", 329, 3590.87f, -3643.22f, 138.491f, 315.f, 58, 10435, "Magistrate Barthilas", 20 },
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

// The Four Horsemen take eight tanks, as a Classic raid brings: each horseman's tank hands him over at three marks to one that does not
// carry his mark (RaidRules::tankSwap), and with four tanks there is never a free one, so each tank stacked its own horseman's mark (and
// its neighbour's) until it died. Four of the melee DPS (warriors and the Retribution paladin) stay home; the rest is the Molten Core raid.
inline constexpr RaidRow kHorsemenRaid[] = {
    { "Mctankw",  ScnRace::Human,    ScnClass::Warrior, ScenarioRoleKind::Tank,   6 },
    { "Mctankd",  ScnRace::NightElf, ScnClass::Druid,   ScenarioRoleKind::Tank,   2 },
    { "Mcheals",  ScnRace::Human,    ScnClass::Priest,  ScenarioRoleKind::Healer, 5 },
    { "Mchealp",  ScnRace::Human,    ScnClass::Paladin, ScenarioRoleKind::Healer, 4 },
    { "Mchealdr", ScnRace::NightElf, ScnClass::Druid,   ScenarioRoleKind::Healer, 3 },
    { "Mcmage",   ScnRace::Human,    ScnClass::Mage,    ScenarioRoleKind::Dps,    5 },
    { "Mclock",   ScnRace::Human,    ScnClass::Warlock, ScenarioRoleKind::Dps,    5 },
    { "Mcrogue",  ScnRace::Human,    ScnClass::Rogue,   ScenarioRoleKind::Dps,    4 },
    { "Mchunt",   ScnRace::Dwarf,    ScnClass::Hunter,  ScenarioRoleKind::Dps,    4 },
    { "Mcshad",   ScnRace::Human,    ScnClass::Priest,  ScenarioRoleKind::Dps,    2 },
};

// Loatheb's Corrupted Mind leaves each healer one heal a minute, so a Classic raid brought 14-16 healers for him. With the Molten Core
// raid's 12 healers (one heal every 5 s at best, about 600 healing a second) his melee (5491-7280 every 1.48 s, vanilla data) killed the
// tanks one after another (naxx_loatheb af38-af40, 1119). Two more holy priests and a restoration druid take the places of the two shadow
// priests and a warlock: 4 tanks, 15 healers, 21 DPS.
inline constexpr RaidRow kLoathebRaid[] = {
    { "Mctankw",  ScnRace::Human,    ScnClass::Warrior, ScenarioRoleKind::Tank,   3 },
    { "Mctankd",  ScnRace::NightElf, ScnClass::Druid,   ScenarioRoleKind::Tank,   1 },
    { "Mcheals",  ScnRace::Human,    ScnClass::Priest,  ScenarioRoleKind::Healer, 7 },
    { "Mchealp",  ScnRace::Human,    ScnClass::Paladin, ScenarioRoleKind::Healer, 4 },
    { "Mchealdr", ScnRace::NightElf, ScnClass::Druid,   ScenarioRoleKind::Healer, 4 },
    { "Mcmage",   ScnRace::Human,    ScnClass::Mage,    ScenarioRoleKind::Dps,    5 },
    { "Mclock",   ScnRace::Human,    ScnClass::Warlock, ScenarioRoleKind::Dps,    4 },
    { "Mcrogue",  ScnRace::Human,    ScnClass::Rogue,   ScenarioRoleKind::Dps,    4 },
    { "Mchunt",   ScnRace::Dwarf,    ScnClass::Hunter,  ScenarioRoleKind::Dps,    4 },
    { "Mcwarr",   ScnRace::Dwarf,    ScnClass::Warrior, ScenarioRoleKind::Dps,    3 },
    { "Mcret",    ScnRace::Human,    ScnClass::Paladin, ScenarioRoleKind::Dps,    1 },
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
    float lair_x = 0.f, lair_y = 0.f, lair_z = 0.f, lair_o = 0.f;   // a boss a script summons (no creature row): where; see ScenarioDef::lair_x
    float arrive_min_yards = 0.f;       // see ScenarioDef::arrive_min_yards
    std::uint32_t goal_adds_a = 0, goal_adds_b = 0, goal_adds_needed = 0;   // see ScenarioDef::goal_adds_needed
    std::uint32_t spare_entry = 0;      // see ScenarioDef::spare_entry
    std::uint32_t keep_entry_b = 0, keep_entry_c = 0;   // two more entries staging leaves alive (a trio, the other Horsemen); see ScenarioDef::keep_entry
    std::uint8_t resist_school = 0;     // see ScenarioDef::resist_school
    std::uint32_t wear_entry = 0;       // see ScenarioDef::wear_entry
    std::uint32_t start_object = 0;     // see ScenarioDef::start_object
    std::uint32_t start_event = 0;      // see ScenarioDef::start_event
    std::uint32_t summoner_entry = 0;   // see ScenarioDef::summoner_entry
    std::uint32_t summoner_spell = 0;
    std::uint32_t start_add = 0;        // see ScenarioDef::start_add
    float clear_formation_beyond = 0.f; // see ScenarioDef::clear_formation_beyond
    float stage_x = 0.f, stage_y = 0.f, stage_z = 0.f;   // a listed landing spot instead of the ring search; see ScenarioDef::stage_x
    float route_x = 0.f, route_y = 0.f, route_z = 0.f;   // see ScenarioDef::route_x
    // The instance data of the bosses a raid kills before this one (SetData type, value), set at the attack: see ScenarioDef::start_instance_data.
    std::array<std::pair<std::uint32_t, std::uint32_t>, 8> start_data{};
    std::uint8_t start_data_n = 0;
    // The way in a raid opens before this boss (SetData type, value, set before the landing, and a door it opens): see
    // ScenarioDef::entry_instance_data.
    std::array<std::pair<std::uint32_t, std::uint32_t>, 4> entry_data{};
    std::uint8_t entry_data_n = 0;
    std::uint32_t entry_door = 0;
    bool open_each = false;             // see ScenarioDef::open_each
};

// A row whose fight keeps two or three kinds of creature alive (the bug trio, the Four Horsemen): the extra ones, without spelling out
// every field in between.
constexpr RaidBossRow Keeps(RaidBossRow r, std::uint32_t b, std::uint32_t c = 0)
{
    r.keep_entry_b = b;
    r.keep_entry_c = c;
    return r;
}

// A row whose boss sees players from further than the core's aggro reach (under 20 y): his script's MoveInLineOfSight attacks, or pulls
// the room, within a distance of its own, so the raid lands <yards> away, that distance plus 8 for the squad's spread round the landing
// spot. The 124 runs of bwl_broodlord (classic_boss_broodlord_lashlayer.cpp: 40 y in sight) and aq40_huhuran (classic_boss_huhuran.cpp:
// 80 y, no sight needed) both began in the landing second on a shadow priest, the tanks 39 y from the boss; the vanilla scripts' values
// are in the instances' classic_boss_*.cpp.
constexpr RaidBossRow Sight(RaidBossRow r, float yards)
{
    r.arrive_min_yards = yards;
    return r;
}

// A row whose fight a vanilla raid dressed resistance gear for: the school (ScnSchool) every raider wears in each armour and jewellery slot
// that has a piece with it under the tier's item level (ScenarioDef::resist_school).
constexpr RaidBossRow Resist(RaidBossRow r, std::uint8_t school)
{
    r.resist_school = school;
    return r;
}

// A row whose fight needs one piece on every raider whatever it scores (ScnItem; ScenarioDef::wear_entry): the Onyxia Scale Cloak, without
// which Shadow Flame puts its 22682 burn on the target (SpellEffects.cpp: 22539 checks aura 22683).
constexpr RaidBossRow Wears(RaidBossRow r, std::uint32_t entry)
{
    r.wear_entry = entry;
    return r;
}

// A row whose fight starts when the raid leader uses a game object (ScenarioDef::start_object): Chromaggus' lever.
constexpr RaidBossRow StartsWith(RaidBossRow r, std::uint32_t object_entry)
{
    r.start_object = object_entry;
    return r;
}

// A row whose boss comes out on a game event the raid leader sends (ScenarioDef::start_event): Gahz'ranka's Mudskunk Lure.
constexpr RaidBossRow StartsOnEvent(RaidBossRow r, std::uint32_t event_id)
{
    r.start_event = event_id;
    return r;
}

// A row whose boss a creature of the room summons when a player comes near (ScenarioDef::summoner_entry): Ouro's Spawner.
constexpr RaidBossRow SummonedBy(RaidBossRow r, std::uint32_t summoner_entry, std::uint32_t summoner_spell)
{
    r.summoner_entry = summoner_entry;
    r.summoner_spell = summoner_spell;
    return r;
}

// A row whose fight the raid opens on one of the boss's adds (ScenarioDef::start_add): Mandokir's Vilebranch Speaker.
constexpr RaidBossRow OpensOn(RaidBossRow r, std::uint32_t add_entry)
{
    r.start_add = add_entry;
    return r;
}

// A row whose boss's formation in the world DB holds trash as well as his adds (ScenarioDef::clear_formation_beyond).
constexpr RaidBossRow ClearsFormation(RaidBossRow r, float beyond_yards)
{
    r.clear_formation_beyond = beyond_yards;
    return r;
}

// A row whose room the ring search cannot land the raid in (every spot in the boss's sight is on his walk, or has no floor): the raid lands
// on the listed spot, where its encounter rules fight him (RaidRules stand / tank posts).
constexpr RaidBossRow Staged(RaidBossRow r, float x, float y, float z)
{
    r.stage_x = x; r.stage_y = y; r.stage_z = z;
    return r;
}

// A staged row whose boss no raider can walk to before his fight (ScenarioDef::route_x): the landing's route is checked to this point.
constexpr RaidBossRow RoutesTo(RaidBossRow r, float x, float y, float z)
{
    r.route_x = x; r.route_y = y; r.route_z = z;
    return r;
}

// A row whose boss a raid fights after others, with fight parts that only the living ones bring (Hakkar's five Aspects and his Power
// stacks, one per High Priest alive): the instance data the runner sets at the attack (ScenarioDef::start_instance_data), in order.
constexpr RaidBossRow After(RaidBossRow r, std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> data)
{
    for (auto const& d : data)
        if (r.start_data_n < r.start_data.size())
            r.start_data[r.start_data_n++] = d;
    return r;
}

// A row whose way in a raid opens on its way there (ScenarioDef::entry_instance_data): the earlier bosses' deaths, set before the landing,
// and the door then opened. Thaddius: Gluth's death unlocks his door.
constexpr RaidBossRow ComesThrough(RaidBossRow r, std::uint32_t door_entry, std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> data)
{
    r.entry_door = door_entry;
    for (auto const& d : data)
        if (r.entry_data_n < r.entry_data.size())
            r.entry_data[r.entry_data_n++] = d;
    return r;
}

// A row whose boss and kept companions are each opened by a tank of its own at the order (ScenarioDef::open_each): the Four Horsemen.
constexpr RaidBossRow OpensEach(RaidBossRow r)
{
    r.open_each = true;
    return r;
}

// In the order of the raid. Each starts at the boss's room with the encounter pre-spawned (the earlier bosses alive too).
inline constexpr RaidBossRow kMcBosses[] = {
    { "mc_garr",      "Molten Core: 40 level 60 raiders to Garr",      12057, "Garr",      15, 12099, 0, 0,
      0.f, 0.f, 0.f, 0.f, 0.f, 0, 0, 0, 12099 },   // 12099 Firesworn: kept alive at staging, and none may die before Garr
    { "mc_lucifron",  "Molten Core: 40 level 60 raiders to Lucifron",  12118, "Lucifron",  15, 12119, 0, 1 },   // Flamewaker Protector; Lucifron's Curse must be removed
    { "mc_magmadar",  "Molten Core: 40 level 60 raiders to Magmadar",  11982, "Magmadar",  15, 0, 1 },          // Frenzy must be tranquilized
    { "mc_shazzrah",  "Molten Core: 40 level 60 raiders to Shazzrah",  12264, "Shazzrah",  15, 0, 0, 1 },       // Shazzrah's Curse must be removed; the Gate drops him on a random raider
    { "mc_gehennas",  "Molten Core: 40 level 60 raiders to Gehennas",  12259, "Gehennas",  15, 11661, 0, 1 },   // Flamewaker; Gehennas' Curse must be removed
    { "mc_sulfuron",  "Molten Core: 40 level 60 raiders to Sulfuron Harbinger", 12098, "Sulfuron Harbinger", 15, 11662 },   // Flamewaker Priests: Dark Mending is interrupted, Inspire purged, Shadow Word: Pain kept off
    { "mc_golemagg",  "Molten Core: 40 level 60 raiders to Golemagg the Incinerator", 11988, "Golemagg the Incinerator", 15, 11672 },   // Core Ragers: the off tank holds them away from Golemagg; Magma Splash stacks on the main tank
    // Majordomo does not die: the fight ends when his eight Flamewakers (11664 elite, 11663 healer) are dead and he yields. Magic Reflection and Damage
    // Shield on the adds, Aegis of Ragnaros on him.
    { "mc_majordomo", "Molten Core: 40 level 60 raiders to Majordomo Executus", 12018, "Majordomo Executus", 15, 0, 0, 0, 758.089f, -1176.71f, -118.640f, 3.12414f, 0.f, 11664, 11663, 8 },   // no creature row: the instance script summons him in his hall (classic_instance_molten_core.cpp, SummonCreature 758.089,-1176.71,-118.640); the runner summons him there
    { "mc_geddon",    "Molten Core: 40 level 60 raiders to Baron Geddon", 12056, "Baron Geddon", 15 },   // Inferno (19698, 20 y): the raid must leave his range
    { "mc_ragnaros",  "Molten Core: 40 level 60 raiders to Ragnaros",  11502, "Ragnaros",  20, 0, 0, 0,
      838.308f, -831.466f, -232.185f, 2.19911f },   // no creature row: summoned at his lair (classic_boss_majordomo_executus.cpp:342). Elemental Fire on the tank, Wrath threat reset and knockback, Magma Blast out of melee reach, submerge at 3 min (Sons of Flame)
};

// Where a 40-man raid scenario arrives: the instance and its entrance areatrigger target.
struct RaidEntrance { std::uint32_t map; float x, y, z, o_deg; };

inline constexpr RaidEntrance kMcEntrance  = { 409, 1091.89f, -466.985f, -105.084f, 180.f };   // areatrigger_teleport 1102886
inline constexpr RaidEntrance kBwlEntrance = { 469, -7673.2f, -1106.2f, 396.65f, 200.f };   // areatrigger_teleport 1103726 (vanilla entrance target)
inline constexpr RaidEntrance kOnyEntrance = { 249, 30.8916f, -54.079f, -5.02784f, 270.f };    // areatrigger_teleport 1102848
inline constexpr RaidEntrance kZgEntrance   = { 309, -11916.6f, -1243.52f, 92.5338f, 270.f };  // areatrigger_teleport 1103928
inline constexpr RaidEntrance kAq20Entrance = { 509, -8436.53f, 1519.17f, 31.907f, 150.f };    // areatrigger_teleport 1104008
inline constexpr RaidEntrance kAq40Entrance = { 531, -8221.35f, 2014.34f, 129.071f, 50.f };    // areatrigger_teleport 1104010
inline constexpr RaidEntrance kNaxxEntrance = { 533, 3005.87f, -3435.01f, 293.882f, 0.f };     // areatrigger_teleport 1104055

inline ScenarioDef MakeRaidScenario(RaidBossRow const& r, RaidEntrance const& e, std::vector<ScenarioMember> squad)
{
    ScenarioDef d{};
    d.name = r.name;
    d.summary = r.summary;
    d.level = 60;
    d.squad = std::move(squad);
    d.map = e.map;
    d.x = e.x; d.y = e.y; d.z = e.z;
    d.o = e.o_deg * 3.14159265f / 180.f;
    d.goal = ScenarioGoalKind::KillCreature;
    d.goal_entry = r.boss_entry;
    d.goal_name = r.boss_name;
    d.timeout_ms = r.timeout_min * 60u * 1000u;
    d.limits.expected_map = e.map;
    d.limits.bounds = { true, d.x - 800.f, d.x + 800.f, d.y - 800.f, d.y + 800.f, d.z - 250.f, d.z + 250.f };
    d.limits.expected_group = std::uint32_t(d.squad.size());
    d.limits.kill_is_pass = true;                                    // playtest 2026-10-14: a boss kill passes, however many die
    d.limits.tolerated_deaths = 0;                                   // every death is a warning in the result
    d.limits.max_deaths = 0;                                         // and none ends the run: only a full wipe, a reset, a stall or the timeout does
    d.limits.dps_hold_ms = 20000;
    d.limits.tanks_dead_is_wipe = false;
    d.limits.min_tranq_shots = r.min_tranq_shots;
    d.limits.min_decurses = r.min_decurses;
    d.limits.stray_yards = 120.f;                                    // a raid spreads over a room
    d.arrive_near_entry = r.boss_entry;
    d.keep_entry = r.keep_entry;
    d.keep_entry_b = r.keep_entry_b;
    d.keep_entry_c = r.keep_entry_c;
    d.goal_adds_a = r.goal_adds_a; d.goal_adds_b = r.goal_adds_b; d.goal_adds_needed = r.goal_adds_needed;
    d.spare_entry = r.spare_entry;
    d.lair_x = r.lair_x; d.lair_y = r.lair_y; d.lair_z = r.lair_z; d.lair_o = r.lair_o;
    d.arrive_min_yards = r.arrive_min_yards;
    d.resist_school = r.resist_school;
    d.wear_entry = r.wear_entry;
    d.start_object = r.start_object;
    d.start_event = r.start_event;
    d.summoner_entry = r.summoner_entry;
    d.summoner_spell = r.summoner_spell;
    d.start_add = r.start_add;
    d.clear_formation_beyond = r.clear_formation_beyond;
    d.stage_x = r.stage_x; d.stage_y = r.stage_y; d.stage_z = r.stage_z;
    d.route_x = r.route_x; d.route_y = r.route_y; d.route_z = r.route_z;
    d.start_instance_data.assign(r.start_data.begin(), r.start_data.begin() + r.start_data_n);
    d.entry_instance_data.assign(r.entry_data.begin(), r.entry_data.begin() + r.entry_data_n);
    d.entry_door = r.entry_door;
    d.open_each = r.open_each;
    d.order_attack = true;
    d.order_delay_ms = 20000;
    return d;
}

inline ScenarioDef MakeMcScenario(RaidBossRow const& r)
{
    return MakeRaidScenario(r, kMcEntrance, RaidSquad(kMcRaid, sizeof(kMcRaid) / sizeof(kMcRaid[0])));
}

// ---- Onyxia's Lair: the whole fight, 40 level 60 raiders ----
// Phase 1 (100-65 %): tank her with her back to the raid, melee behind and on the flanks, Wing Buffet / Knock Away on the tank, Flame Breath
// and Tail Sweep cones left empty. Phase 2 (65-40 %): she flies, Fireball spreads the ranged, 5-7 whelps per wave to the tanks and AoE, and a
// Deep Breath along one of eight lines (the 5 s wind-up is the only warning: the whole raid must leave the corridor). Phase 3 (< 40 %): she
// lands, Bellowing Roar fear every 15-30 s (a Dwarf priest's Fear Ward on the main tank), a whelp every 1-10 s, melee again from behind.
// The squad is the MC raid with one Dwarf priest healer for Fear Ward. A boss kill passes the run however many die; a Deep Breath that finds the
// raid standing on the line (the 2026-10-13 wipe: 33 dead in one second) is a wipe and fails it. Read the run's [raid_breath] lines (corridor latched
// at the wind-up), [raid_death] and the phase hp in [raid_fight].
inline constexpr RaidRow kOnyRaid[] = {
    { "Mctankw",  ScnRace::Human,    ScnClass::Warrior, ScenarioRoleKind::Tank,   3 },
    { "Mctankd",  ScnRace::NightElf, ScnClass::Druid,   ScenarioRoleKind::Tank,   1 },
    { "Mcheals",  ScnRace::Human,    ScnClass::Priest,  ScenarioRoleKind::Healer, 4 },
    { "Mchealsd", ScnRace::Dwarf,    ScnClass::Priest,  ScenarioRoleKind::Healer, 1 },   // Fear Ward (Dwarf racial)
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

inline ScenarioDef MakeOnyScenario()
{
    static constexpr RaidBossRow kOny = { "onyxia_full", "Onyxia's Lair: 40 level 60 raiders, the whole fight (phase 1 tanking, phase 2 Deep Breath and whelps, phase 3 fear)",
                                          10184, "Onyxia", 20, 0, 0, 0, 0.f, 0.f, 0.f, 0.f, 68.f };   // Onyxia aggroes at 58 y: land outside it, the tanks pull
    return MakeRaidScenario(kOny, kOnyEntrance, RaidSquad(kOnyRaid, sizeof(kOnyRaid) / sizeof(kOnyRaid[0])));
}

// The same fight judged on the dodge. onyxia_full passes on the kill however many die, which let a live fight lose 38 of 40 (25 to Deep Breath, 11 to
// Heated Ground, three waves) with the boss alive and say nothing. Here the fourth death fails the run and the sixth ends it: the raid
// stands off the line of the waypoint she hovers at before the cast, and out of the Heated Ground the pass leaves. Read the run's [raid_breath]
// lines (the corridor opens on the cast), [raid_death] (spell, position) and [raid_hazard] (Heated Ground exits).
inline ScenarioDef MakeOnyDodgeScenario()
{
    ScenarioDef d = MakeOnyScenario();
    d.name = "onyxia_dodge";
    d.summary = "Onyxia's Lair: the whole fight with three deaths allowed, so a Deep Breath or Heated Ground that kills the raid fails the run";
    d.limits.kill_is_pass = false;
    d.limits.tolerated_deaths = 3;
    d.limits.max_deaths = 6;
    return d;
}

// ---- Blackwing Lair, Razorgore the Untamed: the orb fight, 40 level 60 raiders ----
// Phase 1: nobody attacks Razorgore while a Black Dragon Egg stands (he dying early kills the raid). A DPS bot walks to the Orb of Domination
// (GO 177808) and uses it, which engages and possesses Razorgore; the controller sends him egg to egg (Destroy Egg), Mind Exhaustion hands the
// orb to the next bot. Phase 2: the eggs gone, the tanks hold him and the raid kills him. Pass = Razorgore killed. Read the run's [raid_fight]
// lines and the server's classic.bwl.orb lines (who clicked, the Possess cast result, any refusal and why). Grethok stays alive (his guards
// are the pull).
inline ScenarioDef MakeBwlRazorgoreScenario()
{
    static constexpr RaidBossRow kRaz = { "bwl_razorgore", "Blackwing Lair: 40 level 60 raiders to Razorgore the Untamed (a bot takes the Orb of Domination, the eggs, then the kill)",
                                          12435, "Razorgore the Untamed", 20, 12557 };   // 12557 Grethok the Controller
    return MakeRaidScenario(kRaz, kBwlEntrance, RaidSquad(kMcRaid, sizeof(kMcRaid) / sizeof(kMcRaid[0])));
}

// ---- Blackwing Lair, Vaelastrasz the Corrupt: the fight his gossip starts, 40 level 60 raiders ----
// He lies friendly at 30% health until a player talks him through his two gossip options (classic_boss_vaelastrasz.cpp), which needs
// Razorgore dead and the room entered (the area trigger runs Lord Victor Nefarius's corruption intro, about 26 s, after which he offers
// gossip). The runner sets those two instance steps (data 0 Razorgore = DONE 3, data 8 the Vaelastrasz event = DONE 3), the main tank clicks
// through, his speech runs 36 s and he attacks the tank. Burning Adrenaline on a mana user every 15 s and on the tank every 45 s, Flame
// Breath, Cleave, Fire Nova, Tail Sweep. Pass = Vaelastrasz killed. The raid wears fire resistance, as a vanilla raid did.
inline ScenarioDef MakeBwlVaelScenario()
{
    static constexpr RaidBossRow kVael = Resist({ "bwl_vaelastrasz", "Blackwing Lair: 40 level 60 raiders to Vaelastrasz the Corrupt (his gossip starts it, Burning Adrenaline, Tail Sweep)",
                                                  13020, "Vaelastrasz the Corrupt", 20 }, ScnSchool::Fire);
    ScenarioDef d = MakeRaidScenario(kVael, kBwlEntrance, RaidSquad(kMcRaid, sizeof(kMcRaid) / sizeof(kMcRaid[0])));
    d.start_npc = 13020;
    d.start_gossip_steps = 2;
    d.start_instance_data = { { 0u, 3u }, { 8u, 3u } };
    return d;
}

// ---- Blackwing Lair, Nefarian: the whole fight from Lord Victor Nefarius's start, 40 level 60 raiders ----
// The raid lands in sight of Lord Victor Nefarius (10162) on his throne. The runner starts his event as his gossip option "Attack them,
// minions!" does (its DB script calls the creature's start: DoAction 1, classic_boss_victor_nefarius.cpp). Phase 1: two drakonid colours
// every 6-7 s and a Chromatic Drakonid pair every 35 s; he sits behind his barrier casting Shadow Bolt, Fear, Silence and Shadow Command
// (a raider charmed). The raid fights it on its own, no Hold. At 40 drakonids dead Nefarian flies in and lands; the order goes onto him.
// Phase 2: Shadow Flame, Bellowing Roar, Veil of Shadow, Tail Lash, the class calls; at 20% the drakonids rise as Bone Constructs.
// Pass = Nefarian killed. Every raider wears the Onyxia Scale Cloak (Shadow Flame).
inline ScenarioDef MakeBwlNefarianScenario()
{
    static constexpr RaidBossRow kNef = Wears({ "bwl_nefarian", "Blackwing Lair: 40 level 60 raiders to Nefarian (Victor Nefarius's drakonid waves, then Nefarian, class calls, Bone Constructs)",
                                                11583, "Nefarian", 35 }, ScnItem::OnyxiaScaleCloak);
    ScenarioDef d = MakeRaidScenario(kNef, kBwlEntrance, RaidSquad(kMcRaid, sizeof(kMcRaid) / sizeof(kMcRaid[0])));
    d.arrive_near_entry = 10162;   // Nefarian has no spawn row: Lord Victor Nefarius summons him
    d.start_npc = 10162;
    d.start_npc_action = 1;        // CLASSIC_BWL_ACTION_NEFARIUS_START
    d.goal_wait_ms = 15u * 60u * 1000u;
    return d;
}

// ---- The other raids, one boss at a time: Blackwing Lair, Zul'Gurub, the Ruins and the Temple of Ahn'Qiraj, Naxxramas ----
// The same shape as the Molten Core rows: the squad enters at the instance entrance, staging kills the trash, places the raid in sight of
// the boss and orders the attack. A boss kill passes the run; every death is a warning. Only bosses with a creature row of their own
// are here (Vaelastrasz and Nefarian, whose fights a gossip starts, have their own scenarios above). Left out, with the reason: General Rajaxx (his seven waves come from Andorov's event),
// Ouro (a trigger summons him) and Sapphiron (the instance spawns him once the four wings are cleared).
// Reading a run: the [raid] lines of the bots' decisions (Logger.playerbot.v2=3), [raid_death] (spell, position) and the fight hp in [raid_fight].

// A 20-player raid for Zul'Gurub and the Ruins: 2 tanks, 6 healers, 12 DPS, the same characters as the first half of the 40 (the names repeat).
inline constexpr RaidRow kRaid20[] = {
    { "Mctankw",  ScnRace::Human,    ScnClass::Warrior, ScenarioRoleKind::Tank,   2 },
    { "Mcheals",  ScnRace::Human,    ScnClass::Priest,  ScenarioRoleKind::Healer, 3 },
    { "Mchealp",  ScnRace::Human,    ScnClass::Paladin, ScenarioRoleKind::Healer, 2 },
    { "Mchealdr", ScnRace::NightElf, ScnClass::Druid,   ScenarioRoleKind::Healer, 1 },
    { "Mcmage",   ScnRace::Human,    ScnClass::Mage,    ScenarioRoleKind::Dps,    3 },
    { "Mclock",   ScnRace::Human,    ScnClass::Warlock, ScenarioRoleKind::Dps,    2 },
    { "Mcrogue",  ScnRace::Human,    ScnClass::Rogue,   ScenarioRoleKind::Dps,    2 },
    { "Mchunt",   ScnRace::Dwarf,    ScnClass::Hunter,  ScenarioRoleKind::Dps,    2 },
    { "Mcwarr",   ScnRace::Dwarf,    ScnClass::Warrior, ScenarioRoleKind::Dps,    2 },
    { "Mcshad",   ScnRace::Human,    ScnClass::Priest,  ScenarioRoleKind::Dps,    1 },
};

// Blackwing Lair after Razorgore (bwl_razorgore above has the orb fight). Broodlord: the Suppression Devices of his room stay (game objects),
// the rogues disarm them. The three drakes: Wing Buffet throws the tank, Shadow Flame is a cone. Chromaggus: five Brood Afflictions, two breaths.
inline constexpr RaidBossRow kBwlBosses[] = {
    Sight({ "bwl_broodlord",  "Blackwing Lair: 40 level 60 raiders to Broodlord Lashlayer (Suppression Devices, Mortal Strike, Blast Wave)",
      12017, "Broodlord Lashlayer", 20 }, 48.f),   // sees 40 y
    // The three drakes and Nefarian cast Shadow Flame: every raider wears the Onyxia Scale Cloak. Firemaw's Flame Buffet is fire, stacking
    // on everyone in 100 y: the raid wore fire resistance for him (123: 36 of 38 deaths to Flame Buffet).
    Resist(Wears({ "bwl_firemaw", "Blackwing Lair: 40 level 60 raiders to Firemaw (Flame Buffet stacks, Wing Buffet, Shadow Flame cone)",
      11983, "Firemaw",             20 }, ScnItem::OnyxiaScaleCloak), ScnSchool::Fire),
    Wears({ "bwl_ebonroc",  "Blackwing Lair: 40 level 60 raiders to Ebonroc (Shadow of Ebonroc tank swap, Wing Buffet)",
      14601, "Ebonroc",             20 }, ScnItem::OnyxiaScaleCloak),
    Wears({ "bwl_flamegor", "Blackwing Lair: 40 level 60 raiders to Flamegor (Frenzy tranquilized, Wing Buffet, Shadow Flame cone)",
      11981, "Flamegor",            20 }, ScnItem::OnyxiaScaleCloak),
    // Chromaggus stays unattackable behind his side door until the lever opens it (classic_boss_chromaggus.cpp UpdateAI): 4f7b18e3 failed with
    // "the boss is not attackable" at the order. The main tank pulls the lever as a raid leader would.
    // He casts Frenzy 23128 every 15 s (classic_boss_chromaggus.cpp; Enrage dispel type 9, which Tranquilizing Shot 19801 removes): a kill
    // must have seen a shot, and every run reports how many (0.1.28g: two runs of 4 and 6 minutes logged no [raid_tranq] line at all).
    // His two breaths are rolled per instance (2 of 5; Time Lapse, the off-tank swap check of 0018, is in 8 of the 20 pairs). A forced
    // extra Time Lapse was tried (8d2fd499aa) and is not used: cast at his 60 s breath it pushed his own breath to right after it, when
    // the raid had come out of hiding, and 26 raiders died to one Incinerate. The swap is checked in the runs whose roll has it.
    StartsWith({ "bwl_chromaggus", "Blackwing Lair: 40 level 60 raiders to Chromaggus (Brood Afflictions dispelled, Time Lapse tank swap)",
      14020, "Chromaggus",          25, 0, 1 }, 179148),
};

// Zul'Gurub, 20 raiders. Venoxis' four cobras and Hakkar's Sons stand for the fight; Thekal's Zath and Lor'Khan (the three must die together)
// and Mandokir's Vilebranch Speaker (his death starts the fight; Ohgan is summoned) stay.
inline constexpr RaidBossRow kZgBosses[] = {
    { "zg_jeklik",    "Zul'Gurub: 20 level 60 raiders to High Priestess Jeklik (bats, Great Heal kicked, Pierce Armor tank swap)",
      14517, "High Priestess Jeklik", 20 },
    { "zg_venoxis",   "Zul'Gurub: 20 level 60 raiders to High Priest Venoxis (cobras, Holy Wrath spread, snake form poison cloud)",
      14507, "High Priest Venoxis",   20, 11373 },   // Razzashi Cobra
    { "zg_marli",     "Zul'Gurub: 20 level 60 raiders to High Priestess Mar'li (spawns first, Poison Volley, Corrosive Poison dispel)",
      14510, "High Priestess Mar'li", 20 },
    Keeps({ "zg_thekal", "Zul'Gurub: 20 level 60 raiders to High Priest Thekal, Zath and Lor'Khan (the three die together, then the tiger)",
      14509, "High Priest Thekal", 25, 11348 }, 11347),   // Zath, Lor'Khan
    OpensOn({ "zg_mandokir",  "Zul'Gurub: 20 level 60 raiders to Bloodlord Mandokir (the Speaker starts it, Ohgan first, Threatening Gaze)",
      11382, "Bloodlord Mandokir",    25, 11391 }, 11391),   // Vilebranch Speaker: kept alive at staging, the attack order is onto him
    // Arlokk has no creature row: the Gong of Bethekk (180526) summons her ten seconds after it is struck (classic_boss_arlokk.cpp,
    // ArlokkSummonPos; VMaNGOS event_scripts 9066). The runner summons her at that spot, as for Ragnaros. Her vanish and the Prowlers that
    // come meanwhile are the fight.
    { "zg_arlokk",    "Zul'Gurub: 20 level 60 raiders to High Priestess Arlokk (summoned as by the gong; vanish, Prowlers, Gouge retaunt)",
      14515, "High Priestess Arlokk", 20, 0, 0, 0, -11540.7f, -1627.71f, 41.27f, 0.1f },
    { "zg_jindo",     "Zul'Gurub: 20 level 60 raiders to Jin'do the Hexxer (totems first, Hex, Delusions, brain-washed raiders)",
      11380, "Jin'do the Hexxer",     20 },
    // The five High Priests are dead before Hakkar (Arlokk 1, Jeklik 2, Venoxis 3, Mar'li 4, Thekal 6: DONE), then type 0 sets the Hakkar's
    // Power stacks from them (classic_instance_zulgurub.cpp UpdateHakkarPowerStacks: one per priest alive). With the priests alive the run met
    // their Aspects (sweep-zg-0134: Hakkar's melee up to 6690, the tank dead at 33 s, the raid at 43-77 s to Aspect of Jeklik / Venoxis).
    After({ "zg_hakkar",    "Zul'Gurub: 20 level 60 raiders to Hakkar (Corrupted Blood carrier runs, Cause Insanity, Blood Siphon)",
      14834, "Hakkar",                25, 11357 }, { { 1u, 3u }, { 2u, 3u }, { 3u, 3u }, { 4u, 3u }, { 6u, 3u }, { 0u, 0u } }),   // Son of Hakkar
    StartsOnEvent({ "zg_gahzranka", "Zul'Gurub: 20 level 60 raiders to Gahz'ranka (Frost Breath cone, Massive Geyser threat reset)",
      15114, "Gahz'ranka",            15 }, 9104),   // the Mudskunk Lure's event (classic_event_summon_gahzranka)
    // The Edge of Madness: one of four bosses a lockout, the one the Brazier of Madness (180327, game events 29-32) brings out for the
    // Gurubashi Mojo Madness. None has a creature row and the instance's own summon is switched off (classic_instance_zulgurub.cpp
    // SpawnRandomBoss, as in VMaNGOS), so each run summons its boss at the spot that summon used, in front of the brazier, as for Arlokk.
    // One row a boss: a lockout has one, a test can have any.
    { "zg_grilek",    "Zul'Gurub: 20 level 60 raiders to Gri'lek of the Iron Blood (Avatar, the chase, roots)",
      15082, "Gri'lek",               20, 0, 0, 0, -11901.45f, -1906.337f, 65.37f, 0.86f },
    { "zg_hazzarah",  "Zul'Gurub: 20 level 60 raiders to Hazza'rah the Dreamweaver (Nightmare Illusions first, mana burn, sleep)",
      15083, "Hazza'rah",             20, 0, 0, 0, -11901.45f, -1906.337f, 65.37f, 0.86f },
    { "zg_renataki",  "Zul'Gurub: 20 level 60 raiders to Renataki of the Thousand Blades (vanish, Ambush, Thousand Blades, threat resets)",
      15084, "Renataki",              20, 0, 0, 0, -11901.45f, -1906.337f, 65.37f, 0.86f },
    { "zg_wushoolay", "Zul'Gurub: 20 level 60 raiders to Wushoolay the Storm Witch (Chain Lightning, Forked Lightning, spread)",
      15085, "Wushoolay",             20, 0, 0, 0, -11901.45f, -1906.337f, 65.37f, 0.86f },
};

// Ruins of Ahn'Qiraj, 20 raiders. Buru's eggs stay (he must be led over them).
inline constexpr RaidBossRow kAq20Bosses[] = {
    { "aq20_kurinnaxx", "Ruins of Ahn'Qiraj: 20 level 60 raiders to Kurinnaxx (Mortal Wound tank swap, Sand Traps, Wide Slash cone)",
      15348, "Kurinnaxx",              20 },
    { "aq20_moam",      "Ruins of Ahn'Qiraj: 20 level 60 raiders to Moam (Mana Fiends first, Arcane Eruption, drains kicked)",
      15340, "Moam",                   20 },
    { "aq20_buru",      "Ruins of Ahn'Qiraj: 20 level 60 raiders to Buru the Gorger (the chase, eggs, hatchlings, the 20% transform)",
      15370, "Buru the Gorger",        25, 15514 },   // Buru Egg
    { "aq20_ayamiss",   "Ruins of Ahn'Qiraj: 20 level 60 raiders to Ayamiss the Hunter (larva first, swarmers, Poison Stinger)",
      15369, "Ayamiss the Hunter",     20 },
    { "aq20_ossirian",  "Ruins of Ahn'Qiraj: 20 level 60 raiders to Ossirian the Unscarred (Curse of Tongues, tornados, War Stomp)",
      15339, "Ossirian the Unscarred", 25 },
};

// Temple of Ahn'Qiraj, 40 raiders. The bug trio dies Yauj, Kri, Vem (the goal is the last); Sartura's Royal Guards and Vek'lor stand with
// their boss; C'Thun's eye and tentacles are summoned by the instance when the raid reaches him.
inline constexpr RaidBossRow kAq40Bosses[] = {
    Sight({ "aq40_skeram",   "Temple of Ahn'Qiraj: 40 level 60 raiders to The Prophet Skeram (True Fulfillment, Arcane Explosion kicked, images)",
      15263, "The Prophet Skeram",     20 }, 36.f),   // sees 28 y
    Sight(Keeps({ "aq40_bugtrio", "Temple of Ahn'Qiraj: 40 level 60 raiders to the bug trio, Yauj then Kri then Vem (Great Heal kicked, Toxic Vapors)",
      15544, "Vem", 25, 15543 }, 15511), 68.f),   // Princess Yauj, Lord Kri; they see 60 y
    Sight({ "aq40_sartura",  "Temple of Ahn'Qiraj: 40 level 60 raiders to Battleguard Sartura (guards first, Whirlwinds left)",
      15516, "Battleguard Sartura",    20, 15984 }, 93.f),   // Sartura's Royal Guard; she sees 85 y
    Sight({ "aq40_fankriss", "Temple of Ahn'Qiraj: 40 level 60 raiders to Fankriss the Unyielding (Mortal Wound tank swap, spawns, hatchlings)",
      15510, "Fankriss the Unyielding", 20 }, 108.f),   // sees 100 y
    // Viscidus's Poison Shock and Poison Bolt Volley are nature: the other nature resistance fight of the temple.
    // He takes anyone within 95 y (no line of sight needed), and his whole room is nearer than that: the ring search's spots 99-115 y out all
    // lay beyond its walls (aq40_viscidus af52 sweep 1: 160 spots, none with floor). The raid waits on the ledge at the room's north end,
    // where the ramp from the tunnel comes in, 110 y from him (vmaps 0531: floor -25.3), a straight 110 y walk down to him.
    Staged(Resist(Sight({ "aq40_viscidus", "Temple of Ahn'Qiraj: 40 level 60 raiders to Viscidus (frost to freeze, melee to shatter, globs, toxin clouds)",
      15299, "Viscidus",               25 }, 103.f), ScnSchool::Nature), -7990.0f, 1035.0f, -25.3f),   // sees 95 y
    Resist(Sight({ "aq40_huhuran", "Temple of Ahn'Qiraj: 40 level 60 raiders to Princess Huhuran (Frenzy tranquilized, Noxious Poison, the 30% volley)",
      15509, "Princess Huhuran",       20 }, 88.f), ScnSchool::Nature),   // sees 80 y, no line of sight needed; worn in nature resistance
    { "aq40_twins",    "Temple of Ahn'Qiraj: 40 level 60 raiders to the Twin Emperors (physical on Vek'nilash, casters on Vek'lor, teleports)",
      15275, "Emperor Vek'nilash",   25, 15276 },   // Emperor Vek'lor
    // Ouro has no spawn row: his Spawner (15957) summons him (26061) when a player comes within 25 y, and he comes up in combat with the raid.
    SummonedBy({ "aq40_ouro", "Temple of Ahn'Qiraj: 40 level 60 raiders to Ouro (Sweep, Sand Blast on the tank, submerge and the dirt mounds, scarabs, Berserk)",
      15517, "Ouro",                   20 }, 15957, 26061),
    { "aq40_cthun",    "Temple of Ahn'Qiraj: 40 level 60 raiders to C'Thun (the eye and Dark Glare, tentacles, the stomach)",
      15727, "C'Thun",                 30 },
};

// Naxxramas, 40 raiders. The adds that belong to a fight stay: Anub'Rekhan's Crypt Guards, Faerlina's Worshippers and Followers, the
// Deathknight Understudies, the other three Horsemen, Gluth's Zombie Chows, Stalagg and Feugen.
inline constexpr RaidBossRow kNaxxBosses[] = {
    Sight({ "naxx_anubrekhan", "Naxxramas: 40 level 60 raiders to Anub'Rekhan (Locust Swarm kite, Crypt Guards, scarabs)",
      15956, "Anub'Rekhan",            20, 16573 }, 63.f),   // Crypt Guard; sees 55 y
    // Six Naxxramas Acolytes 31 y off stand in her formation (the world DB links them to her), so the clearing left them, and every ring spot
    // was refused for one inside 40 y (naxx_faerlina 2026-11-14 1114a). Keeping the Acolytes was wrong (1114c: the entry kept all 24 in the
    // instance, and their Shadow Bolt Volley killed 37): they are trash a raid clears first, so the formation's members beyond 20 y of her
    // go with the trash; her Worshippers and Followers stand at her side.
    ClearsFormation(Keeps({ "naxx_faerlina", "Naxxramas: 40 level 60 raiders to Grand Widow Faerlina (Worshipper sacrifice after each Enrage, Rain of Fire)",
      15953, "Grand Widow Faerlina", 20, 16506 }, 16505), 20.f),   // Naxxramas Worshipper, Follower
    Sight({ "naxx_maexxna",    "Naxxramas: 40 level 60 raiders to Maexxna (Web Wrap freed, Necrotic Poison dispelled, Poison Shock cone)",
      15952, "Maexxna",                20 }, 48.f),   // sees 40 y
    { "naxx_noth",       "Naxxramas: 40 level 60 raiders to Noth the Plaguebringer (the curse dispelled first, spread, balcony adds)",
      15954, "Noth the Plaguebringer", 20 },
    { "naxx_heigan",     "Naxxramas: 40 level 60 raiders to Heigan the Unclean (the eruption dance, Decrepit Fever, Mana Burn range)",
      15936, "Heigan the Unclean",     20 },
    { "naxx_loatheb",    "Naxxramas: 40 level 60 raiders to Loatheb (spores, Corrupted Mind heal rotation)",
      16011, "Loatheb",                25 },
    // The fight opens on a priest's Mind Control on an understudy (RaidMechanics_Naxx.cpp, NaxxPrePullRazuvious); the main tank's order is
    // onto a free understudy, never onto him: he kills a raid tank in 3 s (naxx_razuvious r22).
    OpensOn({ "naxx_razuvious",  "Naxxramas: 40 level 60 raiders to Instructor Razuvious (priests control the understudies that tank him)",
      16061, "Instructor Razuvious",   20, 16803 }, 16803),   // Deathknight Understudy
    // Gothik stands on his balcony (2642.1,-3387.0,285.5); his room's floor, where the combat gate stands (2692.07,-3360.78,267.5), is 18 y
    // below it. The ring search landed the raid at the balcony's height (2670.9,-3359.2,285.8, naxx-data-1114a), where the raid brain's
    // side split does not count it in the room: no split, and he reset at his first speech for fewer than 10 players on a side (naxx21,
    // 1114a). The raid lands on the living side's floor, 26 y south of the gate, and splits from there (RaidMechanics_Naxx.cpp, Gothik).
    // Nobody walks to him on the balcony, so the landing's route is checked to the dead side's spot of that split, 18 y north of the gate
    // (GothAnchor's first try): the way half the raid walks before the pull.
    RoutesTo(Staged({ "naxx_gothik",     "Naxxramas: 40 level 60 raiders to Gothik the Harvester (the side split, waves, the gate, teleports)",
      16060, "Gothik the Harvester",   25 }, 2691.2f, -3387.0f, 267.7f), 2692.1f, -3342.8f, 267.5f),
    // The four stand together in the middle of the room (Mograine's spawn 2524.3,-2951.3) and run at whoever is hit first. With only the
    // main tank opening from the landing (2573,-3018, the Korth'azz/Mograine side) all four came to the raid there, and Blaumeux and Zeliek
    // were dragged 100-117 y back across the room to their corners: at 18 s both still stood in the middle of the raid, Zeliek loose on a
    // healer, and by the third mark most raiders carried stacks of three or four horsemen (af54 r2-2: 27 of 40 deaths to marks 4 and 5).
    // Each horseman's own tank opens on him, as a raid's four tanks charge together, and drags him the 54-69 y from the middle to his corner.
    OpensEach(Sight(Keeps({ "naxx_horsemen", "Naxxramas: 40 level 60 raiders to the Four Horsemen (marks, the tank rotation, Void Zones, Meteor)",
      16062, "Highlord Mograine", 25, 16063 }, 16064, 16065), 83.f)),   // Zeliek, Korth'azz, Blaumeux; they see 75 y
    // His formation in the world DB spans the hall's trash (Bile Retchers 16018, 16017, 16021, 16029, 3000-3171 x, up to 300 y from him),
    // which the clearing left; it joined the fight on his patrol and Bile Vomit killed 21 (naxx_patchwerk 1114c). Cleared like trash.
    // His walk runs through his room on the raised floor (z 294.1), and no ring spot both sees him and keeps off it: the raid landed out
    // of his sight twice (1114e, 1114f: 17 spots near the walk, 43 out of sight, 158 without floor) and the healers' heals failed for line
    // of sight while the tanks died at the edge of the walk. It waits on the room's lower floor south of the walk (z 292.6, out of his
    // sight while he walks), and once he is pulled it goes up into the hall, where he is fought (RaidEncounterData_NAXX.cpp).
    // That lower floor lies behind his exit gate (181123 at 3318.0,-3254.4, closed until he dies): with bot paths kept clear of closed
    // doors the staging route stopped at the gate (release-0138 setup: path type 0x4, 46 y for 69 y straight), and no player stands
    // there before he dies. The raid waits at the hall's west end instead, on its own floor (z 294.1): a complete 48.5 y route to his
    // spawn, away from the gate (PC check on the map 533 navmesh, 2026-11-18). It is in his hall, so if his walk reaches it during the
    // hold he is pulled early: watch the first run for that.
    // It did: his walk (path 800882980) runs west from his spawn through 3257.4,-3230.4, 3 y from that spot, out to 3107,-3145 and back.
    // naxx_patchwerk sweep-1 (af52): he came through 7 s after the landing, opened on a healer still sitting to eat, killed it and a
    // priest at 0 s, and was fought at the hall's west end with the healers 21 y off and stuck leaving his circle (most of them at 1-3
    // heals a minute); 43 died. The raid now waits at the hall's east end (3328,-3232, the ranged and healer posts' floor, z 294.1), 23 y
    // past the east end of his walk (3305,-3231.8) and 20 y from his spawn: he walks home toward it and the main tank meets him at its post.
    Staged(ClearsFormation({ "naxx_patchwerk",  "Naxxramas: 40 level 60 raiders to Patchwerk (Hateful Strike soaked by three off tanks)",
      16028, "Patchwerk",              15 }, 20.f), 3328.0f, -3232.0f, 294.1f),
    { "naxx_grobbulus",  "Naxxramas: 40 level 60 raiders to Grobbulus (Mutating Injection carrier runs, poison clouds, Slime Spray cone)",
      15931, "Grobbulus",              20 },
    // Gluth sees 49 y (MoveInLineOfSight, classic_boss_gluth.cpp) from his spawn 3283.1,-3157.0 in the middle of his room. The raid lands
    // just outside that, on the north wall by the way in (3262,-3108: 53 y from him), 28 y from its camp (RaidEncounterData_NAXX.cpp,
    // 3290,-3110), so the main tank opens and the healers have a short walk. 0080 landed it on the old camp 3294,-3141, 19 y from him:
    // he came at once and killed a priest 1 s in (1115e). From the ring search's spot 57 y out the healers walked 62 y (1115d).
    Staged(Sight({ "naxx_gluth",      "Naxxramas: 40 level 60 raiders to Gluth (Mortal Wound tank swap, Frenzy tranquilized, Zombie Chows)",
      15932, "Gluth",                  20, 16360 }, 57.f), 3262.0f, -3108.0f, 297.8f),   // Zombie Chow; sees 49 y
    // Thaddius sits immune to players until Stalagg and Feugen are dead (classic_boss_thaddius.cpp): the raid opens on Stalagg, as a raid
    // does (naxx-grep-1114a: the order onto Thaddius was refused at 110 s and the raid stood for 120 s). It comes in at the room's door
    // (Thaddius Door 3421.9,-3017.5,295.6): the ring search landed it on his own walkway, which the slime pit and a 15 y wall cut off from
    // both add platforms (1115c: no path up, 0/5 killed). From the door both platforms are a walk (103 y to Stalagg's), and the raid brain
    // splits the raid onto them before the pull (RaidMechanics_Naxx.cpp, Thaddius).
    // That spot is the door itself (Thaddius Door 181121, world DB state closed): it stays shut and locked until Gluth is dead
    // (classic_instance_naxxramas.cpp, UpdateManualDoor on TYPE_GLUTH 11), and the clearing despawns Gluth without a death. With bot paths
    // kept clear of closed doors, af54 r2-1/r2-2 failed their setup at 0 s, Stalagg up on his platform: "no walkable route to the creature
    // (path type 0x8, length 89 y for 87 y straight)", where the map 533 navmesh has a complete 101 y route. Gluth is set dead and the
    // door opened, as a raid coming from Gluth's room finds it.
    Staged(ComesThrough(OpensOn(Keeps({ "naxx_thaddius", "Naxxramas: 40 level 60 raiders to Thaddius (Stalagg and Feugen first, Static Field, Polarity Shift sides)",
      15928, "Thaddius", 25, 15929 }, 15930), 15929), 181121, { { 11u, 3u } }), 3421.9f, -3017.5f, 295.6f),   // Stalagg, Feugen; Gluth DONE
    // Sapphiron has no creature row: the instance summons him at his lair once the raid touches his bones (GO_SAPPHIRON_SPAWN, Birth in
    // classic_boss_sapphiron.cpp; EVENT_SPAWN_SAPPHIRON at aSapphPositions, classic_naxxramas.h). The runner summons him there, as for
    // Ragnaros. Frost Aura (28529) on the whole raid, Life Drain, Blizzard, Cleave and Tail Sweep on the ground, then the air phase every 40 s
    // of ground: Icebolts that freeze their targets into ice blocks and Frost Breath, which only the blocks shelter from
    // (RaidMechanics_Naxx.cpp, Sapphiron). Worn in frost resistance, as a raid goes in.
    Sight(Resist({ "naxx_sapphiron", "Naxxramas: 40 level 60 raiders to Sapphiron (Frost Aura, Life Drain decursed, Blizzard, air phase: ice blocks and Frost Breath)",
      15989, "Sapphiron", 25, 0, 0, 0, 3522.56f, -5236.76f, 137.626f, 4.4855f }, ScnSchool::Frost), 60.f),
};

// ---- Naxxramas, Kel'Thuzad: the whole fight from the start of phase 1, 40 level 60 raiders ----
// His room's area trigger starts the encounter (OnKTAreaTrigger, classic_boss_kelthuzad.cpp); a raid placed in sight of him never walks
// through it, so the runner starts it as the trigger does (DoAction CLASSIC_NAXX_ACTION_KT_START). Phase 1: 5:20 of skeletons,
// abominations and banshees from the alcoves while he channels, immune to players; the raid fights them on its own. Then his intro and
// phase 2 15 s later, when he turns attackable; the order goes onto him. naxx-grep-1114a: the plain row ordered the tank onto him at 20 s,
// was refused (immune, the encounter never started) and the raid stood for 429 s.
inline ScenarioDef MakeNaxxKelThuzadScenario()
{
    static constexpr RaidBossRow kKt = { "naxx_kelthuzad", "Naxxramas: 40 level 60 raiders to Kel'Thuzad (phase 1 adds, Detonate Mana, Shadow Fissures, Guardians shackled)",
                                         15990, "Kel'Thuzad", 30 };
    ScenarioDef d = MakeRaidScenario(kKt, kNaxxEntrance, RaidSquad(kMcRaid, sizeof(kMcRaid) / sizeof(kMcRaid[0])));
    d.start_npc = 15990;
    d.start_npc_action = 1;        // CLASSIC_NAXX_ACTION_KT_START
    // The raid stands in the middle round him, as in Classic, away from the alcoves: placed by the ring search 40 y out (3707,-5104), it
    // stood within reach of an alcove's resting Soldiers, pulled them at 3 s and lost 33 raiders to their Dark Blast by 12 s
    // (naxx_kelthuzad 1114c). He is immune and passive in phase 1; same floor (z 142).
    // The middle is the room's, not his: he sits at 3746,-5113, 46 y from the east and south-east alcoves, and the old landing beside him
    // (3736,-5113) was 48 y from one and 52 y from another, against the 62-64 y every alcove keeps from the room's middle, the pull portal at
    // 3716,-5107 (classic_boss_kelthuzad.cpp ktPullPortal, ktAlcoves). The raid lands 4 y east of the portal (60-68 y from the alcoves), which despawns 7 s in.
    d.stage_x = 3720.4f; d.stage_y = -5107.8f; d.stage_z = 142.0f;
    d.goal_wait_ms = 8u * 60u * 1000u;   // phase 2 opens 5:35 after the start
    return d;
}

inline std::vector<ScenarioDef> MakeOtherRaidScenarios()
{
    std::vector<ScenarioDef> v;
    auto squad40 = [] { return RaidSquad(kMcRaid, sizeof(kMcRaid) / sizeof(kMcRaid[0])); };
    auto squad20 = [] { return RaidSquad(kRaid20, sizeof(kRaid20) / sizeof(kRaid20[0])); };
    for (RaidBossRow const& r : kBwlBosses)
        v.push_back(MakeRaidScenario(r, kBwlEntrance, squad40()));
    for (RaidBossRow const& r : kZgBosses)
        v.push_back(MakeRaidScenario(r, kZgEntrance, squad20()));
    for (RaidBossRow const& r : kAq20Bosses)
        v.push_back(MakeRaidScenario(r, kAq20Entrance, squad20()));
    for (RaidBossRow const& r : kAq40Bosses)
        v.push_back(MakeRaidScenario(r, kAq40Entrance, squad40()));
    for (RaidBossRow const& r : kNaxxBosses)
        v.push_back(MakeRaidScenario(r, kNaxxEntrance, std::string_view(r.name) == "naxx_horsemen"
                                                           ? RaidSquad(kHorsemenRaid, sizeof(kHorsemenRaid) / sizeof(kHorsemenRaid[0]))
                                                           : std::string_view(r.name) == "naxx_loatheb"
                                                           ? RaidSquad(kLoathebRaid, sizeof(kLoathebRaid) / sizeof(kLoathebRaid[0])) : squad40()));
    v.push_back(MakeNaxxKelThuzadScenario());
    return v;
}

// ---- damage_parity: one level 60 bot per class against a level 63 target dummy ----
// damage_parity_<class>   four reference legs of `leg_s` seconds (no gear / tier 1 / tier 3 / tier 3 with raid buffs and five Sunders), then on request or after a failed check the sensitivity legs (+spell power / +attack power / 0 armor / five Sunders)
// finisher_sweep_<class>  rogue, druid, warrior: every combo point or rage scaled spell at 1-5 points / rising rage, the damage must grow
// threat_audit_<class>    rogue, priest, hunter, druid: the threat reducer's drop on a dummy's threat list against vanilla's
// talent_audit_<class>    the class's key talents in the spellbook with their effects, and (warlock) Improved Shadow Bolt's +20 % after a crit
// spell_check_<class>     the player spell check: key spells cast at the dummy, each must land and hit for at least its floor and half its spell data
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

// ---- dps_scorecard_raid: one raid, every dps spec, one boss dummy ----
// A tank, three raid buffers and one bot per dps spec (Classic builds: 19) in Tier 3, all hitting one level 63 dummy (armor 3731, no resistances,
// two billion health) at the same time for `parity_leg_s` seconds. The runner forms one raid group; the bots keep their own logic, so the
// buffers cast the raid buffs, the shared debuffs come from whoever has them (Sunder Armor, curses, Shadow Weaving ...), and the bots drink
// their flasks and elixirs as in any raid. Parity::Kind::Group writes one row per bot to DpsScorecard.tsv. Alliance only (one raid group).
struct ScorecardRow
{
    char const* name;
    std::uint8_t race;
    std::uint8_t cls;
    std::uint32_t spec;
    ScenarioRoleKind role;
    bool melee;
};

inline constexpr ScorecardRow kScorecardRaid[] = {
    // Not scored: the main tank (Sunder Armor, Demoralizing Shout) and the three buffers (Fortitude/Spirit, Kings/Might/Wisdom, Mark of the Wild).
    { "Dsctank",    ScnRace::Human,    ScnClass::Warrior, 73,  ScenarioRoleKind::Tank,   true  },
    { "Dscpriest",  ScnRace::Human,    ScnClass::Priest,  257, ScenarioRoleKind::Healer, false },
    { "Dscpaladin", ScnRace::Human,    ScnClass::Paladin, 65,  ScenarioRoleKind::Healer, false },
    { "Dscdruid",   ScnRace::NightElf, ScnClass::Druid,   105, ScenarioRoleKind::Healer, false },
    // The dps specs.
    { "Dscarms",    ScnRace::Human,    ScnClass::Warrior, 71,  ScenarioRoleKind::Dps,    true  },
    { "Dscfury",    ScnRace::Human,    ScnClass::Warrior, 72,  ScenarioRoleKind::Dps,    true  },
    { "Dscret",     ScnRace::Human,    ScnClass::Paladin, 70,  ScenarioRoleKind::Dps,    true  },
    { "Dscbm",      ScnRace::Dwarf,    ScnClass::Hunter,  253, ScenarioRoleKind::Dps,    false },
    { "Dscmm",      ScnRace::Dwarf,    ScnClass::Hunter,  254, ScenarioRoleKind::Dps,    false },
    { "Dscsurv",    ScnRace::Dwarf,    ScnClass::Hunter,  255, ScenarioRoleKind::Dps,    false },
    { "Dsccombat",  ScnRace::Human,    ScnClass::Rogue,   260, ScenarioRoleKind::Dps,    true  },
    { "Dscassn",    ScnRace::Human,    ScnClass::Rogue,   259, ScenarioRoleKind::Dps,    true  },
    { "Dscshadow",  ScnRace::Human,    ScnClass::Priest,  258, ScenarioRoleKind::Dps,    false },
    { "Dscele",     ScnRace::Dwarf,    ScnClass::Shaman,  262, ScenarioRoleKind::Dps,    false },
    { "Dscenh",     ScnRace::Dwarf,    ScnClass::Shaman,  263, ScenarioRoleKind::Dps,    true  },
    { "Dscfire",    ScnRace::Human,    ScnClass::Mage,    63,  ScenarioRoleKind::Dps,    false },
    { "Dscfrost",   ScnRace::Human,    ScnClass::Mage,    64,  ScenarioRoleKind::Dps,    false },
    { "Dscarcane",  ScnRace::Human,    ScnClass::Mage,    62,  ScenarioRoleKind::Dps,    false },
    { "Dscaff",     ScnRace::Human,    ScnClass::Warlock, 265, ScenarioRoleKind::Dps,    false },
    { "Dscdemo",    ScnRace::Human,    ScnClass::Warlock, 266, ScenarioRoleKind::Dps,    false },
    { "Dscdest",    ScnRace::Human,    ScnClass::Warlock, 267, ScenarioRoleKind::Dps,    false },
    { "Dscbalance", ScnRace::NightElf, ScnClass::Druid,   102, ScenarioRoleKind::Dps,    false },
    { "Dscferal",   ScnRace::NightElf, ScnClass::Druid,   103, ScenarioRoleKind::Dps,    true  },
};

// ---- support_scorecard_<tank>: one raid with five healers and four dps, a boss that hits the tank and pulses the raid ----
// The healers and the tank of the support scorecard (Parity::BeginGroup with support): the boss swings at the tank and shoots a pulse at raiders in turn,
// the healers keep everyone up, four dps give the tank something to out-thread. One scenario per tank, so each tank spec is measured on its own
// against the same healers. The healers also buff (Fortitude and Spirit, Kings, Mark of the Wild). Alliance only.
struct SupportTank
{
    char const* suffix;
    ScorecardRow row;
};

inline constexpr SupportTank kSupportTanks[] = {
    { "warrior", { "Spttankw", ScnRace::Human,    ScnClass::Warrior, 73,  ScenarioRoleKind::Tank, true } },
    { "paladin", { "Spttankp", ScnRace::Human,    ScnClass::Paladin, 66,  ScenarioRoleKind::Tank, true } },
    { "druid",   { "Spttankd", ScnRace::NightElf, ScnClass::Druid,   104, ScenarioRoleKind::Tank, true } },
};

inline constexpr ScorecardRow kSupportRaid[] = {
    { "Spthpriest", ScnRace::Human,    ScnClass::Priest,  257, ScenarioRoleKind::Healer, false },
    { "Sptdpriest", ScnRace::Human,    ScnClass::Priest,  256, ScenarioRoleKind::Healer, false },
    { "Spthpala",   ScnRace::Human,    ScnClass::Paladin, 65,  ScenarioRoleKind::Healer, false },
    { "Sptrdruid",  ScnRace::NightElf, ScnClass::Druid,   105, ScenarioRoleKind::Healer, false },
    { "Sptrshaman", ScnRace::Dwarf,    ScnClass::Shaman,  264, ScenarioRoleKind::Healer, false },
    { "Sptfury",    ScnRace::Human,    ScnClass::Warrior, 72,  ScenarioRoleKind::Dps,    true  },
    { "Sptcombat",  ScnRace::Human,    ScnClass::Rogue,   260, ScenarioRoleKind::Dps,    true  },
    { "Sptfire",    ScnRace::Human,    ScnClass::Mage,    63,  ScenarioRoleKind::Dps,    false },
    { "Sptaff",     ScnRace::Human,    ScnClass::Warlock, 265, ScenarioRoleKind::Dps,    false },
};

inline ScenarioDef MakeSupportScenario(SupportTank const& tank, char const* name, char const* summary)
{
    ScenarioDef d{};
    d.name = name;
    d.summary = summary;
    d.level = 60;
    d.squad.push_back({ tank.row.name, tank.row.race, tank.row.cls, tank.row.role, tank.row.spec, tank.row.melee });
    for (ScorecardRow const& r : kSupportRaid)
        d.squad.push_back({ r.name, r.race, r.cls, r.role, r.spec, r.melee });
    d.map = 0;
    d.x = 0.f; d.y = 0.f; d.z = 0.f; d.o = 0.f;
    d.goal = ScenarioGoalKind::Parity;
    d.goal_entry = 0;
    d.goal_name = "";
    d.timeout_ms = 30u * 60u * 1000u;
    d.limits.expected_map = 0;
    d.parity = 9;                      // the group runtime in support mode
    d.parity_leg_s = 120;
    d.full_clear = true;
    return d;
}

inline ScenarioDef MakeScorecardScenario()
{
    ScenarioDef d{};
    d.name = "dps_scorecard_raid";
    d.summary = "DPS scorecard: one Tier 3 raid, a tank, three buffers and one bot per dps spec, all on one boss dummy for 2 minutes (Playerbot.DamageParity.LegSeconds changes it); one row per bot in DpsScorecard.tsv";
    d.level = 60;
    for (ScorecardRow const& r : kScorecardRaid)
        d.squad.push_back({ r.name, r.race, r.cls, r.role, r.spec, r.melee });
    d.map = 0;
    d.x = 0.f; d.y = 0.f; d.z = 0.f; d.o = 0.f;
    d.goal = ScenarioGoalKind::Parity;
    d.goal_entry = 0;
    d.goal_name = "";
    d.timeout_ms = 40u * 60u * 1000u;   // 23 characters set up four a step, then 60 s of buffing, a warm-up and the measurement
    d.limits.expected_map = 0;
    d.parity = 8;                      // Parity::Kind::Group
    d.parity_leg_s = 120;
    d.full_clear = true;               // long: `runall` with no filter leaves it out
    return d;
}

// ---- brm_follow: a player's group through Blackrock Mountain on foot (Karrz 2026-10-30) ----
// The rogue leads like a player from the Blackrock Spire door at the top down to the Blackrock Depths door at the bottom, and back up. The
// way down has drops the bots leap (follow_drop); the way up is where a player takes the chains and the jumps, which bots cannot: where
// the rogue finds no walking route he is set down on the point, and the bots that cannot follow are brought to him (CatchUpToOwner). The
// report says per point how long the squad took to gather and who walked and who was brought. Points are the instance portals' trigger
// centres (hotfixes area_trigger 1466 / 1468): on the floor, and a bot standing in one does not zone (the client sends the trigger).
inline ScenarioDef MakeBrmFollowScenario()
{
    ScenarioDef d{};
    d.name = "brm_follow";
    d.summary = "Blackrock Mountain on foot: level 60 party led like a player from the Spire door down to the Depths door and back up";
    d.level = 60;
    d.squad = StandardParty();
    d.map = 0;
    d.x = -7518.19f; d.y = -1239.13f; d.z = 285.743f;
    d.o = 0.f;
    d.goal = ScenarioGoalKind::FollowRoute;
    d.goal_name = "";
    d.timeout_ms = 20u * 60u * 1000u;
    d.limits.expected_map = 0;
    d.limits.bounds = { true, d.x - 700.f, d.x + 700.f, d.y - 700.f, d.y + 700.f, 0.f, 500.f };
    // The gather check at each point (FollowRoute) is the test: a bot left on the other level while the rogue goes on is what it is
    // about, so the tank-distance and stand-still rules are widened past the time the gather allows.
    d.limits.stray_yards = 500.f;
    d.limits.idle_ms = 120000;
    d.stand_in = "Scnrogue";
    d.player_lead = true;
    d.route = { { "the Blackrock Depths door", -7176.63f, -937.667f, 168.706f },
                { "the Blackrock Spire door", -7518.19f, -1239.13f, 285.743f } };
    return d;
}

// ---- lake_swim: field bots in a lake (Discord 2026-11-08: bots on top of the water, then under it, forever; some drown) ----
// The rogue leads like a player from Goldshire to the deep water of Crystal Lake, stays there two minutes (longer than a bot's breath),
// swims to a second deep spot and walks back. The squad follows him into the water and waits around him. Every bot must swim: the
// water check fails a bot whose head stays under for 15 s, that goes under and back up 6 times in a minute, or that is in deep water
// without the swimming flag for 5 s (standing on top of the water or on the lake bed). A drowning is a death, which fails the run.
// The [swim] and [swim_settle] lines in Server.log show each bot start, stop and settle. The deep spots are searched for when the leg
// begins, around the given points (Crystal Lake, east of Goldshire), so no exact water coordinate is needed.
inline ScenarioDef MakeLakeSwimScenario()
{
    ScenarioDef d{};
    d.name = "lake_swim";
    d.summary = "Crystal Lake: level 60 party led into deep water, two minutes in it, then out: every bot swims, none sinks, bobs or drowns";
    d.level = 60;
    d.squad = StandardParty();
    d.map = 0;
    d.x = -9464.f; d.y = 62.f; d.z = 56.f;   // Goldshire
    d.o = 0.f;
    d.goal = ScenarioGoalKind::FollowRoute;
    d.goal_name = "";
    d.timeout_ms = 15u * 60u * 1000u;
    d.limits.expected_map = 0;
    d.limits.bounds = { true, d.x - 600.f, d.x + 600.f, d.y - 800.f, d.y + 400.f, -50.f, 200.f };
    // The squad stands in the water around him on purpose: the stand-still rules are widened past the hold.
    d.limits.stray_yards = 200.f;
    d.limits.idle_ms = 180000;
    d.limits.stall_ms = 240000;
    d.limits.water_under_ms = 15000;
    d.limits.water_flips = 6;
    d.limits.water_noswim_ms = 5000;
    d.stand_in = "Scnrogue";
    d.player_lead = true;
    // Crystal Lake on the Elwynn map is about (51, 66): (-9467, -235) in world yards.
    d.route = { { "the deep water of Crystal Lake", -9467.f, -235.f, 50.f, true, 120 },
                { "a second deep spot in the lake", -9440.f, -270.f, 50.f, true, 30 },
                { "Goldshire", -9464.f, 62.f, 56.f } };
    return d;
}

// ---- vile_reef_fight: an ordered fight under water (Discord 2026-11-16, Gefest: at the Vile Reef in Stranglethorn, "when given the
// command to attack, the companions freeze") ----
// The squad lands on the water above the Vile Reef. The rogue leads like a player: he swims down into the deep water, and once the squad is
// with him he selects the nearest Saltscale Warrior (871, on the reef at 55-75 y under the surface), says "attack" and fights it, then a
// second one. Every member must fight: the [scenario_water_fight] lines show each one's distance, swim flag, motion and victim every 5 s,
// and a Saltscale left alive 90 s after the order fails the run. The water check of lake_swim applies (no drowning, no bobbing).
// Where the squad lands: the open water over the reef (-12200, 640); the deep spot is searched for when the leg begins.
inline ScenarioDef MakeVileReefFightScenario()
{
    ScenarioDef d{};
    d.name = "vile_reef_fight";
    d.summary = "Vile Reef: level 45 party ordered onto Saltscale murlocs under water: every bot swims down and fights, none freezes or drowns";
    d.level = 45;
    d.squad = StandardParty();
    d.map = 0;
    d.x = -12200.f; d.y = 640.f; d.z = 1.f;   // the water's surface over the reef
    d.o = 0.f;
    d.goal = ScenarioGoalKind::FollowRoute;
    d.goal_name = "";
    d.timeout_ms = 12u * 60u * 1000u;
    d.limits.expected_map = 0;
    d.limits.bounds = { true, d.x - 400.f, d.x + 400.f, d.y - 400.f, d.y + 400.f, -150.f, 100.f };
    d.limits.stray_yards = 200.f;
    d.limits.idle_ms = 180000;
    d.limits.stall_ms = 240000;
    d.limits.water_under_ms = 0;          // a fight on the reef is under water on purpose: breath is the drowning check's job
    d.limits.water_flips = 6;
    d.limits.water_noswim_ms = 5000;
    d.stand_in = "Scnrogue";
    d.player_lead = true;
    d.route = { { "the deep water over the Vile Reef", -12200.f, 640.f, -60.f, true, 0, 871 },
                { "a second Saltscale on the reef", -12200.f, 640.f, -60.f, true, 0, 871 } };
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
            // Gnomeregan reaches 720 y north of its entrance (Thermaplugg at y 670): the 600 y box ended at y 596.8, inside the
            // Electrocutioner's hall, and failed a rogue fighting there (0.1.27 set A, out_of_bounds at (-531.4,596.9,-230.6)). The box
            // is the map 90 creature and object spawns' extent (x -910..-361, y -140..721, z -334..-148) with 50 y around it.
            if (std::string(r.name) == "gnomeregan_electrocutioner")
                d.limits.bounds = { true, -960.f, -311.f, -190.f, 771.f, -384.f, -98.f };
            // The same dungeon led like the owner leads his bots (ScenarioDef::player_lead): the rogue is the player, walks the route,
            // calls the pulls and the attacks; the rest play as a player's group (the group fight, the autopilot stood down).
            static std::deque<std::string> s_led_names;
            std::string const base = r.name;
            bool const led = base == "rfc_oggleflint" || base == "wc_cobrahn" || base == "sfk_rethilgore" || base == "deadmines_rhahkzor";
            ScenarioDef l = d;
            std::size_t const plain_at = v.size();
            v.push_back(std::move(d));
            if (led)
            {
                s_led_names.push_back(base + "_led");
                s_led_names.push_back(std::string(r.summary) + ", led like a player (the rogue walks, calls the pulls and the attacks)");
                l.name = s_led_names[s_led_names.size() - 2].c_str();
                l.summary = s_led_names.back().c_str();
                l.stand_in = "Scnrogue";
                l.player_lead = true;
                // He pulls each pack within 30 y on its own, says "attack" when it is back with him and waits for health and mana
                // after every fight: twice the bot-led run's time (rfc_oggleflint_led RC8: route point 23 of 54 at 835 s, five
                // minutes of it waiting for mana; the bot-led run passes in 600-620 s).
                l.timeout_ms *= 2;
                // Ragefire, 40 minutes: RC10 reached route point 50 of 54 at 1737 s of its 1800, 54 of its 62 waits on the 60 % led rest.
                if (base == "rfc_oggleflint")
                    l.timeout_ms = 40u * 60u * 1000u;
                v.push_back(std::move(l));
            }
            // The Library at 36 with an owner's two orders a bot-only squad never gets (test items 4 and 19 of the dungeon list): the
            // healer told "focus heal" on the tank, and the squad told "yes fear" (the Tracking Hounds are beasts: the hunter's Scare Beast
            // and the warrior's Intimidating Shout are allowed). The plain run shows the default, these show the orders working.
            if (base == "smlibrary_loksey36")
            {
                ScenarioDef f = v[plain_at];
                f.name = "smlibrary_loksey36_focusheal";
                f.summary = "Scarlet Monastery Library: level 36 party to Loksey, the healer told 'focus heal' on the tank";
                f.focus_heal_tank = true;
                v.push_back(std::move(f));
                ScenarioDef y = v[plain_at];
                y.name = "smlibrary_loksey36_yesfear";
                y.summary = "Scarlet Monastery Library: level 36 party to Loksey, the squad told 'yes fear'";
                y.yes_fear = true;
                v.push_back(std::move(y));
            }
        }
        // Krol'dok Stronghold (Forever, map 3039), the cave landing to Overlord Mugg: Slavecrusher Grox, Twilight Commander Zarmok, War
        // Handler Ashka with her wolves, Nar'gurk, Bossman Toof and his shamans, Mugg (the 0046 route and KroldokStrongholdScript's
        // order; the spawns are the live kill spots of 2026_11_19_66). Bot-led, at 43.
        {
            static constexpr DungeonRow kKroldok = { "kroldok_full", "Krol'dok Stronghold: level 43 party, the cave to Overlord Mugg",
                                                     3039, -7659.97f, -3713.1f, 157.51f, 18.f, 43, 258968, "Overlord Mugg", 75 };
            ScenarioDef d = MakeDungeonScenario(kKroldok);
            // The spawns and the route span x -7660..-6826, y -4588..-3713, z 124..265; 50 y around them.
            d.limits.bounds = { true, -7710.f, -6776.f, -4638.f, -3663.f, 74.f, 315.f };
            d.full_clear = true;
            // Staging spots: crumbs of the 0046 route 35-50 y of walk before each boss.
            d.checkpoints = { { "grox", 258966, -7519.87f, -3851.64f, 124.14f },
                              { "zarmok", 258972, -7477.29f, -4062.44f, 152.30f },
                              { "ashka", 259281, -7264.91f, -4471.41f, 176.44f },
                              { "nargurk", 258964, -7305.20f, -4040.53f, 263.75f },
                              { "toof", 264773, -7184.47f, -4408.93f, 243.96f },
                              { "mugg", 258968, -6867.02f, -4573.96f, 242.73f } };
            v.push_back(std::move(d));
        }
        // Ruins of Lordaeron (Forever, map 2999), the entrance to Bjork: Witherfang, The Baron, The Abandoned (the statue), Rath'mael,
        // Bjork (the RuinsOfLordaeronScript order; the spawns are the official sniff's, world 2026_10_11_01). Bot-led, at 18. A
        // player's tank-led group stalled here with no boss to walk to before the script existed.
        {
            static constexpr DungeonRow kRol = { "rol_full", "Ruins of Lordaeron: level 18 party, entrance to Bjork",
                                                 2999, 1804.15f, 149.174f, 70.399f, 266.f, 18, 256097, "Bjork", 60 };
            ScenarioDef d = MakeDungeonScenario(kRol);
            d.full_clear = true;
            // No route yet, so no route crumbs to stage on: the runner searches a ring round each boss. The Abandoned has no spawn
            // (the statue summons him), so he has no checkpoint: a checkpoint starts the squad in sight of its creature's spawn.
            d.checkpoints = { { "witherfang", 250483 }, { "baron", 250660 }, { "rathmael", 250657 }, { "bjork", 256097 } };
            v.push_back(std::move(d));
        }
        // The Drowned City (Forever, map 2970), the cave landing to Min'loth the Serpent: Zul'Alai, Var'Taka, Captain Dreadrise,
        // Deathless Marrow, the Primeval Elemental in the flooded quarter's pool, Min'loth (the 0047 route and DrownedCityScript's
        // order; the spawns are the live kill spots of 2026_11_19_67). Bot-led, at 38.
        {
            static constexpr DungeonRow kDrowned = { "drowned_full", "The Drowned City: level 38 party, the cave to Min'loth the Serpent",
                                                     2970, -13296.7f, 2232.3f, -47.5f, 91.f, 38, 260243, "Min'loth the Serpent", 90 };
            ScenarioDef d = MakeDungeonScenario(kDrowned);
            // The spawns and the route span x -13733..-13064, y 2214..3339, z -63..13; 50 y around them.
            d.limits.bounds = { true, -13783.f, -13014.f, 2164.f, 3389.f, -113.f, 63.f };
            d.full_clear = true;
            // Staging spots: crumbs of the 0047 route 35-50 y of walk before each boss.
            d.checkpoints = { { "zulalai", 258905, -13282.03f, 2464.53f, -3.20f },
                              { "vartaka", 258924, -13545.44f, 2781.87f, -13.42f },
                              { "dreadrise", 260042, -13109.33f, 2698.67f, -33.55f },
                              { "marrow", 260202, -13321.27f, 3038.13f, -7.66f },
                              { "primeval", 263886, -13536.00f, 2592.00f, 0.05f },
                              { "minloth", 260243, -13365.33f, 3338.67f, 0.18f } };
            v.push_back(std::move(d));
        }
        // Whole-dungeon runs of the classic dungeons, entrance to the last boss, bot-led (a player's report: tank-led groups get stuck in
        // about 4 of 10 runs, and only Deadmines had a whole-dungeon scenario, so a sweep could not show where). The goal is the last
        // fixed-spawn boss (Mutanus and Atal'alarion are event summons, so Verdan and Eranikus); the checkpoints are the bosses in the
        // order of the map's dungeon script, logged as they fall, so a stall shows as the first checkpoint not down. Bounds: the map's
        // dungeon route (playerbot_shared_data / 0034-0036, Dire Maul's 0050) and the entrance, 50 y around, z 50 below and above. Scholomance has no
        // route, so it keeps the wide default box. Event bosses on the way (Aku'mai's fires, Archaedas, the Zul'Farrak stairs, the
        // Sunken Temple trolls, the Ring of Law) are part of what the sweep tests.
        {
            struct FullRun
            {
                DungeonRow row; ScenarioBounds bounds; std::vector<ScenarioCheckpoint> checkpoints;
                std::uint32_t carry_entry = 0, carry_entry_b = 0;
                bool goal_after_checkpoints = false;   // see ScenarioDef::goal_after_checkpoints
            };
            static FullRun const kClassicFull[] = {
                { { "rfc_full", "Ragefire Chasm: level 14 party, entrance to Bazzalan",
                    389, 0.797643f, -8.23429f, -15.5288f, 270.f, 14, 11519, "Bazzalan", 40 },
                  { true, -418.f, 51.f, -104.f, 265.f, -111.f, 35.f },
                  { { "oggleflint", 11517 }, { "taragaman", 11520 }, { "jergosh", 11518 }, { "bazzalan", 11519 } } },
                { { "wc_full", "Wailing Caverns: level 20 party, entrance to Verdan the Everliving",
                    43, -158.441f, 131.601f, -74.2552f, 335.f, 20, 5775, "Verdan the Everliving", 60 },
                  { true, -372.f, 108.f, -364.f, 544.f, -157.f, 23.f },
                  { { "anacondra", 3671 }, { "kresh", 3653 }, { "cobrahn", 3669 }, { "pythas", 3670 }, { "skum", 3674 }, { "serpentis", 3673 },
                    { "verdan", 5775 } } },
                { { "sfk_full", "Shadowfang Keep: level 24 party, entrance to Archmage Arugal",
                    33, -228.191f, 2111.41f, 76.8904f, 70.f, 24, 4275, "Archmage Arugal", 50 },
                  { true, -346.f, -32.f, 2053.f, 2351.f, 25.f, 206.f },
                  { { "rethilgore", 3914 }, { "razorclaw", 3886 }, { "silverlaine", 3887 }, { "springvale", 4278 }, { "odo", 4279 },
                    { "fenrus", 4274 }, { "nandos", 3927 }, { "arugal", 4275 } } },
                { { "bfd_full", "Blackfathom Deeps: level 26 party, entrance to Aku'mai",
                    48, -150.234f, 106.594f, -39.779f, 255.f, 26, 4829, "Aku'mai", 60 },
                  { true, -899.f, -100.f, -504.f, 360.f, -107.f, 33.f },
                  { { "ghamoora", 4887 }, { "gelihast", 6243 }, { "kelris", 4832 }, { "akumai", 4829 } } },
                { { "stockade_full", "Stormwind Stockade: level 27 party, entrance to Bazil Thredd",
                    34, 48.9849f, 0.483882f, -16.3942f, 360.f, 27, 1716, "Bazil Thredd", 30 },
                  { true, -1.f, 222.f, -161.f, 167.f, -85.f, 34.f },
                  { { "targorr", 1696 }, { "kam", 1666 }, { "hamhock", 1717 }, { "dextren", 1663 }, { "bazil", 1716 } } },
                { { "smgraveyard_full", "Scarlet Monastery Graveyard: level 30 party, entrance to Bloodmage Thalnos",
                    189, 1687.27f, 1050.09f, 18.6773f, 90.f, 30, 4543, "Bloodmage Thalnos", 30 },
                  { true, 70.f, 2019.f, -497.f, 1482.f, -58.f, 83.f },
                  { { "vishas", 3983 }, { "thalnos", 4543 } } },
                { { "rfk_full", "Razorfen Kraul: level 31 party, entrance to Charlga Razorflank",
                    47, 1942.27f, 1544.23f, 83.3055f, 75.f, 31, 4421, "Charlga Razorflank", 60 },
                  { true, 1892.f, 2241.f, 1361.f, 2027.f, 11.f, 136.f },
                  { { "aggem", 4424 }, { "jargba", 4428 }, { "ramtusk", 4420 }, { "agathelos", 4422 }, { "charlga", 4421 } } },
                { { "gnomeregan_full", "Gnomeregan: level 32 party, entrance to Mekgineer Thermaplugg",
                    90, -329.098f, -3.20722f, -152.851f, 170.f, 32, 7800, "Mekgineer Thermaplugg", 75 },
                  { true, -942.f, -277.f, -156.f, 750.f, -381.f, -96.f },
                  { { "grubbis", 7361 }, { "fallout", 7079 }, { "electrocutioner", 6235 }, { "pummeler", 6229 }, { "thermaplugg", 7800 } } },
                { { "smlibrary_full", "Scarlet Monastery Library: level 33 party, entrance to Arcanist Doan",
                    189, 253.672f, -206.624f, 18.6773f, 270.f, 33, 6487, "Arcanist Doan", 35 },
                  { true, 70.f, 2019.f, -497.f, 1482.f, -58.f, 83.f },
                  { { "loksey", 3974 }, { "doan", 6487 } } },
                // Herod's Door (101854) is the only way into Herod's room and the Chapel Door (104591) the only way to Mograine and
                // Whitemane; both spawn shut and locked, and their lock (299) takes The Scarlet Key (7146, Doan's Strongbox in the
                // Library), Lockpicking 175 or a blast. The lever by each door is on the room's side. The tank comes with the key, as a
                // party that has done the Library, and opens the door with it (BotSnapshotBuilder KeyForShutDoor).
                { { "smarmory_full", "Scarlet Monastery Armory: level 37 party, entrance to Herod",
                    189, 1608.1f, -318.919f, 18.6714f, 270.f, 37, 3975, "Herod", 35 },
                  { true, 70.f, 2019.f, -497.f, 1482.f, -58.f, 83.f },
                  { { "herod", 3975 } }, 7146 },
                { { "smcathedral_full", "Scarlet Monastery Cathedral: level 38 party, entrance to High Inquisitor Whitemane",
                    189, 853.179f, 1319.18f, 18.6714f, 90.f, 38, 3977, "High Inquisitor Whitemane", 40 },
                  { true, 70.f, 2019.f, -497.f, 1482.f, -58.f, 83.f },
                  { { "fairbanks", 4542 }, { "mograine", 3976 }, { "whitemane", 3977 } }, 7146, 0, true },
                // Tuten'kash and Plaguemaw come from the gong and the Belnistrasz event (no static spawn), so they are no checkpoint.
                { { "rfd_full", "Razorfen Downs: level 40 party, entrance to Amnennar the Coldbringer",
                    129, 2593.68f, 1111.23f, 50.9518f, 270.f, 40, 7358, "Amnennar the Coldbringer", 60 },
                  { true, 2281.f, 2643.f, 622.f, 1161.f, -24.f, 113.f },
                  { { "glutton", 8567 }, { "mordresh", 7357 }, { "amnennar", 7358 } } },
                { { "uldaman_full", "Uldaman: level 44 party, entrance to Archaedas",
                    70, -228.859f, 46.1018f, -46.0186f, 90.f, 44, 2748, "Archaedas", 75 },
                  { true, -403.f, 216.f, -4.f, 505.f, -104.f, 26.f },
                  { { "revelosh", 6910 }, { "baelog", 6906 }, { "ironaya", 7228 }, { "sentinel", 7023 }, { "stonekeeper", 7206 },
                    { "galgann", 7291 }, { "grimlok", 4854 }, { "archaedas", 2748 } },
                  7733 },   // Staff of Prehistoria: the Keystone (124371, lock 359) that opens the Seal of Khaz'Mul to Ironaya takes only it
                { { "maraudon_full", "Maraudon: level 48 party, orange entrance to Princess Theradras",
                    349, 1016.83f, -458.52f, -43.4737f, 360.f, 48, 12201, "Princess Theradras", 90 },
                  // Bounds: the 0053 route, which goes on from the outer caves into the inner ones (the old box held only the outer).
                  { true, -154.f, 1180.f, -523.f, 303.f, -246.f, 19.f },
                  { { "noxxion", 13282 }, { "razorlash", 12258 }, { "vyletongue", 12236 }, { "celebras", 12225 }, { "landslide", 12203 },
                    { "gizlock", 13601 }, { "rotgrip", 13596 }, { "theradras", 12201 } } },
                { { "zf_full", "Zul'Farrak: level 48 party, entrance to Chief Ukorz Sandscalp",
                    209, 1212.67f, 842.04f, 8.93346f, 360.f, 48, 7267, "Chief Ukorz Sandscalp", 60 },
                  { true, 1163.f, 1962.f, 649.f, 1313.f, -41.f, 105.f },
                  { { "antusul", 8127 }, { "theka", 7272 }, { "zumrah", 7271 }, { "velratha", 7795 }, { "ukorz", 7267 } } },
                { { "st_full", "Sunken Temple: level 50 party, entrance to the Shade of Eranikus",
                    109, -315.903f, 100.197f, -131.849f, 180.f, 50, 5709, "Shade of Eranikus", 120 },
                  // Bounds: the 0053 route, down to Atal'alarion's pit (z -189). 120 min: the six statues and the six balconies are
                  // each a climb of their own, about 5 km of walk with the route.
                  { true, -728.f, -239.f, -146.f, 323.f, -239.f, -37.f },
                  { { "atalarion", 8580 }, { "hukku", 5715 }, { "mijan", 5717 }, { "zullor", 5716 }, { "zolo", 5712 }, { "gasher", 5713 },
                    { "loro", 5714 }, { "morphaz", 5719 }, { "hazzas", 5722 }, { "jammalan", 5710 }, { "weaver", 5720 },
                    { "dreamscythe", 5721 }, { "eranikus", 5709 } } },
                { { "brd_full", "Blackrock Depths: level 56 party, entrance to Emperor Dagran Thaurissan",
                    230, 456.929f, 34.0923f, -68.0896f, 270.f, 56, 9019, "Emperor Dagran Thaurissan", 150 },
                  { true, 262.f, 1435.f, -886.f, 234.f, -142.f, 7.f },
                  { { "gerstahn", 9018 }, { "roccor", 9025 }, { "grebmar", 9319 }, { "incendius", 9017 }, { "angerforge", 9033 },
                    { "argelmach", 8983 }, { "plugger", 9499 }, { "flamelash", 9156 }, { "doomrel", 9039 }, { "magmus", 9938 },
                    { "emperor", 9019 } },
                  // The Shadowforge Key: the Shadowforge Gate, the East Garrison Door and the Shadowforge Lock that opens the Giant Doors
                  // (170559, 170570, 161460; lock 680: the key, Lockpicking 250 or a blast), the way past Incendius and Angerforge, and
                  // the Lyceum door (170558, lock 680) past the Tomb of the Seven. The Shadowforge Torch: the two Shadowforge Braziers
                  // (174744, 174745, lock 799) in the Lyceum, both lit open the Golem Rooms to Magmus. The Lyceum's Shadowforge Flame
                  // Keepers drop it (80 %), but to whoever loots the body; the run tests the lighting, not the loot roll.
                  11000, 11885 },
                // Lower Spire only: the way on to Upper Blackrock Spire opens for a player holding the Seal of Ascension.
                { { "lbrs_full", "Lower Blackrock Spire: level 58 party, entrance to Overlord Wyrmthalak",
                    229, 78.3534f, -226.841f, 49.7662f, 270.f, 58, 9568, "Overlord Wyrmthalak", 90 },
                  { true, -249.f, 157.f, -627.f, -177.f, -68.f, 141.f },
                  { { "omokk", 9196 }, { "voshgajin", 9236 }, { "voone", 9237 }, { "smolderweb", 10596 }, { "zigris", 9736 },
                    { "halycon", 10220 }, { "wyrmthalak", 9568 } } },
                // Upper Spire only, from the same entrance: up the stairs and through the Dragonspine Door, which opens for a player
                // holding the Seal of Ascension; the seven rune rooms (every Summoner and Veteran dead opens Emberseer In); the
                // Blackrock Altar, three players at once, wakes Pyroguard Emberseer; the stadium event (walk in: the waves, then Gyth
                // and Rend off his back; won, the exit portcullis opens); the Beast; General Drakkisath. Bounds: the Upper Spire part
                // of the route (seq 145-226 of 0052), 50 y around, z 50 below and above.
                { { "ubrs_full", "Upper Blackrock Spire: level 60 party, entrance to General Drakkisath",
                    229, 78.3534f, -226.841f, 49.7662f, 270.f, 60, 10363, "General Drakkisath", 120 },
                  { true, -22.f, 274.f, -614.f, -177.f, -1.f, 173.f },
                  { { "emberseer", 9816 }, { "gyth", 10339 }, { "rend", 10429 }, { "beast", 10430 }, { "drakkisath", 10363 } },
                  // The Seal of Ascension: the Dragonspine Door opens while a living player holding it is near (the instance's
                  // update, and area trigger 2046 on the stairs). A party earns it on a quest chain; the run tests the Spire, not the
                  // chain.
                  12344 },
                // Dire Maul, one run per wing from its entrance (the 0050 route). Each wing has a way on a player opens and the bots
                // do not yet: East, the Conservatory Door to Alzzin (talk to Old Ironbark once Zevrim is dead); West, Immol'thar's
                // force field (kill the guards of all five pylons); North, the Gordok Courtyard and Inner Doors (the keys from
                // Fengus's Chest and Guard Mol'dar). A party that stops there shows that gate.
                { { "dmeast_full", "Dire Maul East: level 57 party, entrance to Alzzin the Wildshaper",
                    429, -203.166f, -322.997f, -2.72467f, 270.f, 57, 11492, "Alzzin the Wildshaper", 75 },
                  { true, -253.f, 885.f, -579.f, 871.f, -170.f, 89.f },
                  { { "pusillin", 14354 }, { "hydrospawn", 13280 }, { "lethtendris", 14327 }, { "zevrim", 11490 }, { "alzzin", 11492 } } },
                { { "dmwest_full", "Dire Maul West: level 58 party, entrance to Prince Tortheldrin",
                    429, 33.1083f, 158.977f, -3.47126f, 45.f, 58, 11486, "Prince Tortheldrin", 90 },
                  { true, -253.f, 885.f, -579.f, 871.f, -170.f, 89.f },
                  { { "tendris", 11489 }, { "kalendris", 11487 }, { "illyanna", 11488 }, { "immolthar", 11496 }, { "tortheldrin", 11486 } } },
                { { "dmnorth_full", "Dire Maul North: level 58 party, entrance to King Gordok",
                    429, 254.92f, -19.463f, -2.5596f, 315.f, 58, 11501, "King Gordok", 75 },
                  { true, -253.f, 885.f, -579.f, 871.f, -170.f, 89.f },
                  { { "moldar", 14326 }, { "kreeg", 14322 }, { "fengus", 14321 }, { "slipkik", 14323 }, { "kromcrush", 14325 },
                    { "chorush", 14324 }, { "gordok", 11501 } } },
                { { "scholo_full", "Scholomance: level 58 party, entrance to Darkmaster Gandling",
                    289, 190.819f, 126.329f, 137.227f, 360.f, 58, 1853, "Darkmaster Gandling", 120 },
                  { true, -409.f, 791.f, -474.f, 726.f, -163.f, 437.f },
                  { { "jandice", 10503 }, { "rattlegore", 11622 }, { "ravenian", 10507 }, { "polkelt", 10901 }, { "vectus", 10432 },
                    { "krastinov", 11261 }, { "illucia", 10502 }, { "alexei", 10504 }, { "ras", 10508 }, { "gandling", 1853 } },
                  13873 },   // the Skeleton Key: the Viewing Room Door (175167, lock 1199) on the way down takes only it
                { { "stratlive_full", "Stratholme (main gate): level 58 party, entrance to Balnazzar",
                    329, 3392.92f, -3395.03f, 143.135f, 90.f, 58, 10813, "Balnazzar", 90 },
                  { true, 3343.f, 4086.f, -3766.f, -2887.f, 65.f, 198.f },
                  { { "forresten", 10558 }, { "malor", 11032 }, { "galford", 10811 } },
                  // Balnazzar is Grand Crusader Dathrohan (10812) until he turns.
                  // The Scarlet Key: the Hall of the High Command and the Bastion Door (176194, 175967; lock 299) to Galford and Dathrohan.
                  7146 },
                { { "stratud_full", "Stratholme (service gate): level 58 party, entrance to Baron Rivendare",
                    329, 3590.87f, -3643.22f, 138.491f, 315.f, 58, 10440, "Baron Rivendare", 150 },
                  // 150 min: after the three ziggurats the Slaughter Square sends its 13 abominations one at a time (35-45 s apart),
                  // then Ramstein, 34 undead and the Black Guard, before the Baron's door opens.
                  { true, 3343.f, 4086.f, -3766.f, -2887.f, 65.f, 198.f },
                  { { "barthilas", 10435 }, { "anastari", 10436 }, { "nerubenkan", 10437 }, { "maleki", 10438 }, { "ramstein", 10439 },
                    { "rivendare", 10440 } },
                  // The Key to the City (Magistrate Barthilas drops it): the Service Entrance Gate the run starts at and both Gauntlet
                  // Gates (175368, 175357, 175356; lock 879: the key, Lockpicking 300 or a blast). A party without it comes in by the
                  // main gate and takes it from Barthilas.
                  12382 },
            };
            for (FullRun const& f : kClassicFull)
            {
                ScenarioDef d = MakeDungeonScenario(f.row);
                d.limits.bounds = f.bounds;
                d.full_clear = true;
                d.checkpoints = f.checkpoints;
                d.carry_entry = f.carry_entry;
                d.carry_entry_b = f.carry_entry_b;
                d.goal_after_checkpoints = f.goal_after_checkpoints;
                v.push_back(std::move(d));
            }
        }
        // The Hall of Thanes (Forever, map 3065), the landing to Durgen Dirgehammer: Faldrim Anvilmar (round by the north, the straight
        // hall's Iron Doors are shut), Magmatus, Plunder, and through the Vault Door the tank opens to Durgen (the 0049 route and
        // HallOfThanesScript's order). Bot-led, at 16.
        {
            static constexpr DungeonRow kHot = { "hot_full", "Hall of Thanes: level 16 party, the landing to Durgen Dirgehammer",
                                                 3065, -95.62f, -0.16f, 510.94f, 0.f, 16, 261319, "Durgen Dirgehammer", 45 };
            ScenarioDef d = MakeDungeonScenario(kHot);
            // The route spans x -96..352, y -196..78, z 486..511; 50 y around it.
            d.limits.bounds = { true, -146.f, 402.f, -246.f, 128.f, 436.f, 561.f };
            d.full_clear = true;
            // Staging spots: crumbs of the 0049 route 33-50 y of walk before each boss (Durgen's is inside the Vault Door).
            d.checkpoints = { { "faldrim", 261306, 81.73f, 39.87f, 500.23f },
                              { "magmatus", 261316, 125.33f, -176.80f, 487.68f },
                              { "plunder", 261311, 140.69f, -34.45f, 501.99f },
                              { "durgen", 261319, 319.29f, -2.31f, 501.55f } };
            v.push_back(std::move(d));
        }
        // City of Dalaran (Forever, map 2959), the sewer landing to the Shade of the Archmage: Atrexis and Lyn in the sewers, then up
        // into the city to Mana Devourer, the three square bosses (Arcane Anomaly, Arcanic Enigma, Unstable Sentinel), Fel Ancient behind
        // the Prismatic Doors, and through the Portal To the Violet Chamber to the Shade (the 0048 route and CityOfDalaranScript's order).
        // Bot-led, at 33 (the Shade's level).
        {
            static constexpr DungeonRow kCod = { "cod_full", "City of Dalaran: level 33 party, the sewers to the Shade of the Archmage",
                                                 2959, 309.8f, 384.0f, 67.5f, 223.f, 33, 246020, "Shade of the Archmage", 75 };
            ScenarioDef d = MakeDungeonScenario(kCod);
            // The route spans x 71..456, y 176..396, z 58..295 (the Violet Chamber is at z 294); 50 y around it.
            d.limits.bounds = { true, 21.f, 506.f, 125.f, 446.f, 8.f, 345.f };
            d.full_clear = true;
            // Staging spots: crumbs of the 0048 route 30-50 y of walk before each boss; the Shade's is where the portal lands.
            d.checkpoints = { { "atrexis", 247126, 214.87f, 342.47f, 64.55f },
                              { "lyn", 247032, 236.27f, 221.78f, 64.11f },
                              { "devourer", 246008, 128.80f, 337.42f, 101.80f },
                              { "anomaly", 245999, 120.18f, 223.96f, 106.16f },
                              { "enigma", 246016, 326.36f, 198.89f, 90.80f },
                              { "sentinel", 246017, 337.11f, 240.00f, 92.02f },
                              { "felancient", 246003, 280.27f, 302.35f, 94.95f },
                              { "shade", 246020, 440.00f, 360.00f, 294.00f } };
            v.push_back(std::move(d));
        }
        // Deadmines, entrance to Edwin VanCleef: Rhahk'Zor, Sneed, Gilnid, the door and cannon, Mr. Smite, Greenskin, VanCleef. The
        // floor plan box covers the whole instance (VanCleef's ship is at y -877). Bot-led, the standard five; a run of 45 minutes.
        {
            static constexpr DungeonRow kFull = { "deadmines_full", "Deadmines: level 21 party, entrance to Edwin VanCleef",
                                                  36, -14.5732f, -385.475f, 62.4561f, 90.f, 21, 639, "Edwin VanCleef", 45 };
            ScenarioDef d = MakeDungeonScenario(kFull);
            // The floor is under the cove, not at z 0: past the Iron Clad Door the route to Mr. Smite (route chain [37]-[40], z 0.14) crosses
            // the cove's water, which the squad swims at its surface, z -0.0 over a bottom at -3 to -7 (deadmines_full 048360b9: all five
            // out_of_bounds at (-90,-707) on the route to Smite, the run otherwise a VanCleef kill). Falling through the map lands far below.
            d.limits.bounds = { true, -420.f, 100.f, -900.f, -340.f, -25.f, 110.f };
            d.full_clear = true;
            // In the order of the clear (creature entries from the Deadmines creature table: a wrong one is reported when the run starts).
            // Staging spots: waypoints of the Deadmines route chain (map 36, seq in brackets) 33-55 y of walk before each boss room.
            d.checkpoints = { { "rhahkzor", 644, -172.036f, -411.199f, 56.910f },   // [7] the corridor before the Rhahk'Zor room
                              { "sneed", 642 /* Sneed's Shredder */, -257.382f, -486.271f, 49.768f },   // [14] the tunnel before the lumber mill
                              { "gilnid", 1763, -209.837f, -568.622f, 21.462f },   // [28] the foundry floor
                              { "smite", 646, -2.585f, -766.284f, 6.172f },   // [41] the ramp up to the ship
                              { "greenskin", 647, -89.643f, -781.119f, 29.200f },   // [47] the gangway
                              { "vancleef", 639, -49.167f, -795.944f, 38.956f } };   // [50] the deck
            // The same clear with the rogue played like the owner's own: it follows the tank and hits the tank's target, nothing else.
            ScenarioDef p = d;
            p.name = "deadmines_player";
            p.summary = "Deadmines: level 21 party with a rogue played like a player, entrance to Edwin VanCleef";
            p.stand_in = "Scnrogue";
            v.push_back(std::move(d));
            v.push_back(std::move(p));
        }
        // Alcaz Prison (Forever, map 2994), the landing on the entrance stair to Blazeroar: Overseer Wrex, Katerina the Cleaner, The
        // Questioner, Tidemistress Ca'zass, Blazeroar (the route and AlcazPrisonScript's order). Bot-led, the standard five at 53.
        {
            static constexpr DungeonRow kAlcaz = { "alcaz_full", "Alcaz Prison: level 53 party, entrance to Blazeroar",
                                                   2994, -8796.8f, 2838.1f, 29.9f, 90.f, 53, 255702, "Blazeroar", 45 };
            ScenarioDef d = MakeDungeonScenario(kAlcaz);
            // The prison's creature spawns span x -8916..-8728, y 2838..3003, z 5..30; 50 y around them.
            d.limits.bounds = { true, -8966.f, -8678.f, 2788.f, 3053.f, -20.f, 80.f };
            d.full_clear = true;
            // Staging spots: crumbs of the 0043 route 30-40 y of walk before each boss.
            d.checkpoints = { { "wrex", 255391, -8762.13f, 2882.13f, 15.31f },
                              { "katerina", 257016, -8823.64f, 2889.6f, 15.58f },
                              { "questioner", 256236, -8852.4f, 2889.38f, 14.02f },
                              { "cazass", 255997, -8763.29f, 2924.71f, 6.69f },
                              { "blazeroar", 255702, -8837.6f, 2968.53f, 7.04f } };
            v.push_back(std::move(d));
        }
        // Blackmaw Hold (Forever, map 2974), the landing in the entrance tunnel to Varrox Bleakhoof: Withermaw, Bonesnout the
        // Unbroken, Impaan the Imprisoner, the Unbound Spirit (Elder Bramblesnap's ritual starts when the party comes near), the
        // Blackmaw Behemoth with Clawbert, Elder Shaman Bloodmane, Varrox (the route and BlackmawHoldScript's order). Bot-led, at 57.
        {
            static constexpr DungeonRow kBlackmaw = { "blackmaw_full", "Blackmaw Hold: level 57 party, entrance to Varrox Bleakhoof",
                                                      2974, 3470.f, 3690.f, 301.9f, 90.f, 57, 247234, "Varrox Bleakhoof", 75 };
            ScenarioDef d = MakeDungeonScenario(kBlackmaw);
            // The hold's creature spawns span x 3255..3745, y 2819..3712, z 136..361; 50 y around them.
            d.limits.bounds = { true, 3205.f, 3795.f, 2769.f, 3762.f, 86.f, 411.f };
            d.full_clear = true;
            // Staging spots: crumbs of the 0044 route 35-60 y of walk before each boss.
            d.checkpoints = { { "withermaw", 247195, 3701.07f, 3371.38f, 289.5f },
                              { "bonesnout", 247198, 3424.16f, 3379.36f, 254.1f },
                              { "impaan", 247206, 3317.33f, 3529.87f, 347.07f },
                              { "spirit", 247231, 3293.4f, 2942.53f, 210.16f },
                              { "behemoth", 247207, 3622.4f, 2892.76f, 172.65f },
                              { "bloodmane", 247209, 3521.33f, 2971.67f, 146.29f },
                              { "varrox", 247234, 3571.87f, 2869.2f, 169.49f } };
            v.push_back(std::move(d));
        }
        // The Shaper's Terrace (Forever, map 3001), the landing beside the client's trigger of the dungeon to Nanaya: Venomridge, Bolt,
        // Cinder, Tor'Rel and Izha'Rel, Snowtalon, Vitrix, Titanosaur, Thunderhoof, King Op, the Direhorn Matriarch, Nanaya (the route
        // and ShapersTerraceScript's order). Bot-led, at 59.
        {
            static constexpr DungeonRow kShapers = { "shapers_full", "The Shaper's Terrace: level 59 party, entrance to Nanaya",
                                                     3001, -5792.65f, -1252.98f, -5.95f, 75.f, 59, 261809, "Nanaya", 90 };
            ScenarioDef d = MakeDungeonScenario(kShapers);
            // The terrace's creature spawns span x -6038..-5496, y -1829..-554, z -6..113; 50 y around them.
            d.limits.bounds = { true, -6088.f, -5446.f, -1879.f, -504.f, -56.f, 163.f };
            d.full_clear = true;
            // Staging spots: crumbs of the 0045 route 35-55 y of walk before each boss.
            d.checkpoints = { { "venomridge", 261813, -5882.29f, -1233.07f, 6.88f },
                              { "bolt", 262081, -5920.0f, -1226.67f, 9.37f },
                              { "cinder", 261974, -5909.07f, -821.07f, 98.06f },
                              { "torrel", 261811, -5728.0f, -736.0f, 31.33f },
                              { "snowtalon", 262297, -5685.33f, -650.67f, 20.0f },
                              { "vitrix", 272561, -5536.91f, -820.75f, 18.66f },
                              { "titanosaur", 253201, -5664.93f, -1409.13f, 30.66f },
                              { "thunderhoof", 261812, -5983.73f, -1547.64f, 7.57f },
                              { "kingop", 261993, -5988.62f, -1808.44f, 44.53f },
                              { "direhorn", 262127, -5770.35f, -1758.24f, 21.19f },
                              { "nanaya", 261809, -5813.33f, -1589.33f, 37.33f } };
            v.push_back(std::move(d));
        }
        for (RaidBossRow const& r : kMcBosses)
            v.push_back(MakeMcScenario(r));
        v.push_back(MakeOnyScenario());
        v.push_back(MakeOnyDodgeScenario());
        v.push_back(MakeBwlRazorgoreScenario());
        v.push_back(MakeBwlVaelScenario());
        v.push_back(MakeBwlNefarianScenario());
        v.push_back(MakeBrmFollowScenario());
        v.push_back(MakeLakeSwimScenario());
        v.push_back(MakeVileReefFightScenario());
        for (ScenarioDef& d : MakeOtherRaidScenarios())
            v.push_back(std::move(d));
        // The names are built once and kept: ScenarioDef carries char const*.
        static std::deque<std::string> s_parity_names;
        for (ParityRow const& r : kParityClasses)
        {
            s_parity_names.push_back(std::string("damage_parity_") + r.cls_name);
            s_parity_names.push_back("Damage parity, " + std::string(r.cls_name) + ": up to eight legs against a level 63 dummy, per-spell outcomes, stat sensitivity, armor, DPS against the vanilla ranges");
            v.push_back(MakeParityScenario(r, s_parity_names[s_parity_names.size() - 2].c_str(), s_parity_names.back().c_str(), 1, 45));
            // The hunter of a raid was Marksmanship (Aimed Shot, Mortal Shots, Lethal Shots): the 146 and 147 runs took the class's default, Beast Mastery.
            if (r.cls == ScnClass::Hunter)
                v.back().parity_spec = 254;
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
                // A dead pet whose body has gone comes back from Revive Pet as happy as it died, and the pet spells come with the pet
                // (PetReviveCheck.h, parity value 11: the runner plays it, not Parity::Begin).
                s_parity_names.push_back("pet_revive_hunter");
                s_parity_names.push_back("Pet revive, hunter: the pet dies, its body goes, Revive Pet brings it back with the happiness and loyalty it died with; Revive/Feed/Dismiss Pet come with Tame Beast and Call Pet");
                v.push_back(MakeParityScenario(r, s_parity_names[s_parity_names.size() - 2].c_str(), s_parity_names.back().c_str(), 11, 6));
            }
            if (r.cls == ScnClass::Rogue || r.cls == ScnClass::Priest || r.cls == ScnClass::Hunter || r.cls == ScnClass::Druid)
            {
                s_parity_names.push_back(std::string("threat_audit_") + r.cls_name);
                s_parity_names.push_back("Threat audit, " + std::string(r.cls_name) + ": the threat reducer (Feint, Fade, Disengage, Cower) cast with 500 and 5000 threat on a level 63 dummy, the drop measured against vanilla's");
                v.push_back(MakeParityScenario(r, s_parity_names[s_parity_names.size() - 2].c_str(), s_parity_names.back().c_str(), 7, 6));
            }
            // The player spell check: every key spell of the class cast the way a player casts it, each must land and hit for at least its floor
            // and half of what the client's spell data says (Parity::Kind::SpellCheck). Runs on the server alone, no game client.
            s_parity_names.push_back(std::string("spell_check_") + r.cls_name);
            s_parity_names.push_back("Player spell check, " + std::string(r.cls_name) + ": each key spell cast at a level 63 dummy (heals on itself), PASS / FAIL / SKIP per spell against a floor and the client's spell data");
            v.push_back(MakeParityScenario(r, s_parity_names[s_parity_names.size() - 2].c_str(), s_parity_names.back().c_str(), 10, 15));
            s_parity_names.push_back(std::string("talent_audit_") + r.cls_name);
            s_parity_names.push_back("Talent audit, " + std::string(r.cls_name) + ": key talents in the spellbook with their effects" +
                                     (r.cls == ScnClass::Warlock ? ", Improved Shadow Bolt +20 % after a crit" : ""));
            v.push_back(MakeParityScenario(r, s_parity_names[s_parity_names.size() - 2].c_str(), s_parity_names.back().c_str(), 3, 10));
        }
        // The other specializations of the classes that play one spec in the base scenario (warrior Arms, mage Frost, warlock Affliction/Demonology).
        for (ScenarioDef const& base : std::vector<ScenarioDef>(v))
        {
            struct SpecVariant { char const* cls; char const* suffix; std::uint32_t spec; char const* label; };
            static constexpr SpecVariant kVariants[] = {
                { "warrior", "fury", 72u, "Fury" }, { "warrior", "prot", 73u, "Protection" },
                { "mage", "fire", 63u, "Fire" }, { "mage", "arcane", 62u, "Arcane" },
                { "warlock", "affliction", 265u, "Affliction" }, { "warlock", "demonology", 266u, "Demonology" }, { "warlock", "destruction", 267u, "Destruction" },
                { "druid", "cat", 103u, "Feral (cat)" }, { "shaman", "elemental", 262u, "Elemental" },
            };
            for (SpecVariant const& variant : kVariants)
            {
                if (std::string(base.name) != std::string("damage_parity_") + variant.cls)
                    continue;
                static std::deque<std::string> s_spec_names;
                s_spec_names.push_back(std::string("damage_parity_") + variant.cls + "_" + variant.suffix);
                s_spec_names.push_back(std::string("Damage parity, ") + variant.cls + " played as " + variant.label + ": up to eight legs against a level 63 dummy, per-spell outcomes");
                ScenarioDef d = base;
                d.name = s_spec_names[s_spec_names.size() - 2].c_str();
                d.summary = s_spec_names.back().c_str();
                d.parity_spec = variant.spec;
                v.push_back(std::move(d));
            }
        }
        // The base mage and warlock scenarios name their spec too: with parity_spec 0 the run kept whatever talents the character held, and after a
        // fire run the "frost" mage was a Fire mage (166: Fireball and Scorch cast, "never got Frostbolt accepted").
        for (ScenarioDef& d : v)
        {
            if (std::string(d.name) == "damage_parity_mage") d.parity_spec = 64u;
            if (std::string(d.name) == "damage_parity_warlock") d.parity_spec = 265u;
            if (std::string(d.name) == "damage_parity_shaman") d.parity_spec = 263u;   // an elemental run left its talents on the character (195: the base ran as a caster)
            // The rest of the base scenarios, pinned to the spec their reference ranges were written for, so no run depends on the talents the last run left.
            if (std::string(d.name) == "damage_parity_paladin") d.parity_spec = 70u;    // Retribution
            if (std::string(d.name) == "damage_parity_rogue") d.parity_spec = 260u;     // Assassination, the build the runs have played
            if (std::string(d.name) == "damage_parity_priest") d.parity_spec = 258u;    // Shadow
            if (std::string(d.name) == "damage_parity_druid") d.parity_spec = 102u;     // Balance (the cat is damage_parity_druid_cat)
        }
        v.push_back(MakeScorecardScenario());
        static std::deque<std::string> s_support_names;   // ScenarioDef carries char const*
        for (SupportTank const& t : kSupportTanks)
        {
            s_support_names.push_back(std::string("support_scorecard_") + t.suffix);
            s_support_names.push_back(std::string("Healer and tank scorecard: one Tier 3 raid with a ") + t.suffix + " tank, five healers and four dps on a boss that hits the tank and pulses the raid; effective healing, overheal, mana, threat and damage taken per bot in DpsScorecard.tsv");
            v.push_back(MakeSupportScenario(t, s_support_names[s_support_names.size() - 2].c_str(), s_support_names.back().c_str()));
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
