// RuinsOfLordaeronScript - Ruins of Lordaeron (Forever, map 2999, a level 18 party in the scenarios).
// Encounters (classic_instance_ruins_of_lordaeron.cpp, from the official dungeon sniff): Witherfang, Rath'mael, The Butcher
// (the creature is The Baron), The Abandoned (comes out of the statue) and Bjork. Lordaeron Captain and Viktor the Vile have
// no boss id there yet, so they are not bosses here: the boss list is counted against the instance's encounters.
//
// The same gap as City of Dalaran, Excavation Site and Krol'dok (0136 round 22): without a script the tank has no boss to walk
// to. A player's tank-led group stalled here (bug bundle of 6 Nov): after the first pulls the tank logged
// [tank_adv_diag] NO TARGET ... script_bosses=0 wp=0/0 between (1719,384) and (1621,380), and the group stood
// cohered_idle for over 350 s until the player took the lead.

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class RuinsOfLordaeronScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 2999; }
    char const* name() const override { return "ruinsoflordaeron"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        // The walking order, from the dungeon's navmesh (r2, 20 Nov). Rath'mael's graveyard has a low wall round it with one way
        // in, on its east side, which passes the statue: a player within 25 y of it starts The Abandoned's waves. The encounter order
        // (Witherfang, Rath'mael, The Butcher, The Abandoned, Bjork) walked the tank 870 y from Witherfang round to that way in, past
        // the statue, with Rath'mael still owed: the waves came while the group was on its way to the graveyard (af52 rol_full: the
        // priest out of mana, wiped at 1331 s). This order is a loop of 1395 y from the entrance (the old one 2225 y): Witherfang,
        // The Baron 370 y on, the statue 361 y, the graveyard 292 y, Bjork 209 y. Leg lengths: navmesh paths on the dev tiles.
        a.bosses = {
            250483,   // Witherfang
            250660,   // The Baron (The Butcher encounter)
            250631,   // The Abandoned (comes out of the statue)
            250657,   // Rath'mael
            256097,   // Bjork
        };
        // Index for index with the bosses above: the boss spawns of the official sniff (world 2026_10_11_01). Witherfang walks a
        // patrol path; this is its sniffed spawn point. The Abandoned has no spawn: the statue's stalker summons him and he lands
        // in the courtyard, at the point his script flies him to (world 2026_10_11_03), 18 y from the stalker. The statue itself is
        // off the navmesh.
        a.progression_waypoints = {
            { 1745.0f, 58.4f, 61.7f },    // Witherfang
            { 1470.4f, 167.1f, 61.6f },   // The Baron
            { 1599.2f, 359.9f, 62.5f },   // The Abandoned
            { 1530.0f, 311.6f, 61.7f },   // Rath'mael
            { 1700.3f, 319.7f, 61.8f },   // Bjork
        };
        // The Abandoned is in no scan until he comes out, 1.5 min after the third wave: about 4 min 36 s after the group reaches
        // the statue (three 90 s waits and 5.7 s). The waiver waits 5 min for him there, not 8 s.
        a.owed_boss_holds = { { 250631, 300000 } };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeRuinsOfLordaeronScript()
{
    return std::make_unique<RuinsOfLordaeronScript>();
}

} // namespace Playerbot
