// Alterac Valley objective rules imported from ike3's bots (Karrz1/wow-1.12 raid10-revamp bcc8b9fe70c42a8fa5d50fa83a472da9e9d19546,
// BattleGroundTacticsAV.cpp): the mine-boss objective and the captain gathering threshold. Pure functions, no game state, so they can be
// tested offline (tests/game/BgAvPlan.cpp). The mid-field fighting tactics are ours and do not live here.
#pragma once

#include <cstdint>
#include <vector>

namespace Playerbot::BgAvPlan
{
    // The captain squad goes in once this many of the team stand at the waiting spot (ike3: attackers near the waiting spot >= 5).
    constexpr int kCaptainGatherCount = 5;
    // Time cap (playtest, AV 2026-10-11: a pile of bots waited at the Stonehearth staging spot for a quorum): after kCaptainGatherMaxMs of waiting the squad
    // goes with whoever is there (at least one), so bots arriving one at a time never park for good.
    constexpr uint32_t kCaptainGatherMaxMs = 75000;
    // `squad` = the bots the plan sends at the captain (0 = unknown: the plain five-bot rule). A big squad waits for two thirds of itself
    // (bot-only AV 249: the squad went in whenever five stood at the waiting spot while the rest were still on the road, and died in
    // turn); the time cap still lets a small or straggling squad go, but a known squad goes on the cap only with five at the spot (or all
    // of a squad smaller than five): in AV 251 every assault went on the cap with 1 or 2 of a squad of 23 to 31, because the bots coming
    // back from the graveyards need longer than the cap, and the "wipes" of those one-bot assaults gave the captain up in 6 minutes.
    // `since_arrival_ms` = how long the count at the spot has not grown: the time cap goes only once the squad stopped arriving. AV 253:
    // all six captain assaults went "5 at the waiting spot" of a squad of 23 to 31, never on the quorum, because the cap counts from the
    // last wipe and had run out long before; the fifth bot to arrive set off the assault while the rest were still on the road.
    constexpr uint32_t kGatherSettleMs = 20000;
    constexpr bool CaptainGather(int at_stage, bool latched, uint32_t waited_ms = 0, int squad = 0, uint32_t since_arrival_ms = kGatherSettleMs)
    {
        int const quorum = squad * 2 / 3 > kCaptainGatherCount ? squad * 2 / 3 : kCaptainGatherCount;
        int const cap_min = squad <= 0 ? 1 : (squad < kCaptainGatherCount ? squad : kCaptainGatherCount);
        return latched || at_stage >= quorum ||
               (at_stage >= cap_min && waited_ms >= kCaptainGatherMaxMs && since_arrival_ms >= kGatherSettleMs);
    }
    // ike3 sends the non-supporters (60-80 %) at the captain, mod-playerbots 90 %. The supporters (garrisons, the valley brawl, Snowfall,
    // mine bosses) may only take what is left over: in AV 249 they took them first and the captain squad was whoever remained, a handful
    // of bots against the captain, two lieutenants and the garrison.
    constexpr int CaptainReserve(int alive_free) { return (alive_free * 65 + 99) / 100; }
    // A captain the team cannot kill must not hold the match: after kCaptainMaxAssaults wiped assaults the team leaves the captain and
    // goes for the towers and the general (the general push needs the towers, not the captain).
    constexpr int kCaptainMaxAssaults = 3;
    constexpr bool CaptainGivenUp(int wiped_assaults) { return wiped_assaults >= kCaptainMaxAssaults; }
    // The assault that went in is over once its squad is gone (bot-only AV soak 2026-10-25: the latch held until the captain died, so after
    // the first wipe every bot back from the graveyard ran into Iceblood Garrison alone; ~350 deaths there with 30 of 37 alive, Galvangar
    // never died and the towers, which come after him, were never attacked). Fewer than kCaptainSquadMin alive bots within
    // kCaptainSquadRadius of the captain, kCaptainAssaultGraceMs after the go (the walk in from the waiting spot, 57 yd at Iceblood): the
    // team gathers at the waiting spot again.
    constexpr int kCaptainSquadMin = 2;
    constexpr float kCaptainSquadRadius = 100.0f;
    constexpr uint32_t kCaptainAssaultGraceMs = 30000;
    constexpr bool CaptainAssaultBroken(int alive_near_captain, uint32_t since_go_ms, uint32_t grace_ms = kCaptainAssaultGraceMs)
    {
        return since_go_ms >= grace_ms && alive_near_captain < kCaptainSquadMin;
    }
    // A squad member still on its way in counts as the assault too: it has left the waiting spot toward the target (`to_target` yards
    // from it, the spot `stage_to_target`) by more than kOnWayMargin. Bot-only AV 262: the general squad went from Snowfall at 02:11:03
    // and 02:18:09 and was "wiped" 6 min 15 s later both times, the grace of the 1,200 yd straight line, while it was still on the
    // road; 51 s after the first "wipe" 17 stood at the waiting spot again (back from the road, not from the graveyard).
    constexpr float kOnWayMargin = 30.0f;
    constexpr bool OnTheWay(float to_target, float stage_to_target) { return to_target < stage_to_target - kOnWayMargin; }
    // Waves. A bot that died after its assault went (`died_ms`, match time of the last plan that saw it dead; `died` = ever seen dead) is
    // out of that wave: it goes back to the waiting spot instead of running in alone, and does not keep the wave from counting as wiped.
    // Once kCaptainGatherCount of them stand at the spot and no more have come for kGatherSettleMs they go in as the next wave.
    // Bot-only AV 268: after the graveyard steps the two general squads met between Snowfall and Stonehearth for 1 h 35 min; the
    // Alliance assault went at 10:37 and never counted as wiped (its dead came back on the road from Stonehearth Graveyard and counted
    // as on their way), the Horde's held from 11:16 to 12:14 the same way. The bots ran back in one by one (a death there had 0.7
    // teammates within 45 yd on average in the sampled role log), and none got within 50 yd of the graveyard it was sent to.
    constexpr bool OutOfWave(bool died, uint32_t died_ms, uint32_t wave_ms) { return died && died_ms >= wave_ms; }
    constexpr bool WaveReinforce(int at_stage, uint32_t since_arrival_ms)
    {
        return at_stage >= kCaptainGatherCount && since_arrival_ms >= kGatherSettleMs;
    }
    // The grace for a walk of `stage_to_target` yards from the waiting spot (the node rally: Iceblood GY is 240 yd from the Alliance
    // waiting spot): 4 yd/s on foot plus 10 s, never less than kCaptainAssaultGraceMs.
    constexpr uint32_t AssaultGraceMs(float stage_to_target)
    {
        uint32_t const walk = uint32_t(stage_to_target > 0.f ? stage_to_target : 0.f) * 250u + 10000u;
        return walk > kCaptainAssaultGraceMs ? walk : kCaptainAssaultGraceMs;
    }
    // Node rally (bot-only AV 251: once the captain was left, the bots ran from Stonehearth GY to Iceblood one by one, about 650 yd past
    // the Winterax trolls, Korrak and the garrison, and the Alliance took no Horde banner in 45 minutes): a bot this far beyond the
    // waiting spot's own distance to the target goes to the waiting spot first; the rest go straight in.
    constexpr float kRallyDetour = 50.0f;
    // The node squad's waiting spot: this far (straight line) back from the first node along the recorded route in from the home base,
    // clear of the tower's bowmen and commander (BgRoutes::ApproachPoint; Stonehearth Bunker's Randolph and his bowmen stand within 25 yd).
    constexpr float kNodeRallyBack = 100.0f;
    constexpr bool RallyFirst(float bot_to_target, float stage_to_target) { return bot_to_target > stage_to_target + kRallyDetour; }

