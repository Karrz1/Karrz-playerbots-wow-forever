// HallOfThanesScript - The Hall of Thanes (Forever, map 3065, a level 16 party in the scenarios).
// Encounters (classic_instance_hall_of_thanes.cpp, from the official dungeon sniff of 2026-10-12): Faldrim Anvilmar, Magmatus
// ("Infurnus"), Plunder, Durgen Dirgehammer. The doors decide the way, as the sniff showed them:
//   - the Iron Doors 645391 and 645392 on the straight hall stay shut until the Ancient Switch in Durgen's room, so the way in
//     to Faldrim is round by the north (the open Portcullises 642858, 648668 and 642026);
//   - Faldrim's death opens the Portcullises 642024 and 642025 out of Anvilmar's Rest, the way south to Magmatus;
//   - Plunder's death opens the Iron Door 645389 on his hallway east;
//   - the Vault Door 645393 into Durgen's room has no lock and no script: players open it, so the tank does (hand_doors below;
//     its data is the same as the scripted Iron Doors', so only the dungeon can name it).
//
// The same gap as City of Dalaran, Krol'dok and Ruins of Lordaeron: without a script the tank has no boss to walk to and a
// tank-led group stands idle once the trash it can see is dead.

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class HallOfThanesScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 3065; }
    char const* name() const override { return "hallofthanes"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        // A Dark Iron Summoner calls a Fiery Assistant (1291218) every 30-35 s for as long as it lives: it dies first.
        a.high_priority_kill_entries = {
            263438,   // Dark Iron Summoner (Summon Fiery Assistant)
        };
        // The instance's encounter order, which is also the only order the doors allow; the 0049 route walks it.
        a.bosses = {
            261306,   // Faldrim Anvilmar (Anvilmar's Rest)
            261316,   // Magmatus
            261311,   // Plunder
            261319,   // Durgen Dirgehammer
        };
        // The waypoints are the boss spawns (world 2026_10_13_01, the official sniff), index for index with the bosses above.
        a.progression_waypoints = {
            { 61.1f, 8.2f, 501.6f },      // Faldrim Anvilmar
            { 164.2f, -195.5f, 486.3f },  // Magmatus
            { 129.9f, 0.0f, 500.2f },     // Plunder
            { 351.6f, -9.8f, 500.8f },    // Durgen Dirgehammer
        };
        return a;
    }

    // The Vault Door into Durgen's room (645393): opened by hand, by the tank as he comes to it (idle:dungeon_open_hand_door).
    std::vector<uint32_t> hand_doors() const override { return { 645393 }; }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeHallOfThanesScript()
{
    return std::make_unique<HallOfThanesScript>();
}

} // namespace Playerbot
