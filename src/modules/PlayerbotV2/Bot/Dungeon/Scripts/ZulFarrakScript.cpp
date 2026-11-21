// ZulFarrakScript — Zul'Farrak (map 209, vanilla 41-51).
// Troll dungeon in Tanaris. Bosses: Antu'sul, Theka the Martyr,
// Sergeant Bly, Hydromancer Velratha, Sandfury Executioner,
// Witch Doctor Zum'rah, Chief Ukorz Sandscalp, Nekrum Gutchewer,
// Ruuzlu, Gahz'rilla.
//
// Authoritative spell IDs from TC source:
//   src/server/scripts/Kalimdor/ZulFarrak/boss_*.cpp

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class ZulFarrakScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 209; }
    char const* name() const override { return "zul_farrak"; }
    // Zero static spawns in the Classic creature data: raised by an event the bots never trigger, so a full clear does not wait for it.
    std::vector<uint32_t> event_summoned_bosses() const override
    {
        return {
            7796,   // Nekrum Gutchewer: Sergeant Bly event
            7273,   // Gahz'rilla: summoned with the Mallet of Zul'Farrak
        };
    }

    // The pyramid event. Sergeant Bly and his crew sit in the Troll Cages on top of the pyramid (141070-141074, doors with no lock); a
    // player opens one and all of them are free (classic_zulfarrak.cpp go_troll_cage: every cage starts the event). Only one is listed:
    // a second cage opened later would set the event back to its start in the middle of the waves.
    std::vector<uint32_t> hand_doors() const override { return { 141074 }; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        // After the three waves of trolls up the pyramid stairs, Weegli Blastfuse blows up the End Door (146084) to Chief Ukorz when
        // asked ("Will you blow up that door now?"), the only way it opens. Then Sergeant Bly, a boss on the route, is told to fight
        // ("That's it! I'm tired of helping you out...") and he and his crew turn on the party. Bly is asked only once the door is
        // open: told first, the crew turns hostile with Weegli and the door is never blown. Both stand at the top of the stairs.
        // Zum'rah must be dead before the Weegli talk, so a tank still at Zum'rah (249 y from the stairs) is not pulled away.
        a.door_talks = {
            { 7607, 1883.87f, 1263.49f, 41.55f, 7267, { 7271 } },          // Weegli Blastfuse -> the End Door to Chief Ukorz
            { 7604, 1887.17f, 1263.72f, 41.48f, 7604, { 7271 }, 146084 },  // Sergeant Bly, once the End Door is open
        };
        a.high_priority_kill_entries = {
            5650,   // Sandfury Witch Doctor
            5648,   // Sandfury Shadowcaster
            7246,   // Sandfury Shadowhunter
        };
        a.mandatory_interrupt_spells = {
            // Witch Doctor Zum'rah
            12739,  // Shadow Bolt
            15245,  // Shadowbolt Volley
            11086,  // Ward of Zum'rah
            12491,  // Healing Wave
            // Classic spell ids from the VMaNGOS spell_template / converted mob casts
            17843,   // Flash Heal
            12471,   // Shadow Bolt
            11641,   // Hex
        };
        a.cc_priority_entries = {
            5650,   // Sandfury Witch Doctor
            5648,   // Sandfury Shadowcaster
        };
        a.dangerous_auras = {
        };
        // Boss progression — Zul'Farrak has 7+ encounters.
        // The boss route is index for index with the waypoints below: the owed boss is steered to by its waypoint. A boss with no
        // static spawn (event_summoned_bosses) has no waypoint and is not on it; with one in, every later waypoint answered the
        // wrong boss.
        a.bosses = {
            8127,   // Antu'sul
            7272,   // Theka the Martyr
            7271,   // Witch Doctor Zum'rah
            7604,   // Sergeant Bly (Pyramid event lead)
            7795,   // Hydromancer Velratha
            7797,   // Ruuzlu (gladiator boss)
            7267,   // Chief Ukorz Sandscalp (final)
        };
        // Progression waypoints — Zul'Farrak is an outdoor desert
        // dungeon (no roof in most sections). Linear walk: entry →
        // Antu'sul → west wing (Theka, Zum'rah) → pyramid stairs →
        // pyramid event (Bly's crew) → Velratha pool → Ruuzlu arena →
        // Chief Ukorz final platform.
        // Boss spawn positions from the Classic creature data (VMaNGOS spawn table, map 209), in encounter order; bosses a script
        // summons (no static spawn) are left out. Replaces hand-typed coordinates that did not match the map.
        a.progression_waypoints = {
            { 1815.8f, 670.4f, 15.0f },   // Antu'sul
            { 1778.0f, 859.7f, 8.9f },   // Theka the Martyr
            { 1912.2f, 1016.1f, 11.6f },   // Witch Doctor Zum'rah
            { 1882.9f, 1299.3f, 48.4f },   // Sergeant Bly
            { 1698.2f, 1210.7f, 9.4f },   // Hydromancer Velratha
            { 1730.3f, 1015.2f, 54.9f },   // Ruuzlu
            { 1727.5f, 1017.4f, 54.9f },   // Chief Ukorz Sandscalp
        };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeZulFarrakScript()
{
    return std::make_unique<ZulFarrakScript>();
}

} // namespace Playerbot