    struct MineTarget { float x, y, z; uint32_t entry; char const* name; };

    // Mine owner world states (801 Irondeep = north, 804 Coldtooth = south): 0 neutral, 1 Alliance, 2 Horde, 255 unread.
    // `team` is 1 = Alliance, 2 = Horde. Killing the boss of a mine the team does not own takes it: a neutral mine is held by its trogg /
    // kobold boss (Morloch / Taskmaster Snivvle), an enemy-held one by that team's boss (Alliance Umi Thorson / Agi Rumblestomp, Horde
    // Keetar / Masha Swiftcut). Positions are world.creature spawns; both sides' bosses of a mine spawn at the same spot.
    inline std::vector<MineTarget> MineTargets(uint8_t team, uint8_t north, uint8_t south)
    {
        std::vector<MineTarget> out;
        if (team != 1 && team != 2) return out;
        if (north != 255 && north != team && north <= 2)
            out.push_back({ 863.838f, -445.468f, 50.846f, north == 0 ? 11657u : (team == 1 ? 13079u : 13078u), "Irondeep Mine boss" });
        if (south != 255 && south != team && south <= 2)
            out.push_back({ -848.902f, -92.931f, 68.633f, south == 0 ? 11677u : (team == 1 ? 13088u : 13086u), "Coldtooth Mine boss" });
        return out;
    }

