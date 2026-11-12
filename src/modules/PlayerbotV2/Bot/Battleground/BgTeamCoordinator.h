// BgTeamCoordinator — team-level battleground strategy (BG audit N60).
//
// Before this, every bot decided greedily from its own snapshot: identical
// stimuli produced identical decisions (thundering herds), nobody held a
// quota (5 attackers on one node, zero on the next), escorts/hunters were
// whoever's guid hashed right, and there was no opening split or mid-match
// rebalancing. This service computes ONE plan per (battleground instance,
// team) on the WORLD THREAD and publishes per-bot orders; the snapshot
// builder copies each bot's order into its snapshot (BotSnapshot::BgState::
// BgOrder) and the bot logic executes it, falling back to the legacy per-bot role
// logic whenever no order is present — graceful degradation by design.
//
// Threading: Update() is the only WRITER of orders_ and runs on the WORLD
// THREAD, inline in OnWorldUpdate BEFORE the parallel snapshot-build barrier.
// OrderFor() is a pure const find() READER and (since #5 Phase 4) is called
// concurrently from the snapshot-build WORKER threads. Safe ONLY because the
// writer and the readers are temporally disjoint — orders_ is never mutated
// during the build phase, and concurrent reads of a non-mutating unordered_map
// are well-defined. Move Update() into the parallel phase and this needs a
// shared_mutex. The bot workers only ever see orders through their snapshots.
//
// Inputs (all already maintained elsewhere — this class adds no harvest):
//   * Roster: live Player objects of V2 bots in the BG (registry walk).
//   * Node states / carriers / score: the freshest team member's published
//     snapshot (node_states are map-global; the builder harvests them every
//     snapshot build, including the per-node player-pressure counts).
//   * Static script data: BattlegroundScriptMgr::GetAdvice computed HERE
//     on the world thread from that same immutable snapshot. Never read a
//     BotAI's BgAdviceCache from this class — it is owned by the bot worker
//     thread, which reassigns its heap vectors every ≤2s (use-after-free;
//     adversarial review 2026-06-10).
//
// Strategy families (selected from the advice shape):
//   * CTF        (enemy_flag set, no capture nodes): FC + escorts + EFC
//                hunt squad + mid pressure.
//   * Node race  (capture nodes, no enemy_flag): per-node defense quotas
//                scaled by live enemy pressure, ONE concentrated attack
//                squad, stop-the-cap emergencies, score/time bias.
//   * Orb/hybrid (enemy_flag AND nodes — Kotmogu, Deephaul, EotS): carrier
//                play from the CTF family + node play for the remainder.
// Endgame (advice endgame target + all-in conditions) overrides attackers.
//
// Stability: orders are sticky — a bot keeps its order unless it becomes
// invalid (target flipped to us / carrier died), an emergency outranks it,
// or the periodic full re-plan finds a materially better assignment.

#pragma once

#include "../BotSnapshot.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

class Battleground;

namespace Playerbot {

class BgTeamCoordinator
{
public:
    using BgOrder = BgState::BgOrder;

    BgTeamCoordinator() = default;

    // World tick driver. Re-plans every team of every active bot BG at
    // kPlanIntervalMs. Cheap when no BGs run (single registry walk).
    void Update(uint32 now_ms);

    // Order lookup for the snapshot builder. Returns nullptr when no
    // current plan covers the bot (consumer falls back to legacy logic).
    BgOrder const* OrderFor(uint64 bot_guid_low) const
    {
        auto it = orders_.find(bot_guid_low);
        return it == orders_.end() ? nullptr : &it->second;
    }

    // Human-readable plan dump for `.playerbot bgcoord`.
    std::string DebugDump() const;

private:
    struct Member
    {
        uint64 guid_low = 0;
        float  x = 0.f, y = 0.f, z = 0.f;
        uint8  cls = 0;
        bool   alive = false;
        bool   healer = false;
        bool   tank = false;
        bool   is_carrier = false;
        bool   assigned = false;   // set as the planner hands out orders
    };

    // Slimmed copy of the script's static node list — attack-target
    // priority lookup by position, plus distinct-orb runner targets on
    // Kotmogu (where nodes[] holds the 4 orb spawns).
    struct AdviceNode { float x = 0.f, y = 0.f, z = 0.f; uint8 priority = 0; };

