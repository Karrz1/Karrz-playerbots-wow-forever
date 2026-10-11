// ShadowfangKeepScript — Shadowfang Keep (map 33, vanilla 16-26).
// Worgen / Forsaken keep in Silverpine. Notable mechanics:
//   * Apothecary Hummel + Frye + Baxter — three-pack final boss in
//     classic SFK. Each Apothecary uses different toxin auras —
//     Hummel applies Chromatic Mutation (interruptible), Frye applies
//     Anesthetic (sleep), Baxter applies Acid Vials (poison DoT).
//     CC priority: drop Frye first (sleep on healer = wipe), then
//     Baxter (DoT removable), then Hummel (single-target).
//   * Hummel periodically summons Crown Apothecary adds — kill order:
//     adds before boss to avoid 5+ targets on tank.
//   * Lord Godfrey casts Cursed Bullet (interruptible) — wastes party
//     resources if missed.

#include "../DungeonScript.h"
#include "../../ClassTables.h"

namespace Playerbot {

namespace {

// Classic 1.60 (Forever): the vanilla dungeon. The script below carries the retail remake's ids, which do not exist there.
DungeonAdvice ShadowfangKeepClassicAdvice()
{
    DungeonAdvice a;
    // Creature ids and spell ids from the VMaNGOS data Forever's Classic world was built from (creature_template, creature spawns,
    // spell_template); the casts come from the converted EventAI / spell lists in 2026_09_28_15_world_vanilla_smartai.sql.
    a.high_priority_kill_entries = {
        3854,   // Shadowfang Wolfguard
        3855,   // Shadowfang Darksoul
        3873,   // Tormented Officer
        3863,   // Lupine Horror
        3853,   // Shadowfang Moonwalker
    };
    a.mandatory_interrupt_spells = {
        7588,   // Void Bolt
        7068,   // Veil of Shadow
        1026,   // Holy Light
        7106,   // Dark Restore
        7295,   // Soul Drain
    };
    a.cc_priority_entries = {
        3855,   // Shadowfang Darksoul
        3873,   // Tormented Officer
    };
    // Fenrus the Devourer's Toxic Saliva (spell_template: periodic damage, poison), cleansed first.
    a.dispel_priority_spells = { 7125 };
    a.bosses = {
        3914,   // Rethilgore
        3886,   // Razorclaw the Butcher
        3887,   // Baron Silverlaine
        4278,   // Commander Springvale
        4279,   // Odo the Blindwatcher
        4274,   // Fenrus the Devourer
        3927,   // Wolf Master Nandos
        4275,   // Archmage Arugal
    };
    a.progression_waypoints = {
        { -252.1f, 2123.1f, 81.2f },   // Rethilgore
        { -202.6f, 2258.0f, 76.3f },   // Razorclaw the Butcher
        { -275.3f, 2297.3f, 76.2f },   // Baron Silverlaine
        { -222.6f, 2259.4f, 102.8f },   // Commander Springvale
        { -236.7f, 2146.1f, 100.1f },   // Odo the Blindwatcher
        { -135.6f, 2168.7f, 128.8f },   // Fenrus the Devourer
        { -120.7f, 2162.0f, 155.8f },   // Wolf Master Nandos
        { -76.8f, 2152.4f, 155.8f },   // Archmage Arugal
    };
    return a;
}

class ShadowfangKeepScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 33; }
    char const* name() const override { return "shadowfang_keep"; }