    // ---- AV quests (ike3 BattleGroundTacticsAV.cpp: the AV quester role; quest data from the VMaNGOS import, NPC spawns from
    // world.creature) ----
    // A small share of the team (IsQuester) works the Armor Scraps quest and the two mine-supply quests instead of walking the front the
    // whole match. Turn in what is complete, accept what can be done, fetch mine supplies from a mine the team owns. Armor scraps drop
    // from kills (the quest collects them while the bot fights); riders, wing commanders, beacons and the world-boss turn-in are not
    // here.
    constexpr uint32_t kScrapsItem = 17422, kIrondeepSupplies = 17522, kColdtoothSupplies = 17542;
    constexpr uint32_t kSuppliesNeeded = 10;
    constexpr uint32_t kMineGoNorth = 178785, kMineGoSouth = 178784;

    // One bot in eight is a quester (ike3: guid % 5 for the 51-59 bracket).
    constexpr bool IsQuester(uint64_t guid_low) { return guid_low % 8u == 0; }

    struct AvPlace { uint32_t entry; float x, y, z; };
    struct TeamQuests
    {
        uint32_t scraps_first, scraps_repeat, mine_north, mine_south;   // quest ids
        AvPlace armorer, quartermaster;                                    // quest NPCs (give and take every quest above)
        AvPlace go_north, go_south;                                        // the mine supply objects (Irondeep N, Coldtooth S)
    };
    // Alliance: scraps 7223 then 6781 (Murgot Deepforge), Irondeep 5892 / Coldtooth 6982 (Stormpike Quartermaster). Horde: scraps 7224
    // then 6741 (Smith Regzar), Coldtooth 5893 / Irondeep 6985 (Frostwolf Quartermaster).
    inline TeamQuests const& QuestsFor(uint8_t team)
    {
        static TeamQuests const alliance = { 7223, 6781, 5892, 6982, { 13257, 647.61f, -61.1548f, 41.7405f }, { 12096, 587.633f, -45.9816f, 37.5438f },
                                             { kMineGoNorth, 881.273f, -442.002f, 54.664f }, { kMineGoSouth, -853.671f, -91.427f, 68.569f } };
        static TeamQuests const horde    = { 7224, 6741, 6985, 5893, { 13176, -1251.5f, -316.327f, 62.6565f }, { 12097, -1293.79f, -194.407f, 72.4398f },
                                             { kMineGoNorth, 881.273f, -442.002f, 54.664f }, { kMineGoSouth, -853.671f, -91.427f, 68.569f } };
        return team == 2 ? horde : alliance;
    }
    // Every quest id the planner looks at, for the snapshot's rewarded mask (bit i = kQuestIds[i] was handed in once).
    constexpr uint32_t kQuestIds[8] = { 7223, 6781, 5892, 6982, 7224, 6741, 5893, 6985 };
    constexpr uint32_t QuestBit(uint32_t quest_id)
    {
        for (uint32_t i = 0; i < 8; ++i) if (kQuestIds[i] == quest_id) return 1u << i;
        return 0;
    }

    enum class AvQStatus : uint8_t { None, Incomplete, Complete, Rewarded };
    struct QuestInput
    {
        uint8_t team = 1;                        // 1 Alliance, 2 Horde
        uint8_t mine_owner_north = 255, mine_owner_south = 255;
        AvQStatus scraps_first = AvQStatus::None, scraps_repeat = AvQStatus::None, mine_north = AvQStatus::None, mine_south = AvQStatus::None;
        uint32_t supplies_north = 0, supplies_south = 0;   // Irondeep / Coldtooth supply items in the bags
    };
    enum class StepKind : uint8_t { None, TurnIn, Accept, UseMineGo };
    struct AvStep { StepKind kind = StepKind::None; uint32_t quest_id = 0; AvPlace place{0, 0, 0, 0}; };