    struct TeamPlanContext
    {
        std::vector<Member>       members;
        std::vector<BgNodeState>  nodes;        // live ownership (small copy)
        std::vector<AdviceNode>   advice_nodes; // static script nodes
        std::vector<uint8>        fc_class_preference;
        uint8  team = 0;                        // 1=alliance 2=horde
        uint16 type_id = 0;
        uint32 instance_id = 0;
        int32  score_delta = 0;                 // my_score - enemy_score
        uint32 in_progress_ms = 0;
        int32  score_bias_threshold = 200;
        // From the advice cache ({0,0,0} sentinel = unset):
        float  enemy_flag_x = 0.f, enemy_flag_y = 0.f, enemy_flag_z = 0.f;
        float  own_flag_x = 0.f, own_flag_y = 0.f, own_flag_z = 0.f;
        float  home_x = 0.f, home_y = 0.f, home_z = 0.f;
        float  endgame_x = 0.f, endgame_y = 0.f, endgame_z = 0.f;
        // AV endgame push: the script flags the boss push as unconditional
        // (enemy reinforcements low) and names the enemy general's entry
        // (Vanndar 11948 / Drek'Thar 11946); the priority-3 advice node is
        // the enemy captain (Galvangar 11947 / Balinda 11949), which the
        // script drops once the captain dies. Default 0/false elsewhere.
        bool   endgame_unconditional = false;
        uint32 endgame_creature_entry = 0;
        float  captain_x = 0.f, captain_y = 0.f, captain_z = 0.f;
        uint32 captain_creature_entry = 0;
        bool   captain_alive = false;
        // AV (see BattlegroundAdvice): where the captain squad gathers, whether the general waits for the enemy towers, and the
        // non-node kill objectives (mine bosses).
        float  captain_stage_x = 0.f, captain_stage_y = 0.f, captain_stage_z = 0.f;
        bool   endgame_needs_towers = false;
        struct KillTarget { float x = 0.f, y = 0.f, z = 0.f; uint32 entry = 0; };
        std::vector<KillTarget> kill_targets;
        // AV open-field brawl (script fight_zone_*, {0,0} = none) and the living human teammates in the BG
        // (x, y): the share of bots sent to the fight follows where the humans are fighting.
        float  fight_x = 0.f, fight_y = 0.f, fight_z = 0.f;
        std::vector<std::pair<float, float>> humans;
        // From the reference snapshot:
        ObjectGuid friendly_carrier;
        ObjectGuid enemy_carrier;
        float  enemy_carrier_x = 0.f, enemy_carrier_y = 0.f,
               enemy_carrier_z = 0.f;
        // Friendly carriers who are NOT V2 bots (human teammates). The
        // planner can't order them, but must not send a redundant pickup
        // runner on single-flag maps, and still owes them an escort.
        int    external_carriers = 0;
        bool   scalar_carrier_is_external = false;
        float  friendly_carrier_x = 0.f, friendly_carrier_y = 0.f,
               friendly_carrier_z = 0.f;
        // Flags lying on the ground (FLAGDROP 26 / NEW_FLAG_DROP 37) on the
        // BG map. Either team can click one: a team returns its own flag and
        // re-lifts the enemy one, so the nearest bot is sent to every drop.
        struct DroppedFlag { float x = 0.f, y = 0.f, z = 0.f; };
        std::vector<DroppedFlag> dropped_flags;
    };

    // `targets` = a signature of what the latched squad attacks: a new target (a node taken, the window sliding on) starts the grace again.
    // `count` / `rise` = the bots at the waiting spot last plan and when that count last grew (the time cap waits for arrivals to stop).
    struct AvGate
    {
        std::unordered_map<uint64, uint32> go, wait, count, rise;
        std::unordered_map<uint64, uint64> targets;
        void Reset(uint64 key) { go.erase(key); wait.erase(key); count.erase(key); rise.erase(key); targets.erase(key); }
    };