    // Apothecary Hummel (36296) is SFK instance encounter index 5, but his
    // spawn is gated to the "Love is in the Air" holiday (game_event 8), so he
    // is NEVER present in a normal pure-bot run — that encounter stays
    // NOT_STARTED forever. Under strict full-clear semantics (not_done <=
    // phantom_k) the run would read incomplete for eternity and the squad would
    // never auto-leave after clearing the 5 real bosses. Declare him event-
    // summoned (same mechanism as Arcatraz Skyriss) so phantom_k accounts for
    // the one perpetually-unfinished encounter and full_clear fires at 5/6.
    // During the holiday bots CAN still kill him — his BossAI self-credits, so
    // done_count simply includes him and full_clear still holds.
    std::vector<uint32_t> event_summoned_bosses() const override { return { 36296 }; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        if (IsVanillaRealm())
            return ShadowfangKeepClassicAdvice();
        DungeonAdvice a;
        // Apothecary entries verified vs TC boss_apothecary_hummel.cpp:84-85
        // (NPC_APOTHECARY_FRYE = 36272, NPC_APOTHECARY_BAXTER = 36565;
        // Hummel itself is the Cata heroic encounter, entry not exposed
        // in the .h — DB lookup confirms ~36296).
        a.high_priority_kill_entries = {
            36885,  // Crown Apothecary (Hummel encounter adds, script-spawned
                    // per TC boss_apothecary_hummel.cpp:87)
        };
        // Spells from TC boss_apothecary_hummel.cpp:34-48. Old IDs
        // 68595/68594/7964 had no TC backing (68595/68594 appear nowhere
        // in TC source); the real interruptibles are:
        a.mandatory_interrupt_spells = {
            68607,  // Perfume Spray (Hummel)
            68948,  // Cologne Spray (Hummel rework)
            68821,  // Chain Reaction
        };
        a.cc_priority_entries = {
            36272,  // Apothecary Frye (real entry per TC :84)
            36565,  // Apothecary Baxter
        };
        // Dangerous-ground spills (perfume/cologne pools).
        a.dangerous_auras = {
            68798,  // Perfume Spill
            68614,  // Cologne Spill
            68927,  // Perfume Spill Damage
            68934,  // Cologne Spill Damage
        };
        // Boss progression — NPC entries from TC's shadowfang_keep.h
        // (the Cata remake is the ONLY live version on map 33; classic
        // Rethilgore/Razorclaw/Nandos/Arugal no longer spawn there).
        // TC encounter order: Ashbury(0) Silverlaine(1) Springvale(2)
        // Walden(3) Godfrey(4); Apothecary Hummel(5) is the seasonal
        // Crown Chemical Co. event, script-spawned.
        a.bosses = {
            46962,  // Baron Ashbury
            3887,   // Baron Silverlaine
            4278,   // Commander Springvale
            46963,  // Lord Walden
            46964,  // Lord Godfrey (final)
        };
        // Baron Ashbury (46962) sits behind a CLOSED Cell Door (GO 18934) in
        // the SW courtyard; bots reach 0.4y of him but get SPELL_FAILED_
        // LINE_OF_SIGHT and can never pull him (live 2026-07-24, pure-bot 0/6
        // courtyard stall). The adjacent Lever (GO 18900) opens that door via
        // its own SmartGameObjectAI (event GO_STATE_CHANGED -> activate closest
        // GO 18934). Have the tank pull it like a human would. (The other two
        // courtyard cells — 18901/18936, 101811/18935 — gate trash/prisoners,
        // not bosses, so only Ashbury's lever is needed to unblock progression.)
        a.use_go_entries = { 18900 };  // Baron Ashbury's cell lever (see event_summoned_bosses re: 6th encounter)
        // Progression waypoints — authored 1:1 at the LIVE Cata-revamp boss
        // spawns (verified from wc_world.creature 2026-07-25), same order as
        // a.bosses above. The prior list was the CLASSIC layout (Rethilgore/
        // Razorclaw/Nandos), badly misaligned with the actual encounters — e.g.
        // it placed "Silverlaine" in the upper hall at z124 when Baron
        // Silverlaine (3887) actually stands at the N courtyard floor (route
        // crumb 18, z76). The route follower anchors the cursor's upper bound to
        // waypoints[bosses_done_count] whenever the caged first boss is out of
        // scan, so these MUST be accurate or the tank marches to the wrong crumb.
        a.progression_waypoints = {
            { -256.1f,  2117.1f,  81.3f },   // [0] Baron Ashbury (caged, route crumb 7)
            { -265.9f,  2293.6f,  76.2f },   // [1] Baron Silverlaine (N courtyard, crumb 18)
            { -224.6f,  2264.5f, 102.8f },   // [2] Commander Springvale (chapel terrace)
            { -150.4f,  2161.9f, 128.7f },   // [3] Lord Walden (upper hall)
            { -82.7f,   2157.4f, 155.8f },   // [4] Lord Godfrey (tower top)
        };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeShadowfangKeepScript()
{
    return std::make_unique<ShadowfangKeepScript>();
}

} // namespace Playerbot