    // The next thing the quester does; None = back to the fight. Mine quests are only taken and worked while the team owns that mine
    // (a mine the enemy holds gives no supplies); the scraps quest is kept running all match (first quest once, then the repeat one).
    inline AvStep NextStep(QuestInput const& in)
    {
        TeamQuests const& q = QuestsFor(in.team);
        // turn in
        if (in.scraps_first == AvQStatus::Complete)  return { StepKind::TurnIn, q.scraps_first,  q.armorer };
        if (in.scraps_repeat == AvQStatus::Complete) return { StepKind::TurnIn, q.scraps_repeat, q.armorer };
        if (in.mine_north == AvQStatus::Complete)    return { StepKind::TurnIn, q.mine_north,    q.quartermaster };
        if (in.mine_south == AvQStatus::Complete)    return { StepKind::TurnIn, q.mine_south,    q.quartermaster };
        // accept
        if (in.scraps_first == AvQStatus::None)
            return { StepKind::Accept, q.scraps_first, q.armorer };
        if (in.scraps_first == AvQStatus::Rewarded && in.scraps_repeat == AvQStatus::None)
            return { StepKind::Accept, q.scraps_repeat, q.armorer };
        bool const own_n = in.mine_owner_north == in.team, own_s = in.mine_owner_south == in.team;
        if (own_n && in.mine_north == AvQStatus::None) return { StepKind::Accept, q.mine_north, q.quartermaster };
        if (own_s && in.mine_south == AvQStatus::None) return { StepKind::Accept, q.mine_south, q.quartermaster };
        // fetch supplies: the quest is open, the bags are short, the mine is ours
        if (own_n && in.mine_north == AvQStatus::Incomplete && in.supplies_north < kSuppliesNeeded)
            return { StepKind::UseMineGo, q.mine_north, q.go_north };
        if (own_s && in.mine_south == AvQStatus::Incomplete && in.supplies_south < kSuppliesNeeded)
            return { StepKind::UseMineGo, q.mine_south, q.go_south };
        return {};
    }

    // ---- Attack order (ike3 BattleGroundTacticsAV.cpp attack lists; mod-playerbots AV_AttackObjectives_*: "first N non-owned, non-destroyed
    // entries", N = 2 balanced) ----
    // Both reference bots walk an ordered list of the enemy's nodes, graveyards INCLUDED: a graveyard held by the enemy sends every dead
    // bot of the team back to the far end of the valley, so taking it early matters as much as a tower. We had only towers and bunkers.
    struct AvListNode { float x, y; char const* name; };
    // Horde: Stonehearth GY, Stonehearth Bunker, Icewing Bunker, Stormpike GY, Dun Baldar South, Dun Baldar North, Stormpike Aid Station.
    inline std::vector<AvListNode> const& AttackList(uint8_t team)
    {
        static std::vector<AvListNode> const horde = {
            { 77.8f, -404.7f, "Stonehearth GY" }, { -152.4f, -441.6f, "Stonehearth Bunker" }, { 203.0f, -361.0f, "Icewing Bunker" },
            { 669.0f, -294.0f, "Stormpike GY" }, { 553.8f, -78.7f, "Dun Baldar South Bunker" }, { 674.0f, -143.1f, "Dun Baldar North Bunker" },
            { 638.5f, -31.5f, "Stormpike Aid Station" } };
        // Alliance: Iceblood GY, Iceblood Tower, Tower Point, Frostwolf GY, East / West Frostwolf Tower, Frostwolf Relief Hut.
        static std::vector<AvListNode> const alliance = {
            { -612.5f, -397.0f, "Iceblood GY" }, { -572.0f, -263.0f, "Iceblood Tower" }, { -768.0f, -363.0f, "Tower Point" },
            { -1082.5f, -347.0f, "Frostwolf GY" }, { -1303.0f, -317.0f, "East Frostwolf Tower" }, { -1298.0f, -267.0f, "West Frostwolf Tower" },
            { -1402.0f, -307.0f, "Frostwolf Relief Hut" } };
        return team == 2 ? horde : alliance;
    }

