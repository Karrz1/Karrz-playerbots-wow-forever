// AlteracValleyScript — Alterac Valley (BattlemasterList id 1 / BATTLEGROUND_AV, map 30).
// 40v40 reinforcement push. Decided by towers / captains / end-boss, not
// zerg: each tower destroyed drains 75 reinforcements; each captain kill
// gives +100 reinforcements to the killing team; killing Drek'thar /
// Vandar Stormpike ends the BG.
//
// Authoritative data from V1 (production-tested):
//   src/modules/Playerbot/bot logic/Coordination/Battleground/Scripts/Siege/
//     AlteracValleyData.h:35 (REINF_GAIN_PER_CAPTAIN), :128-130 (Vandar),
//     :134-136 (Drek'Thar), :149-164 (Captains), :191+ (towers),
//     :451-457 (spawns).

#include "../BattlegroundScript.h"
#include "../../BotSnapshotView.h"
#include "../BgAvPlan.h"

namespace Playerbot {

namespace {

class AlteracValleyScript final : public BattlegroundScript
{
public:
    uint16_t bg_type_id() const override { return 1; }  // BATTLEGROUND_AV
    char const* name() const override { return "alterac_valley"; }

    BattlegroundAdvice get_advice(BotSnapshotView const& s) const override
    {
        BattlegroundAdvice a;
        // 40 slots. AV is decided by reinforcement bleed (kills + tower
        // destroys + captain kills) — not zerg. Real AV runs ~6-8
        // healers per side; 8 healer slots ensure statistical coverage
        // even after class-fit fallback (a non-healer-spec bot in a
        // Healer slot demotes to Roamer via the class-fixup rule).
        //   * 18 Attackers — front-line push.
        //   * 6  Defenders — home keep + forward GY anchor + tower defense.
        //   * 8  Roamers   — counter-flips + tower defense rotation.
        //   * 8  Healers   — split across the line.
        a.role_by_slot.assign(40, BgRole::Free);
        for (uint8_t i = 0;  i < 18; ++i) a.role_by_slot[i] = BgRole::Attacker;
        for (uint8_t i = 18; i < 24; ++i) a.role_by_slot[i] = BgRole::Defender;
        for (uint8_t i = 24; i < 32; ++i) a.role_by_slot[i] = BgRole::Roamer;
        for (uint8_t i = 32; i < 40; ++i) a.role_by_slot[i] = BgRole::Healer;
        // AV banners are GO type 1 (BUTTON) / type 10 (GOOBER) — there are
        // NO type-42/24 GOs on map 30 (audit B26), so node interaction was
        // impossible. Enumerate the DB-verified banner entries instead
        // (every per-state Alliance/Horde/Contested variant on map 30). The
        // "[PH] ... Tower Banner BIG" / "Pre-Banner BIG" objects on the tower
        // tops are decorations (no lock, no script): clicking one does nothing,
        // and the AV 247 soak spent 103 captures on 178927, so they are not here.
        a.auto_use_go_entries = {
            178364, 178365, 178388, 178389, 178393, 178394, 178925, 178929,
            178935, 178936, 178940, 178943, 178944, 178945, 178946, 179284,
            179285, 179286, 179287, 179304, 179305, 179308, 179310, 179435,
            179439, 179441, 179443, 179445, 179449, 179453, 179465, 179466,
            179467, 179468, 179470, 179471, 179472, 179473, 179481, 179482,
            179483, 179484, 180418, 180419, 180420,
        };

        // Endgame targets + node table rebuilt from DB-verified spawns
        // (audit B27: the old V1-attributed coords were corrupted — the
        // Horde "Vanndar" target was DREK'THAR'S OWN ROOM, Alliance
        // structures sat inside the Horde base, and both captains were
        // ~120-230y off; ~70% of objective movement was mis-targeted).
        // world.creature: Vanndar 11948 (722.4,-11.0,50.7); Drek'Thar
        // 11946 (-1370.9,-220.2,98.5); Balinda 11949 (-57.8,-286.6,15.6);
        // Galvangar 11947 (-545.2,-165.4,57.8). Node coords = banner GO
        // spawn clusters on map 30.
        if (s.is_horde())
        {
            // Horde start = world_safe_locs 610 (world SQL 2026_10_25_95): behind the Horde gate. The old (-1437,-610,51) was inside the hill
            // in the 70532 map 30 (no geometry, no navmesh), so no Horde bot ever left it.
            a.home_base_x = -890.00f; a.home_base_y = -575.00f; a.home_base_z = 70.24f;
            // Endgame: Vanndar Stormpike in Dun Baldar (NORTH end, +X).
            a.endgame_target_x = 722.4f;
            a.endgame_target_y = -11.0f;
            a.endgame_target_z =  50.7f;
            a.endgame_creature_entry = 11948;  // VANNDAR_ENTRY
        }
        else
        {
            // Alliance spawn (V1 ALLIANCE_SPAWNS[0]).
            a.home_base_x = 873.98f; a.home_base_y = -491.79f; a.home_base_z = 96.54f;
            // Endgame: Drek'Thar in Frostwolf Keep (SOUTH end, -X).
            a.endgame_target_x = -1370.9f;
            a.endgame_target_y =  -220.2f;
            a.endgame_target_z =    98.5f;
            a.endgame_creature_entry = 11946;  // DREKTHAR_ENTRY
        }
        // Captain staging (ike3 BattleGroundTacticsAV.cpp, Karrz1/wow-1.12 raid10-revamp bcc8b9fe, named locations
        // AV_ICEBLOOD_GARRISON_WAITING_ALLIANCE / AV_STONEHEART_OUTPOST_WAITING_HORDE): the squad for the enemy captain gathers here and
        // goes in together (BgTeamCoordinator::PlanNodeRace), not one bot after another. The Alliance hunts Galvangar, the Horde Balinda.
        // Horde: not ike3's (28.1,-302.6), which lies east of Balinda inside Stonehearth Outpost (an Alliance Sentinel 3 yd away,
        // Lieutenant Stouthandle 11 yd): coming from the west every bot had to pass her and her guards to get there, and in bot-only
        // AV 261 the Horde squad did not gather for 51 minutes (first assault 00:17, "5 at the waiting spot, squad 31"). This is the
        // recorded river road node 115 yd west of her, on the way in from the Horde base, nearest creature 58 yd.
        if (s.is_horde())
        { a.captain_stage_x = -171.3f;  a.captain_stage_y = -266.6f;  a.captain_stage_z = 9.1f; }
        else
        { a.captain_stage_x = -492.17f;  a.captain_stage_y = -187.077f;  a.captain_stage_z = 57.1342f; }
        // ike3 only goes for the general once every enemy tower / bunker is down (they carry the general's buffs).
        a.endgame_needs_towers = true;
        // Mines (ike3 mine-boss objective, BgAvPlan.h): the boss of every mine the team does not own.
        for (auto const& m : BgAvPlan::MineTargets(s.is_horde() ? 2 : 1, s.bg_av_mine_owner_north(), s.bg_av_mine_owner_south()))
            a.kill_targets.push_back({ m.x, m.y, m.z, m.entry, m.name });
        // Open-field fight zone: Snowfall GY, the neutral graveyard in the middle of the valley where
        // the two armies meet (same coordinates as the node below, so the z is a known-good banner
        // height). BgTeamCoordinator sends a per-match random share of each side here (see PlanNodeRace).
        a.fight_zone_x = -202.5f; a.fight_zone_y = -113.0f; a.fight_zone_z = 78.0f;
        // Non-deficit endgame trigger (BG audit AV). The generic endgame
        // redirect only fires under Bias_AllIn (own reinforcements >=200
        // behind), so a LEADING or TIED team never boss-rushed and AV dragged
        // to the reinforcement floor. AV reinforcements map into the bg score;
        // when the ENEMY drops below ~200 of 600 its towers are mostly down
        // (-75 each) so its general has shed its tower buffs and is killable —
        // push the boss to SEAL the win instead of trickling reinforcements.
        // (>0 guard avoids firing on an un-harvested 0 before the match warms.)
        {
            const uint32 enemy_reinf =
                s.is_horde() ? s.bg_score_alliance() : s.bg_score_horde();
            if (enemy_reinf > 0 && enemy_reinf < 200)
                a.endgame_unconditional = true;
        }
        // 15 static nodes: 7 GYs (p=0) + 8 towers/bunkers (p=2, -75 reinf
        // drain each); captains appended below (p=3, +100 reinf for the
        // killer). All coords = banner-GO spawn positions from world DB.
        a.nodes = {
            // Graveyards — priority 0 (rez logistics only).
            {   669.0f, -294.0f,  30.0f, "Stormpike GY",            0 },
            {   638.5f,  -31.5f,  46.0f, "Stormpike Aid Station",   0 },
            {    77.8f, -404.7f,  46.9f, "Stonehearth GY",          0 },
            {  -202.5f, -113.0f,  78.0f, "Snowfall GY (neutral)",   0 },
            {  -612.5f, -397.0f,  61.0f, "Iceblood GY",             0 },
            { -1082.5f, -347.0f,  55.0f, "Frostwolf GY",            0 },
            { -1402.0f, -307.0f,  89.0f, "Frostwolf Relief Hut",    0 },
            // Alliance bunkers — priority 2 (-75 reinf on destruction).
            // South is the lower-X bunker (the classic map script's node
            // table, VMaNGOS BG_AV_Nodes against its banner spawns).
            {   553.8f,  -78.7f,  51.9f, "Dun Baldar South Bunker", 2 },
            {   674.0f, -143.1f,  63.7f, "Dun Baldar North Bunker", 2 },
            {   203.0f, -361.0f,  56.0f, "Icewing Bunker",          2 },
            {  -152.4f, -441.6f,  40.4f, "Stonehearth Bunker",      2 },
            // Horde towers — priority 2.
            // Labels corrected (BG audit §2): the DB captain garrisons prove
            // the names were reversed — Commander Dardosh (13140) garrisons
            // the (-572,-263) tower = ICEBLOOD; Wing Commander Slidore (13438)
            // garrisons the (-768,-363) tower = TOWER POINT.
            {  -572.0f, -263.0f,  75.0f, "Iceblood Tower",          2 },
            {  -768.0f, -363.0f,  91.0f, "Tower Point",             2 },
            { -1303.0f, -317.0f, 114.0f, "East Frostwolf Tower",    2 },
            { -1298.0f, -267.0f, 114.0f, "West Frostwolf Tower",    2 },
        };
        // Captains — priority 3 (+100 reinforcements for the killing
        // team). Permanently-killable: drop from the node list once the
        // snapshot reports the captain dead so bots stop pathing to a
        // corpse. Coords = actual creature spawns (the old values
        // duplicated bunker/GY coords ~230y away).
        if (s.bg_av_balinda_alive())
            a.nodes.push_back({  -57.8f, -286.6f, 15.6f, "Balinda Stonecaster (A)", 3 });
        if (s.bg_av_galvangar_alive())
            a.nodes.push_back({ -545.2f, -165.4f, 57.8f, "Captain Galvangar (H)", 3 });
        return a;
    }
};

} // anonymous

std::unique_ptr<BattlegroundScript> MakeAlteracValleyScript()
{
    return std::make_unique<AlteracValleyScript>();
}

} // namespace Playerbot
