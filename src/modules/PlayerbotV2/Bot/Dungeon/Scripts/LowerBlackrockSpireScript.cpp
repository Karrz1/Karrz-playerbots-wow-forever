// LowerBlackrockSpireScript — LBRS (map 229, vanilla 55-60).
// Bosses: Highlord Omokk, Shadow Hunter Vosh'gajin, War Master Voone,
// Mother Smolderweb, Urok Doomhowl, Quartermaster Zigris, Halycon,
// Gizrul the Slavener, Overlord Wyrmthalak.
//
// Authoritative spell IDs from TC source:
//   src/server/scripts/EasternKingdoms/BlackrockMountain/BlackrockSpire/boss_*.cpp

#include "../DungeonScript.h"
#include "../../BotSnapshotView.h"
#include <array>

namespace Playerbot {

namespace {

class LowerBlackrockSpireScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 229; }
    char const* name() const override { return "lbrs"; }
    // Zero static spawns in the Classic creature data: raised by an event the bots never trigger, so a full clear does not wait for it.
    std::vector<uint32_t> event_summoned_bosses() const override
    {
        return {
            10584,   // Urok Doomhowl: summoned by the ogre summoner
            10268,   // Gizrul the Slavener: summoned by the Rend Blackhand event
        };
    }

    DungeonAdvice get_advice(BotSnapshotView const& s) const override
    {
        DungeonAdvice a;
        a.high_priority_kill_entries = {
            10375,  // Spire Spiderling (summoned by Mother Smolderweb, spell 16103)
            9218,   // Spirestone Battle Lord
            10601,  // Urok Enforcer (Urok Doomhowl event summon)
            // Classic casters and healers on map 229 (VMaNGOS spawn and cast data)
            9045,   // Scarshield Acolyte
            9269,   // Smolderthorn Seer
            9239,   // Smolderthorn Mystic
            9262,   // Firebrand Invoker
            9261,   // Firebrand Darkweaver
            9817,   // Blackhand Dreadweaver
            9818,   // Blackhand Summoner
            9240,   // Smolderthorn Shadow Priest
        };
        a.mandatory_interrupt_spells = {
            // Highlord Omokk
            23462,  // Fire Nova
            20691,  // Cleave
            16805,  // Conflagration
            15548,  // Thunderclap
            // Mother Smolderweb
            16104,  // Crystallize
            16468,  // Mother's Milk
            16103,  // Summon Spire Spiderling
            // Gizrul
            16128,  // Infected Bite
            // Quartermaster Zigris
            10101,  // Knock Away
            // Pyroguard Emberseer
            17274,  // Pyroblast
            23341,  // Flame Buffet
            15580,  // Strike
            // Classic spell ids from the VMaNGOS spell_template / converted mob casts
            12039,   // Heal
            12492,   // Healing Wave
            15799,   // Chain Heal
            12466,   // Fireball
            12468,   // Flamestrike
            15242,   // Fireball
            12739,   // Shadow Bolt
            12471,   // Shadow Bolt
            14887,   // Shadow Bolt Volley
            15800,   // Mana Burn
            15785,   // Mana Burn
            15982,   // Healing Wave
            15659,   // Chain Lightning
            16006,   // Chain Lightning
            15305,   // Chain Lightning
        };
        a.cc_priority_entries = {
            9817,   // Blackhand Dreadweaver
            9818,   // Blackhand Summoner
            9240,   // Smolderthorn Shadow Priest
            9098,   // Scarshield Spellbinder
            9261,   // Firebrand Darkweaver
            9262,   // Firebrand Invoker
            9257,   // Scarshield Warlock
            9045,   // Scarshield Acolyte
            10374,   // Spire Spider
        };
        a.dangerous_auras = {
            // Halycon / dragon adds
            16167,  // Rend Mounts
            16359,  // Corrosive Acid
            16390,  // Flame Breath
            // Smolderweb / general
            8269,   // Frenzy
            16495,  // Fatal Bite
            13738,  // Rend
            3391,   // Thrash
            // Emberseer
            13376,  // Fire Shield
            23462,  // Fire Nova
        };
        // Upper Blackrock Spire's ways on. The Dragonspire Hall's seven rooms: the instance opens Emberseer In (175244) once every
        // Blackhand Summoner (9818) and Veteran (9819) of all seven is dead, each room's rune lighting as its last one dies. The rooms
        // open off the hall, out of the route's sight, so the tank walked to the shut door for good. The tank goes in for them
        // (idle:dungeon_clear_spot), one spot per rune (175194-175200, the Classic gameobject data), both entries.
        struct Spot { float x, y, z; };
        static std::array<Spot, 7> const runes = {{
            { 125.4f, -340.5f, 70.9f }, { 155.3f, -353.0f, 70.8f }, { 124.8f, -298.0f, 70.9f }, { 155.3f, -286.1f, 70.9f },
            { 215.2f, -334.7f, 76.8f }, { 228.8f, -301.5f, 76.9f }, { 192.7f, -258.4f, 76.9f },
        }};
        for (Spot const& r : runes)
        {
            a.clear_spots.push_back({ 9818, r.x, r.y, r.z, 12.f });   // Blackhand Summoner
            a.clear_spots.push_back({ 9819, r.x, r.y, r.z, 12.f });   // Blackhand Veteran
        }
        // The Blackrock Altar (175706) wakes Pyroguard Emberseer once three players use it together (classic_go_blackrock_altar:
        // within 8 y, holding its channel, 16532, for 3 s). Bots only joined an altar a person had started; the whole party uses it
        // while Emberseer is still in his cage (idle:dungeon_key_use). Emberseer Out (175153) opens when he dies.
        a.key_uses = { { 175706, 144.4f, -280.9f, 91.5f, 0, 9816, 16532 } };
        // The stadium: a player walking in (area trigger 2026) starts Lord Victor Nefarius's seven waves, then Gyth, then Warchief
        // Rend Blackhand off his back; the exit portcullis (175186) to the Beast and General Drakkisath opens when they are dead.
        a.enter_area_triggers = { 2026 };

        // The boss route is the part of the Spire the bot stands in, the same as Stratholme and Dire Maul: the Lower and the Upper
        // Spire share the instance and its entrance, and one list for both sent an Upper Spire party (the Seal of Ascension opens
        // the Dragonspine Door) to Highlord Omokk first. A bot carrying the Seal of Ascension (12344) is on the Upper Spire, and so
        // is one past the Dragonspine Door (a party member with the Seal opened it): the Upper Spire's whole route lies at z 71.5 and
        // over east of x 28, where the Lower Spire's never climbs past z 61.7. Everyone else is on the Lower Spire. The nearest boss
        // spawn or way in named the part before, and it is wrong in the hall below the door, which both parts walk through: the
        // Lower Spire's way from the entrance to its first room passes the foot of the Upper Spire's stairs right under Emberseer's
        // room (lbrs_full af56: at (97,-306,65) Emberseer was the nearest spot, the tank turned up the stairs for him, the door
        // stayed shut, and the party stood there for 120 s).
        // The boss route is index for index with the waypoints: the owed boss is steered to by its waypoint. A boss with no static
        // spawn (event_summoned_bosses) has no waypoint and is not on it; with one in, every later waypoint answered the wrong boss.
        // Boss spawns from the Classic creature data (VMaNGOS spawn table, map 229), in encounter order.
        //   Lower: Omokk, Vosh'gajin, Voone, Smolderweb, Zigris, Halycon, Wyrmthalak. The way in is the instance entrance.
        //   Upper: Pyroguard Emberseer; Gyth, whose waypoint is the stadium floor inside area trigger 2026 (he is summoned after the
        //   waves, and Rend, whose death is the encounter, comes off his back; the Rend standing on the balcony above is the
        //   event's speaker and leaves before the fight); the Beast; General Drakkisath. The way in is the stairs up to the
        //   Dragonspine Door (route seq 149).
        struct Wing { std::vector<uint32_t> bosses; std::vector<Spot> spots; };
        static std::array<Wing, 2> const wings = {{
            { { 9196,     // Highlord Omokk
                9236,     // Shadow Hunter Vosh'gajin
                9237,     // War Master Voone
                10596,    // Mother Smolderweb
                9736,     // Quartermaster Zigris
                10220,    // Halycon
                9568 },   // Overlord Wyrmthalak (Lower final)
              { { -22.7f, -299.8f, 31.7f }, { -121.2f, -482.2f, 24.7f }, { -17.0f, -459.1f, -18.6f }, { -135.5f, -565.9f, 10.2f },
                { -199.1f, -458.9f, 87.4f }, { -193.9f, -338.1f, 64.5f }, { -22.6f, -486.2f, 90.8f } } },
            { { 9816,     // Pyroguard Emberseer
                10339,    // Gyth (summoned: the stadium event)
                10430,    // The Beast
                10363 },  // General Drakkisath (Upper final)
              { { 144.4f, -258.0f, 96.4f }, { 160.0f, -420.3f, 110.8f }, { 124.2f, -563.8f, 107.4f }, { 36.0f, -286.0f, 111.0f } } },
        }};
        // Gyth comes after the waves (about a minute each, seven of them) and the talk: the tank holds the stadium floor for him
        // instead of waiving him after the usual 8 s and walking on to the shut exit portcullis.
        a.owed_boss_holds = { { 10339, 600000 } };
        auto add = [&a](Wing const& w)
        {
            a.bosses.insert(a.bosses.end(), w.bosses.begin(), w.bosses.end());
            for (Spot const& p : w.spots)
                a.progression_waypoints.push_back({ p.x, p.y, p.z });
        };
        // No bot in the map (the GM audit and list commands pass an empty view): both parts.
        if (s.map_id() != map_id())
        {
            for (Wing const& w : wings)
                add(w);
            return a;
        }
        constexpr uint32_t kSealOfAscension = 12344;
        constexpr float kPastDoorMinX = 20.f, kPastDoorMinZ = 69.f;   // the Dragonspine Door stands at z 71.5
        float px, py, pz;
        s.position(px, py, pz);
        bool const upper = s.has_item(kSealOfAscension) || (px >= kPastDoorMinX && pz >= kPastDoorMinZ);
        add(wings[upper ? 1 : 0]);
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeLowerBlackrockSpireScript()
{
    return std::make_unique<LowerBlackrockSpireScript>();
}

} // namespace Playerbot
