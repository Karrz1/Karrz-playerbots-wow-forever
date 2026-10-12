#include "RaidEncounters.h"

// Ported from Karrz's raid10-revamp ike3 bots (the ike3-based playerbot code). Script references (mc/..., bwl/..., ony/...) point at the
// VMaNGOS scripts that Forever's scripts/Custom/Classic instances were ported from; the ids are the same.

namespace Playerbot::Raid {

    // Onyxia's Lair (map 249). Source: src/scripts/kalimdor/dustwallow_marsh/onyxias_lair/boss_onyxia.cpp (abbreviated ony/boss_onyxia.cpp).
    // Boss entry 10184 (Onyxia, dragonkin, 1.1M hp), whelp 11262 (2.8k hp), warder 12129 (83k hp elite, dragonkin).
    //
    // Fight as scripted: phase 1 (100-65%) on the ground: Flame Breath 18435 (cone, 2s cast), Cleave 19983, Wing Buffet 18500,
    // Knock Away 19633 (-25% threat), Tail Sweep 15847 behind her every 3.5s (ony/boss_onyxia.cpp:362-418).
    // Phase 2 (65-40%) she flies between 8 waypoints, casts Fireball 18392 at her victim with a 100% threat wipe so the target rotates,
    // sometimes a Deep Breath along one of 8 paths (35% clockwise / 35% counter-clockwise / 30% breath, :503-536) and summons 5-7
    // whelps per wave from two caves at (-30.1,-254.5,-89.4) and (-30.8,-177.1,-89.3) (:109-113,473-500).
    // Phase 3 (<40%) she lands at (-8.86,-212.75,-87.5), Bellowing Roar 18431 fear every 15-30s and a whelp every 1-10s (:540-565).
    void RegisterOnyxia(RaidRules& r)
    {
        r.name = "Onyxia's Lair";
        r.bosses = { 10184 };

        // Whelps die to a single AoE tick but several at once overwhelm healers; the Warders sit in the egg pit and are respawned
        // when Onyxia aggroes (ony/boss_onyxia.cpp:298-302) - they are not part of the kill.
        r.priority.push_back({ 11262, 2 });   // Onyxian Whelp: above Onyxia (default 1) because she cannot be reached in phase 2 (ony/boss_onyxia.cpp:70,479)
        r.priority.push_back({ 12129, 0 });   // Onyxian Warder: below Onyxia, never chased (ony/boss_onyxia.cpp:71,298-302)

        // Phase 2 Fireball (18392) is cast at her current victim and the victim changes every cast (ony/boss_onyxia.cpp:461-466);
        // splash/standing-in-a-stack is not in the script, the 8-yard value is taken from the AzerothCore bots (raid-research.md), so it is
        // kept small. Only ranged and healers, tanks have to stay put for the whelps.
        // Phase window 65..40 percent: Fireball is a phase 2 spell, so the spread no longer scatters the raid in phases 1 and 3.
        r.spread.push_back({ 10184, 8.0f, Who::Ranged, 65.0f, 40.0f });    // UNVERIFIED radius (spell 18392 area not in the scripts)
        r.spread.push_back({ 10184, 8.0f, Who::Healer, 65.0f, 40.0f });    // UNVERIFIED radius

        // Deep Breath (phase 2, boss_onyxia.cpp:512-538): she casts the path spell of her CURRENT waypoint on herself, turns to face the
        // opposite waypoint (m_uiMovePoint + 4), waits 5s, then flies there at speed rate 3.0 (:448-462). The path spells' target positions
        // (comment block at the end of the file) run along that straight line, 89-90 yards between the waypoints (:106-116) and out to
        // the cave wall. Corridor = 10 yd behind her position, 95 yd ahead along her facing. Half width 14 is UNVERIFIED (the trigger
        // spells' radius is not in the scripts); the points along the paths are 7-12 yd apart.
        for (uint32_t spell : { 17086u, 18351u, 18576u, 18609u, 18564u, 18584u, 18596u, 18617u })
            r.avoidBreath.push_back({ 10184, spell, 14.0f, 95.0f, Who::Anyone });

        // Phase 1 on the ground: Flame Breath 18435, Wing Buffet 18500 and Cleave 19983 hit the front, Tail Sweep 15847 the back
        // (classic_boss_onyxia.cpp:20-25). Only the tank stands in front, nobody stands behind her; melee and casters take the
        // flanks. The cones skip her while she flies (phase 2), where her facing changes at every waypoint. Cone angles and
        // lengths are from the classic tooltips, UNVERIFIED (the spell radii are client data, not in the scripts).
        r.avoidFrontal.push_back({ 10184, 25.0f, 45.0f, Who::NonTank });
        r.avoidFrontal.push_back({ 10184, 15.0f, 80.0f, Who::NonTank, 180.0f });

        // Left out on purpose (needs engine support or coordinates the scripts do not give):
        //  - Bellowing Roar fear (18431): no dispel (fear), tremor totem / fear ward is a buff decision.
        //  - Heated Ground 22191-22202: trigger positions only, no dynamic object proven.
    }
} // namespace Playerbot::Raid
