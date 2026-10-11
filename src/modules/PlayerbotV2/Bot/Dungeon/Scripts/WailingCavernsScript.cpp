// WailingCavernsScript — Wailing Caverns (map 43, vanilla 17-27).
// Druid-themed Kalimdor cave dungeon. Notable:
//   * Lord Cobrahn shapeshifts into Snake Form mid-fight; while in
//     snake form he gains poison + faster attacks. Ranged DPS keep
//     distance; melee should not be in front of him.
//   * Lady Anacondra heals other Druids of the Fang adds (CC her or
//     burn her quickly).
//   * Kresh — random patrolling Devilsaur-style turtle; high HP,
//     no special mechanic.
//   * Skum — pull from Lord Pythas; chain-pull risk.
//   * Mutanus the Devourer (final) — hard-hitter; tank-and-spank.
// Generic logic clears most of this. Script value:
//   - Kill priority: Druids of the Fang (entry 3850, 3851, 3852)
//     first because Lady Anacondra heals them.
//   - CC priority: Lady Anacondra (3669) because she heals.
//   - Cobrahn's Mind Spike (8133) is interruptible and worth kicking.

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class WailingCavernsScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 43; }
    char const* name() const override { return "wailing_caverns"; }
    // Zero static spawns in the Classic creature data: raised by an event the bots never trigger, so a full clear does not wait for it.
    std::vector<uint32_t> event_summoned_bosses() const override
    {
        return {
            3654,   // Mutanus the Devourer: summoned by the Naralex event
        };
    }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        a.high_priority_kill_entries = {
            3840,   // Druid of the Fang
        };
        a.mandatory_interrupt_spells = {
            8133,   // Mind Spike (Cobrahn) — interruptible nuke
            8040,   // Sleep (Lady Anacondra) — disables a party member
            // Classic spell ids from the VMaNGOS spell_template / converted mob casts
            5187,   // Healing Touch
            6778,   // Healing Touch
            700,   // Sleep
            6254,   // Chained Bolt
        };
        a.cc_priority_entries = {
            3840,   // Druid of the Fang
        };
        a.dangerous_auras = {};
        // Progression waypoints — WC is a winding druid-themed cave;
        // tank walks an outer spiral hitting each Druid of the Fang
        // chamber in turn before Mutanus at the deep end. Coords from
        // public dumps; Detour validates so off-mesh points stall.
        // Boss spawn positions from the Classic creature data (VMaNGOS spawn table, map 43), in encounter order; bosses a script
        // summons (no static spawn) are left out. Replaces hand-typed coordinates that did not match the map.
        a.progression_waypoints = {
            { -151.1f, 414.4f, -72.6f },   // Lord Cobrahn
            { 46.5f, 184.0f, -89.2f },   // Lady Anacondra
            { 36.8f, -241.1f, -79.5f },   // Lord Pythas
            { -285.6f, -313.7f, -69.2f },   // Skum
            { -120.2f, -24.6f, -28.6f },   // Lord Serpentis
        };
        // Boss progression order (encounter sequence). Used by the
        // tank-advance rule for cross-room navigation when no enemies
        // are in the snapshot's nearby_enemies range — the tank
        // path-finds toward the first live boss it can reach.
        a.bosses = {
            3669,   // Lord Cobrahn
            3671,   // Lady Anacondra
            3670,   // Lord Pythas
            3674,   // Skum
            3673,   // Lord Serpentis
            3654,   // Mutanus the Devourer (final)
        };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeWailingCavernsScript()
{
    return std::make_unique<WailingCavernsScript>();
}

} // namespace Playerbot
