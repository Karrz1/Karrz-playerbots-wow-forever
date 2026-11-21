// CityOfDalaranScript - City of Dalaran (Forever, map 2959, levels 28-33).
// The Sewer Grate lands the party at the sewer start (310,384). Two sewer bosses first (Atrexis, then Lyn below him); the door to
// the city opens once both are dead (classic_instance_city_of_dalaran.cpp). In the city the three square bosses (Arcane Anomaly,
// Arcanic Enigma, Unstable Sentinel) open the Prismatic Doors to Fel Ancient, and Mana Devourer plus Fel Ancient open the portal
// up to the Shade of the Archmage.
//
// Without a script the tank had no boss to walk to: once Atrexis was dead it cleared what trash it could see, then stood at
// (249,278) with nothing in its 150 y scan while Lyn waited 105 y south (cod_lyn 0136 round 22: [tank_adv_diag] NO TARGET
// script_bosses=0 wp=0/0, then 700 s of wandering round (320,300), goal not reached).

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class CityOfDalaranScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 2959; }
    char const* name() const override { return "cityofdalaran"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        // A Kirin Tor Necromancer raises a Skeleton every 13-16 s (Raise Dead 1303266) for as long as it lives: cod_full af53 (20 Nov) stood
        // 140 s by Atrexis' room killing one Skeleton after another from three Necromancers, the Necromancers last. They die first, then
        // the Dark Casters (Heal 1303227 on their pack).
        a.high_priority_kill_entries = {
            269134,   // Kirin Tor Necromancer (Raise Dead)
            255072,   // Dark Caster (Heal)
        };
        // The order of the 0048 route: the shortest walk on the 2959 navmesh that the doors allow (the sewer bosses first; Fel
        // Ancient after the three city squares; the portal last), about 1830 y. Mana Devourer has no door, so he comes first in the
        // city, where the climb from the sewers comes out (INFERRED: no live route data).
        a.bosses = {
            247126,   // Atrexis the Grave Knight (sewers)
            247032,   // Lyn the Ignored (sewers, below Atrexis)
            246008,   // Mana Devourer
            245999,   // Arcane Anomaly (city square)
            246016,   // Arcanic Enigma (city square)
            246017,   // Unstable Sentinel (city square)
            246003,   // Fel Ancient (behind the Prismatic Doors)
            246020,   // Shade of the Archmage (the Violet Chamber, through the portal)
        };
        // The waypoints are the boss spawns (world DB), index for index with the bosses above; the Shade's is the Portal To the
        // Violet Chamber, since no navmesh joins his room to the city.
        a.progression_waypoints = {
            { 235.0f, 313.0f, 58.6f },    // Atrexis the Grave Knight
            { 226.9f, 175.7f, 59.3f },    // Lyn the Ignored
            { 93.9f, 353.7f, 98.6f },     // Mana Devourer
            { 71.5f, 224.6f, 108.1f },    // Arcane Anomaly
            { 346.9f, 182.8f, 89.6f },    // Arcanic Enigma
            { 362.9f, 263.2f, 88.3f },    // Unstable Sentinel
            { 240.5f, 310.7f, 96.4f },    // Fel Ancient
            { 428.6f, 375.0f, 129.1f },   // Shade of the Archmage: the Portal To the Violet Chamber (656243)
        };
        // The portal works once Mana Devourer and Fel Ancient are dead (classic_go_cod_portal_to_parlor) and lands the bot 16 y
        // from the Shade.
        a.portal_hops = { { 656243, 428.62f, 375.01f, 129.08f, 246020, { 246008, 246003 } } };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeCityOfDalaranScript()
{
    return std::make_unique<CityOfDalaranScript>();
}

} // namespace Playerbot
