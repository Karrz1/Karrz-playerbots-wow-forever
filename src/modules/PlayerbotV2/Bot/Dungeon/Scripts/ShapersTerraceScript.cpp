// ShapersTerraceScript - per-dungeon advice for The Shaper's Terrace (map 3001, Classic 1.60 Forever, levels 58-60).
// The creatures and the boss fights are in classic_instance_shapers_terrace.cpp and 2026_11_19_72_world_forever_shapers_terrace.sql;
// the route (playerbot_dungeon_routes, 0045_shapers_terrace_route.sql) and this file follow the same boss order: Venomridge, Bolt,
// Cinder, Tor'Rel and Izha'Rel, Snowtalon, Vitrix, Titanosaur, Thunderhoof, King Op, the Direhorn Matriarch, Nanaya (the shortest
// walk from the landing that ends at Nanaya, about 4150 y; the journal's order would be 10100 y; every leg was path-checked on the
// 3001 navmesh).

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class ShapersTerraceScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 3001; }   // The Shaper's Terrace
    char const* name() const override { return "shapers_terrace"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        // Izha'Rel slows and shocks the group while Tor'Rel holds the tank: she dies first.
        a.high_priority_kill_entries = {
            253218,   // Izha'Rel
        };
        a.mandatory_interrupt_spells = {
            1310513,   // Chain Lightning (Thunderhoof, Bolt)
            1291380,   // Chain Lightning (Nanaya, yellow panel)
            8364,      // Blizzard (Nanaya, blue panel)
        };
        a.bosses = {
            261813,   // Venomridge
            262081,   // Bolt
            261974,   // Cinder
            261811,   // Tor'Rel (with Izha'Rel)
            262297,   // Snowtalon
            272561,   // Vitrix
            253201,   // Titanosaur
            261812,   // Thunderhoof
            261993,   // King Op
            262127,   // Direhorn Matriarch
            261809,   // Nanaya
        };
        // 1:1 with `bosses`: their spawn spots (2026_11_19_72_world_forever_shapers_terrace.sql).
        a.progression_waypoints = {
            { -5912.5f, -1220.8f, 8.88f },    // Venomridge
            { -5935.0f, -1181.8f, 9.05f },    // Bolt
            { -5904.2f, -862.5f, 112.81f },   // Cinder
            { -5687.5f, -745.8f, 25.93f },    // Tor'Rel
            { -5687.5f, -604.2f, 13.02f },    // Snowtalon
            { -5520.8f, -845.8f, 22.18f },    // Vitrix
            { -5662.5f, -1454.2f, 36.37f },   // Titanosaur
            { -6020.8f, -1554.2f, 9.37f },    // Thunderhoof
            { -6020.8f, -1829.2f, 57.07f },   // King Op
            { -5729.2f, -1762.5f, 34.07f },   // Direhorn Matriarch
            { -5854.2f, -1587.5f, 37.14f },   // Nanaya
        };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeShapersTerraceScript()
{
    return std::make_unique<ShapersTerraceScript>();
}

} // namespace Playerbot