    void PlanTeam(TeamPlanContext& ctx, uint32 now_ms);
    void PlanCtf(TeamPlanContext& ctx);
    void PlanNodeRace(TeamPlanContext& ctx);
    // AV: runs the gather-at-the-waiting-spot / go in together / regroup-after-a-wipe cycle of `gate` and returns whether the squad goes
    // in. `targets` are what the squad attacks (the wipe check looks within BgAvPlan::kCaptainSquadRadius of them, after a grace for the
    // walk from the waiting spot to the nearest one); `squad` = the bots the plan sends; `wiped` is set when this call ended an assault.
    bool AvAssaultGo(TeamPlanContext const& ctx, AvGate& gate, uint64 key, float stage_x, float stage_y,
                     std::vector<std::pair<float, float>> const& targets, int squad, char const* what, bool& wiped);
    // Whether `guid` died after the assault wave of `key` that went at `wave_ms` (BgAvPlan::OutOfWave).
    bool OutOfWave(uint64 key, uint64 guid, uint32 wave_ms) const;
    void AssignOrder(uint64 guid, uint8 kind, float x, float y, float z,
                     ObjectGuid focus = ObjectGuid::Empty, uint8 squad = 0,
                     uint32 target_entry = 0);
    int  PickNearest(std::vector<Member>& members, uint8 kind,
                     float tx, float ty, int healer_bias,
                     bool allow_carrier,
                     ObjectGuid focus = ObjectGuid::Empty) const;
    static uint8 NodePriorityFor(TeamPlanContext const& ctx,
                                 BgNodeState const& n);

    // Published plan (read by the builder via OrderFor) and the plan being
    // built this Update pass. Two maps so OrderFor stays valid mid-plan and
    // the sticky-discount can compare against the previous assignment.
    std::unordered_map<uint64, BgOrder> orders_;
    std::unordered_map<uint64, BgOrder> next_orders_;
    // Per-(instance,team) plan signature for change-only logging.
    std::unordered_map<uint64, uint64>  plan_sig_;
    // Per-(instance,team) STICKY endgame commitment. Once a team commits to the
    // captain->general push (lead_push fired with the captain alive), keep
    // pushing through reinforcement-lead dips until the endgame creature is dead
    // or the team is genuinely crushed — otherwise the AV reinforcement race
    // oscillates the lead around +-75 and the push fizzles before enough bots
    // pile into the captain's courtyard to kill him. Value = commit timestamp
    // (unused beyond presence); key packs (instance_id<<8 | team).
    std::unordered_map<uint64, uint32>  endgame_commit_;
    // AvGate (above) per (instance,team): `go` is the latch, set once enough of the squad stood at the waiting spot and cleared when the
    // target is gone or the squad that went in is wiped (BgAvPlan::CaptainAssaultBroken); without it the squad leaving the spot would
    // drop the count below the threshold and send everyone back. `wait` = match time the squad started waiting (the time cap).
    // The captain assault, and the assault on the enemy nodes (towers and graveyards) once the captain is dead or left.
    AvGate captain_gate_;
    AvGate node_gate_;
    AvGate general_gate_;   // the general push (BgAvPlan::GeneralDue or the tower rule), gathering short of his room
    // Per-(instance,team) count of wiped captain assaults; at BgAvPlan::kCaptainMaxAssaults the team leaves the captain for the towers.
    std::unordered_map<uint64, uint32>  captain_wipes_;
    // Per-(instance,team): the enemy captain's death has been logged.
    std::unordered_set<uint64>          captain_down_;
    // Per-(instance,team): match time at which the enemy captain was first seen dead or was left (BgAvPlan::GeneralDue counts from it).
    std::unordered_map<uint64, uint32>  captain_done_ms_;
    // Per-(instance,team): bot guid -> match time of the last plan that saw it dead (AV assault waves, BgAvPlan::OutOfWave).
    std::unordered_map<uint64, std::unordered_map<uint64, uint32>> died_ms_;
    // Per-(instance,team): the graveyard the general push last went for (BgAvPlan::GeneralRouteGraveyard), so each new one is logged once.
    std::unordered_map<uint64, uint64>  general_step_;
    // Per-(instance,team) AV brawl state: the match's random base share, the follow mode in force
    // (0 = base, 1 = a human fights in the valley, 2 = a human pushes the enemy bases) and when a
    // different mode was first wanted (kMidModeHoldMs hysteresis so bots do not flip-flop).
    struct MidState { float base = 0.4f; uint8 mode = 0; uint8 wanted = 0; uint32 wanted_since_ms = 0; };
    std::unordered_map<uint64, MidState> mid_state_;
    // Per-(instance,team,node) match time at which the enemy's assault on a node was first seen: the contest age the defender picker
    // uses (BgTactics::CanArriveBeforeFlip). Erased as soon as the node is no longer contested.
    std::unordered_map<uint64, uint32> contest_since_;
    // Per-(instance,team) WSG zone-rotation epoch last planned (PlanCtf): bots keep their zone within an epoch, and a new epoch reshuffles.
    std::unordered_map<uint64, uint32> zone_epoch_;
    uint32 last_plan_ms_ = 0;
    std::string last_dump_;
};

} // namespace Playerbot
