// FalseCombatEscape - where a dungeon bot walks when it is held in combat by attackers it can never hit
// (State_Idle DungeonCombatPositioning, [false_combat_esc]).
//
// Pure function on plain values, so the choice is unit-tested without a server.
//
// The escape was written for the dungeon autopilot: the tank walks on to the next boss it owes, the others walk to the tank.
// A bot the autopilot is suspended for (a human leads, Passive, a follow / stay / fallback order) has no pulls of its own, so it
// never walks to a boss: it walks to the player it follows, else it drops the fight in place (UBRS 2026-10-26: a passive
// companion tank summoned in with three Urok Enforcers on its threat list walked to the first boss's waypoint for minutes,
// pulling every pack on the way while the group defended it).

#pragma once

namespace Playerbot::FalseCombatEscape {

enum class Goal { BossWaypoint, Tank, Leader, Disengage };

struct Inputs
{
    bool autopilot = true;       // the dungeon autopilot leads this bot (not suspended)
    bool is_tank = false;
    bool tank_known = false;     // a live group tank on this map, other than the bot
    bool leader_known = false;   // the player the bot follows, alive on this map and more than 8 y away
    bool immune_lock = false;    // only immune attackers hold it: walking re-aggros them
    bool far_from_boss = false;  // the boss it owes is 40 y or more away
};

inline Goal Pick(Inputs const& in)
{
    if (!in.autopilot)
        return in.leader_known ? Goal::Leader : Goal::Disengage;
    if (in.immune_lock || !in.far_from_boss)
        return Goal::Disengage;
    if (!in.is_tank)
        return in.tank_known ? Goal::Tank : Goal::Disengage;
    return Goal::BossWaypoint;
}

inline char const* Tag(Goal g)
{
    switch (g)
    {
        case Goal::BossWaypoint: return "boss_wp";
        case Goal::Tank:         return "TANK";
        case Goal::Leader:       return "LEADER";
        default:                 return "";
    }
}

} // namespace Playerbot::FalseCombatEscape
