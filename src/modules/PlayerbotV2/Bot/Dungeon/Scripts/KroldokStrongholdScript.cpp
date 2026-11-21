// KroldokStrongholdScript - Krol'dok Stronghold (Forever, map 3039, levels 40-45).
// Encounters (DungeonEncounter.db2): Slavecrusher Grox, Twilight Commander Zarmok, Handler Ashka, Nar'gurk, Overlord Mugg.
// Bossman Toof (264773) guards the way to Mugg but has no encounter (classic_instance_kroldok_stronghold.cpp), so he is not a
// boss here: the boss list is counted against the instance's encounters.
//
// The same gap as City of Dalaran and Excavation Site (0136 round 22): without a script the tank has no boss to walk to and
// only clears the trash it can see.

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class KroldokStrongholdScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 3039; }
    char const* name() const override { return "kroldokstronghold"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        // The shortest walk from the landing that ends at Mugg, on the 3039 navmesh (Toof stands on the way from Nar'gurk to
        // Mugg); the 0046 route walks it.
        a.bosses = {
            258966,   // Slavecrusher Grox
            258972,   // Twilight Commander Zarmok
            259281,   // Handler Ashka
            258964,   // Nar'gurk
            258968,   // Overlord Mugg
        };
        // The waypoints are the boss spawns (world 2026_11_19_66: where the live combat logs put them), index for index with
        // the bosses above.
        a.progression_waypoints = {
            { -7488.8f, -3874.4f, 127.6f },   // Slavecrusher Grox
            { -7504.3f, -4082.4f, 151.5f },   // Twilight Commander Zarmok
            { -7247.9f, -4493.9f, 177.5f },   // Handler Ashka
            { -7317.9f, -4019.7f, 264.4f },   // Nar'gurk
            { -6831.8f, -4583.3f, 241.3f },   // Overlord Mugg
        };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeKroldokStrongholdScript()
{
    return std::make_unique<KroldokStrongholdScript>();
}

} // namespace Playerbot
