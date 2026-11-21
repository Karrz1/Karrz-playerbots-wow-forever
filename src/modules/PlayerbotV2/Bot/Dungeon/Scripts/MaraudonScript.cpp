// MaraudonScript — Maraudon (map 349, vanilla 40-49).
// Earth-themed dungeon in Desolace. 3 wings. Bosses include Celebras
// the Cursed, Noxxion, Princess Theradras, Landslide, Tinkerer
// Gizlock, Lord Vyletongue.
//
// Authoritative spell IDs from TC source:
//   src/server/scripts/Kalimdor/Maraudon/boss_*.cpp

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class MaraudonScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 349; }
    char const* name() const override { return "maraudon"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        a.high_priority_kill_entries = {
            12216,   // Poison Sprite
            13142,   // Deeprot Tangler
        };
        a.mandatory_interrupt_spells = {
            // Celebras the Cursed
            21807,  // Wrath
            12747,  // Entangling Roots
            21968,  // Corrupt Forces
            // Landslide / earth boss
            18670,  // Knock Away
            21808,  // Landslide
            // Noxxion
            21687,  // Toxic Volley
            22916,  // Uppercut
            // Classic spell ids from the VMaNGOS spell_template / converted mob casts
            21067,   // Poison Bolt
            11922,   // Entangling Roots
            21331,   // Entangling Roots
        };
        a.cc_priority_entries = {
            12216,   // Poison Sprite
            13142,   // Deeprot Tangler
        };
        a.dangerous_auras = {
            // Princess Theradras
            21909,  // Dust Field
            21832,  // Boulder
            3391,   // Thrash
            21869,  // Repulsive Gaze
            // Lord Vyletongue
            3815,   // Poison Cloud
            3490,   // Frenzied Rage
            // General trample
            5568,   // Trample
        };
        // Boss progression — NPC entries from TC's maraudon.h
        // (Princess Theradras, Celebras, Noxxion from WoWHead).
        // The boss route in walking order from the orange entrance (the 0053 route): the orange caves (Noxxion, Razorlash), the
        // purple caves (Lord Vyletongue, Celebras the Cursed), then down into the inner caves past Landslide to Tinkerer Gizlock,
        // Rotgrip's pool and Princess Theradras. The old order (Razorlash, Gizlock, Vyletongue, Noxxion, Rotgrip, Celebras) went
        // back and forth between the outer and inner caves, about 4 km a leg, and had no Landslide, so his kill counted as the
        // next boss's and every later waypoint answered the wrong boss. The walk between them, by the navmesh: 948, 443, 956,
        // 981, 1925, 909, 1137, 2297 y.
        a.bosses = {
            13282,  // Noxxion
            12258,  // Razorlash
            12236,  // Lord Vyletongue
            12225,  // Celebras the Cursed
            12203,  // Landslide
            13601,  // Tinkerer Gizlock
            13596,  // Rotgrip
            12201,  // Princess Theradras (final)
        };
        // Boss spawn positions from the Classic creature data (VMaNGOS spawn table, map 349), in the order above.
        a.progression_waypoints = {
            { 1130.4f, -191.3f, -80.0f },   // Noxxion
            { 978.9f, -10.2f, -62.5f },   // Razorlash
            { 748.9f, -219.6f, -47.7f },   // Lord Vyletongue
            { 726.1f, 78.0f, -86.6f },   // Celebras the Cursed
            { 356.7f, -185.5f, -59.8f },   // Landslide
            { 134.9f, -313.7f, -173.6f },   // Tinkerer Gizlock
            { 42.1f, -66.0f, -199.6f },   // Rotgrip
            { 27.9f, 83.2f, -124.5f },   // Princess Theradras
        };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeMaraudonScript()
{
    return std::make_unique<MaraudonScript>();
}

} // namespace Playerbot
