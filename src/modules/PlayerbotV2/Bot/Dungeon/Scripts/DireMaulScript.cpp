// DireMaulScript — Dire Maul (map 429, vanilla 55-60+).
// Three wings: East (DM:E), West (DM:W), North (DM:N).
//   * DM:E — Pusillin chase (rogue, runs). Lethtendris's Imp Mojo
//     party-buff side-mechanic.
//   * DM:W — Tendris Warpwood AoE summon Whirlwind. Magister Doane
//     stacking armor; Illyanna Ravenoak Hunter's Mark.
//   * DM:N — Tribute run (multiple guard groups left alive grants
//     better loot — bots burn everything; suboptimal but completable).
//     King Gordok charges; Cho'Rush dispels boss buffs.

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class DireMaulScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 429; }
    char const* name() const override { return "dire_maul"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        a.high_priority_kill_entries = {
            11448,   // Gordok Warlock
            11444,   // Gordok Mage-Lord
            11470,   // Eldreth Sorcerer
            11466,   // Highborne Summoner
            11457,   // Wildspawn Hellcaller
            11455,   // Wildspawn Felsworn
            14398,   // Eldreth Darter
        };
        a.mandatory_interrupt_spells = {
            22417,  // Mortal Strike (King Gordok / DM:N tribunes)
            22336,  // Whirlwind (Magister Doane)
            22456,  // Hunter's Mark (Illyanna Ravenoak)
            22640,  // Polymorph (DM:W casters mind-control)
            // Classic spell ids from the VMaNGOS spell_template / converted mob casts
            13323,   // Polymorph
            15530,   // Frostbolt
            16102,   // Flamestrike
            16006,   // Chain Lightning
            12466,   // Fireball
            15232,   // Shadow Bolt
            15228,   // Fireball
            15537,   // Shadow Bolt
            12542,   // Fear
            22947,   // Mana Burn
            12096,   // Fear
            22414,   // Lightning Bolt
        };
        a.cc_priority_entries = {
            11448,   // Gordok Warlock
            11444,   // Gordok Mage-Lord
            11470,   // Eldreth Sorcerer
            11466,   // Highborne Summoner
            11457,   // Wildspawn Hellcaller
            11455,   // Wildspawn Felsworn
        };
        a.dangerous_auras = {
            22425,  // Charge (King Gordok knockback)
        };
        // Boss progression — Dire Maul has 3 wings, ~17 encounters total.
        // East/West/North wings; tank-advance picks closest alive boss.
        a.bosses = {
            // East wing
            14354,  // Pusillin
            13280,  // Hydrospawn
            14327,  // Lethtendris
            11490,  // Zevrim Thornhoof
            11492,  // Alzzin the Wildshaper (East final)
            // West wing
            11489,  // Tendris Warpwood
            11487,  // Magister Kalendris
            11488,  // Illyanna Ravenoak
            11467,  // Tsu'zee
            11496,  // Immol'thar
            11486,  // Prince Tortheldrin (West final)
            // North wing
            14326,  // Guard Mol'dar
            14322,  // Stomper Kreeg
            14321,  // Guard Fengus
            14323,  // Guard Slip'kik
            14325,  // Captain Kromcrush
            14324,  // Cho'Rush the Observer
            11501,  // King Gordok (North final)
        };
        // Progression waypoints — Dire Maul has 3 separate wings
        // sharing one map_id. Default path covers the North wing
        // (King Gordok) which is the most-run for the tribute trick.
        // Boss spawn positions from the Classic creature data (VMaNGOS spawn table, map 429), in encounter order; bosses a script
        // summons (no static spawn) are left out. Replaces hand-typed coordinates that did not match the map.
        a.progression_waypoints = {
            { 83.4f, -198.4f, -4.0f },   // Pusillin
            { -16.6f, -427.6f, -59.8f },   // Hydrospawn
            { -7.8f, -443.8f, 16.4f },   // Lethtendris
            { -35.0f, -448.0f, -37.9f },   // Zevrim Thornhoof
            { 260.9f, -436.8f, -120.0f },   // Alzzin the Wildshaper
            { 13.5f, 478.3f, -23.3f },   // Tendris Warpwood
            { -106.3f, 551.3f, -4.4f },   // Magister Kalendris
            { -14.6f, 542.1f, 28.6f },   // Illyanna Ravenoak
            { 128.6f, 561.8f, -4.3f },   // Tsu'zee
            { -40.5f, 811.1f, -29.4f },   // Immol'thar
            { 132.6f, 625.9f, -48.4f },   // Prince Tortheldrin
            { 396.5f, -8.6f, -24.9f },   // Guard Mol'dar
            { 494.2f, 99.7f, -2.6f },   // Stomper Kreeg
            { 384.7f, 341.3f, 2.9f },   // Guard Fengus
            { 559.2f, 549.4f, -25.4f },   // Guard Slip'kik
            { 627.6f, 481.7f, 29.5f },   // Captain Kromcrush
            { 835.4f, 487.9f, 37.3f },   // Cho'Rush the Observer
            { 829.7f, 481.3f, 37.4f },   // King Gordok
        };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeDireMaulScript()
{
    return std::make_unique<DireMaulScript>();
}

} // namespace Playerbot
