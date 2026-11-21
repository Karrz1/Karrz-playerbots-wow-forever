// DrownedCityScript - The Drowned City (Forever, map 2970, levels 35-40).
// Encounters (DungeonEncounter.db2): Zul'Alai, Var'Taka, Captain Dreadrise, Deathless Guardians (Deathless Marrow), Primeval
// Elemental, Min'loth the Serpent. The Primeval Elemental is optional on live (killed in one run of three), awake from the
// start in the pool of the flooded quarter.
//
// The same gap as City of Dalaran and Excavation Site (0136 round 22): without a script the tank has no boss to walk to and
// only clears the trash it can see.

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class DrownedCityScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 2970; }
    char const* name() const override { return "drownedcity"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        // The shortest walk from the landing that ends at Min'loth, on the 2970 navmesh; the 0047 route walks it.
        a.bosses = {
            258905,   // Zul'Alai
            258924,   // Var'Taka
            260042,   // Captain Dreadrise
            260202,   // Deathless Marrow
            263886,   // Primeval Elemental
            260243,   // Min'loth the Serpent
        };
        // The waypoints are the boss spawns (world 2026_11_19_67: where the live combat logs put them), index for index with
        // the bosses above.
        a.progression_waypoints = {
            { -13321.6f, 2460.8f, 0.6f },     // Zul'Alai
            { -13506.6f, 2787.7f, -10.8f },   // Var'Taka
            { -13094.0f, 2739.3f, -32.9f },   // Captain Dreadrise
            { -13323.0f, 3073.2f, -6.1f },    // Deathless Marrow
            { -13496.5f, 2587.4f, 0.1f },     // Primeval Elemental
            { -13322.7f, 3335.9f, 0.2f },     // Min'loth the Serpent
        };
        // The healers of the Brinescale and Saltseer packs (Healing Wave in the live combat logs).
        a.mandatory_interrupt_spells = { 1282611, 1283680 };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeDrownedCityScript()
{
    return std::make_unique<DrownedCityScript>();
}

} // namespace Playerbot