    struct AvNodeView { float x, y; bool ours; bool destroyed; bool enemy_flipping; };

    // Indices into `nodes` of the first `window` list entries that are still worth attacking: present, not razed, not ours, and not
    // being flipped by the enemy (the defence pass owns those). Matches a list entry to a live node by position (30 yd).
    inline std::vector<int> PickAttackNodes(uint8_t team, std::vector<AvNodeView> const& nodes, int window)
    {
        std::vector<int> out;
        for (AvListNode const& e : AttackList(team))
        {
            if (int(out.size()) >= window) break;
            for (int i = 0; i < int(nodes.size()); ++i)
            {
                float const dx = nodes[i].x - e.x, dy = nodes[i].y - e.y;
                if (dx * dx + dy * dy > 30.0f * 30.0f) continue;
                if (nodes[i].destroyed || nodes[i].ours || nodes[i].enemy_flipping) break;
                out.push_back(i);
                break;
            }
        }
        return out;
    }

    // Snowfall Graveyard (the neutral one in the middle of the valley, -202.5,-113): ike3 sends its "supporters" there while it is not
    // ours, 40 % of the team in the first ten minutes ("strife") and 20 % afterwards; mod-playerbots sends roles under 6 (Alliance) / 5
    // (Horde) of 10. Returns how many of `free_bots` go.
    constexpr float kSnowfallX = -202.5f, kSnowfallY = -113.0f;
    inline int SnowfallDetail(int free_bots, uint32_t in_progress_ms)
    {
        if (free_bots < 3) return 0;
        int const share = in_progress_ms < 10u * 60u * 1000u ? 40 : 20;
        int const n = (free_bots * share) / 100;
        return n < 1 ? 1 : n;
    }

    // ---- Rules from ike3 / mod-playerbots AV that the planner uses (Karrz1/wow-1.12 raid10-revamp BattleGroundTacticsAV.cpp, mod-playerbots
    // BattleGroundTactics.cpp selectObjective) ----
    // A defender already deep in the enemy half becomes an attacker: x >= -62 for the Horde, x <= -462 for the Alliance (AV runs from the
    // Frostwolf end at x -1400 to Dun Baldar at x +700); with chance 20 + 15 per own node razed (mod-playerbots BGT 1919-1931).
    constexpr bool DefenderPastLine(uint8_t team, float x) { return team == 2 ? x >= -62.0f : x <= -462.0f; }
    constexpr int  DefenderFlipChance(int own_razed) { return 20 + 15 * own_razed; }

    // The general (Drek'Thar / Vanndar). ike3: only once every enemy tower and bunker is down. mod-playerbots: two or more down, and either
    // the team holds its last graveyard (Frostwolf Relief Hut / Stormpike Aid Station) or 20 or more of its players stand within 200 yd of
    // the boss. Either one opens the push.
    constexpr int kBossFriendsNeeded = 20;
    constexpr bool GeneralPushOk(int enemy_towers_up, int enemy_towers_razed, bool own_final_gy, int friends_near_boss)
    {
        return enemy_towers_up == 0 || (enemy_towers_razed >= 2 && (own_final_gy || friends_near_boss >= kBossFriendsNeeded));
    }
    // Classic AV ends only on a general kill, and in bot-only matches the towers do not fall: in the soaks of 2026-10-25 (247 to 259) the
    // towers were taken 4 to 9 times a match and every one was won back by its garrison before it burned, so GeneralPushOk never held
    // and every match ran into the time limit with both captains dead (257: 1314 / 1344 s, 259: Balinda 912 s). The towers get
    // kGeneralAfterCaptainMs after the enemy captain died or was left; then the team goes for the general (his warmasters / marshals
    // for the standing towers are part of that fight).
    constexpr uint32_t kGeneralAfterCaptainMs = 10u * 60u * 1000u;
    constexpr bool GeneralDue(uint32_t in_progress_ms, uint32_t captain_done_ms)
    {
        return in_progress_ms >= captain_done_ms && in_progress_ms - captain_done_ms >= kGeneralAfterCaptainMs;
    }
    // The general squad's waiting spot: our graveyard nearest the enemy general (held, not under attack), where the bots respawn anyway,
    // so a wiped push regroups where its dead come back. Bot-only AV 261: a spot 200 yd short of the general (below Frostwolf Keep) was
    // never reached: the push fired at 23:52, the Alliance walked to it one by one, and in an hour none got past Iceblood, where they
    // died to the Horde bots (0 at the spot, no "general assault goes"). `gy` = graveyard nodes, -1 when we hold none (the caller then
    // falls back to kGeneralStageBack along the route in).
    struct GyView { float x, y; bool ours; bool contested; };
    inline int GeneralStageGraveyard(std::vector<GyView> const& gy, float general_x, float general_y)
    {
        int best = -1;
        float best_d = 0.f;
        for (int i = 0; i < int(gy.size()); ++i)
        {
            if (!gy[i].ours || gy[i].contested) continue;
            float const dx = gy[i].x - general_x, dy = gy[i].y - general_y, d = dx * dx + dy * dy;
            if (best < 0 || d < best_d) { best = i; best_d = d; }
        }
        return best;
    }
    constexpr float kGeneralStageBack = 200.0f;
    // Our last graveyard (the one an AV team keeps until the end).
    struct AvPoint { float x, y, z; };
    constexpr AvPoint FinalGraveyard(uint8_t team) { return team == 2 ? AvPoint{ -1402.0f, -307.0f, 89.0f } : AvPoint{ 638.5f, -31.5f, 46.0f }; }

