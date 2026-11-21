// AlcazPrisonScript - per-dungeon advice for Alcaz Prison (map 2994, Classic 1.60 Forever, levels 50-55).
// The creatures and the boss fights are in classic_instance_alcaz_prison.cpp and 2026_11_14_41_world_forever_alcaz_prison.sql;
// the route (playerbot_dungeon_routes, 0043_alcaz_prison_route.sql) and this file follow the same boss order:
// Overseer Wrex, Katerina the Cleaner, The Questioner, Tidemistress Ca'zass, Blazeroar (the shortest walk through the
// prison that keeps Wrex first and Blazeroar last; every leg was path-checked on the 2994 navmesh).

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class AlcazPrisonScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 2994; }   // Alcaz Prison
    char const* name() const override { return "alcaz_prison"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        // The casters of the packs: their bolts are the spike damage on the group.
        a.high_priority_kill_entries = {
            255373,   // Strashaz Enchantress (Lightning Bolt, Frost Shock; two of them join Ca'zass)
            255364,   // Renegade Conjurer (Frostbolt, Frost Nova)
            255262,   // Alcaz Chaplain (two of them join Katerina)
        };
        a.cc_priority_entries = {
            255373,   // Strashaz Enchantress
            255364,   // Renegade Conjurer
        };
        a.mandatory_interrupt_spells = {
            12466,   // Fireball (Blazeroar as Zarla Ober)
            17620,   // Drain Life (The Questioner)
        };
        // Commander Folkar is chained (immune to players and creatures) and Mason Connery is a prisoner to free: neither is a target.
        a.ignore_entries = {
            260386,   // Commander Folkar
            264770,   // Mason Connery
        };
        a.bosses = {
            255391,   // Overseer Wrex
            257016,   // Katerina the Cleaner
            256236,   // The Questioner
            255997,   // Tidemistress Ca'zass
            255702,   // Blazeroar
        };
        // 1:1 with `bosses`: their spawn spots (guids 48120006, 48120043, 48120052, 48120017, 48120077).
        a.progression_waypoints = {
            { -8762.0f, 2852.0f, 14.0f },    // Overseer Wrex
            { -8852.0f, 2888.0f, 14.02f },   // Katerina the Cleaner
            { -8888.0f, 2888.0f, 14.42f },   // The Questioner
            { -8728.0f, 2932.0f, 6.15f },    // Tidemistress Ca'zass
            { -8890.0f, 2966.0f, 5.89f },    // Blazeroar
        };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeAlcazPrisonScript()
{
    return std::make_unique<AlcazPrisonScript>();
}

} // namespace Playerbot