    // The graveyard the general push takes before it goes on: of the graveyards nearer the general than our waiting spot (`stage`, an
    // index into `gy`, or -1 for the home base at home_x/home_y), the nearest to us that is not ours and settled; -1 when none is left
    // and the squad goes for the general. The enemy's last graveyard (FinalGraveyard of `enemy_team`) stands next to the general and is
    // not a step. A graveyard we are taking (ours, contested) stays the target until it is ours, so the squad holds it while it turns.
    // Bot-only AV 265: both general pushes went (06:59 / 07:02) and met at Iceblood for 45 minutes, nobody wiped and nobody got
    // through. Iceblood Graveyard stayed Horde the whole match: every Horde bot that died came back 100 yd from the fight, every
    // Alliance bot 350-500 yd back at Snowfall or Stonehearth. The towers / captain plan never took it (the node assaults wiped at 5-8
    // bots), so the push now takes the enemy graveyards on its way, as AV is played (ike3's attack list starts with them too).
    inline int GeneralRouteGraveyard(std::vector<GyView> const& gy, int stage, float home_x, float home_y, float general_x, float general_y,
                                     uint8_t enemy_team)
    {
        auto d2 = [&](float x, float y) { float const dx = x - general_x, dy = y - general_y; return dx * dx + dy * dy; };
        float const ref = stage >= 0 ? d2(gy[stage].x, gy[stage].y) : d2(home_x, home_y);
        AvPoint const fin = FinalGraveyard(enemy_team);
        int best = -1;
        float best_d = 0.f;
        for (int i = 0; i < int(gy.size()); ++i)
        {
            if (gy[i].ours && !gy[i].contested) continue;
            float const fx = gy[i].x - fin.x, fy = gy[i].y - fin.y;
            if (fx * fx + fy * fy < 30.0f * 30.0f) continue;
            float const d = d2(gy[i].x, gy[i].y);
            if (d >= ref) continue;
            if (best < 0 || d > best_d) { best = i; best_d = d; }
        }
        return best;
    }

    // Nothing left to attack: wait next to the enemy general (mod-playerbots AV_BOSS_WAIT_H / AV_BOSS_WAIT_A).
    constexpr AvPoint BossWait(uint8_t team) { return team == 2 ? AvPoint{ 689.21f, -20.173f, 50.62f } : AvPoint{ -1361.773f, -248.977f, 99.369f }; }

    // How far an AV bot looks for a fight (ike3 EnemyPlayerValue::GetMaxAttackDistance): 40 yd during the first 20 minutes, 10 yd after, so
    // late in the match the bots keep running to the objective instead of chasing every enemy in sight.
    constexpr float AggroRange(uint32_t in_progress_ms) { return in_progress_ms < 20u * 60u * 1000u ? 40.0f : 10.0f; }
}
