// StratholmeScript — Stratholme (map 329, vanilla 55-60).
// Two wings: Live (Scarlet) and Undead (Baron). Major bosses:
// Magistrate Barthilas, Cannon Master Willey, Postmaster Malown,
// Baroness Anastari, Ramstein the Gorger, Baron Rivendare, etc.
//
// Authoritative spell IDs from TC source:
//   src/server/scripts/EasternKingdoms/Stratholme/boss_*.cpp

#include "../DungeonScript.h"
#include "../../BotSnapshotView.h"
#include <array>
#include <limits>

namespace Playerbot {

namespace {

class StratholmeScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 329; }
    char const* name() const override { return "stratholme"; }
    // Zero static spawns in the Classic creature data: raised by an event the bots never trigger, so a full clear does not wait for it.
    // Ramstein the Gorger is not one: the Baron summons him in Slaughter Square once its abominations are dead, which the bots do
    // (he is on the Undead wing's route below).
    std::vector<uint32_t> event_summoned_bosses() const override
    {
        return {
            10516,   // The Unforgiven: summoned by the Crimson / courtyard event
        };
    }

    DungeonAdvice get_advice(BotSnapshotView const& s) const override
    {
        DungeonAdvice a;
        a.high_priority_kill_entries = {
            10398,   // Thuzadin Shadowcaster
            10400,   // Thuzadin Necromancer
            10423,   // Crimson Priest
            10420,   // Crimson Initiate
            10419,   // Crimson Conjuror
            10422,   // Crimson Sorcerer
            10426,   // Crimson Inquisitor
            10425,   // Crimson Battle Mage
            10463,   // Shrieking Banshee
        };
        a.mandatory_interrupt_spells = {
            // Baron Rivendare
            17393,  // Shadowbolt
            15708,  // Mortal Strike
            17473,  // Raise Dead
            // Baroness Anastari
            16565,  // Banshee Wail
            16867,  // Banshee Curse
            18327,  // Silence
            17244,  // Possess
            // Balnazzar
            17286,  // Crusader's Hammer (AOE stun)
            17281,  // Crusader Strike
            17284,  // Holy Strike
            17399,  // Shadow Shock
            17287,  // Mind Blast
            13704,  // Psychic Scream
            12098,  // Sleep
            15690,  // Mind Control
            // Maleki the Pallid
            17503,  // Frostbolt
            20743,  // Drain Life
            17243,  // Drain Mana
            16869,  // Ice Tomb
            // Hearthsinger Forresten
            // Magistrate Barthilas (Cannon Master Willey trash)
            10101,  // Knock Away
            15615,  // Pummel
            // Nerub'enkan
            4962,   // Encasing Webs
            6016,   // Pierce Armor
            31602,  // Crypt Scarabs
            17235,  // Raise Undead Scarab
            // Classic spell ids from the VMaNGOS spell_template / converted mob casts
            15586,   // Heal
            17138,   // Flash Heal
            15238,   // Holy Smite
            17142,   // Holy Fire
            13323,   // Polymorph
            12675,   // Frostbolt
            15232,   // Shadow Bolt
            11443,   // Cripple
            16868,   // Banshee Wail
            15493,   // Holy Light
            15785,   // Mana Burn
        };
        a.cc_priority_entries = {
            10419,   // Crimson Conjuror
            10420,   // Crimson Initiate
            10422,   // Crimson Sorcerer
            10423,   // Crimson Priest
            10426,   // Crimson Inquisitor
            10425,   // Crimson Battle Mage
            10398,   // Thuzadin Shadowcaster
            10400,   // Thuzadin Necromancer
        };
        a.dangerous_auras = {
            // Postmaster Malown adds
            16793,  // Draining Blow
            10887,  // Crowd Pummel
            14099,  // Mighty Blow
            16791,  // Furious Anger
            // Cannonmaster
            16496,  // Shoot
            // Baron + adds
            17467,  // Unholy Aura
            17246,  // Possessed (Anastari debuff)
            // Ramstein
            5568,   // Trample
            17307,  // Knockout
            // Timmy
            17470,  // Ravenous Claw
            // Stratholme courier
            7713,   // Wailing Dead
            6253,   // Backhand
            8552,   // Curse of Weakness
            12889,  // Curse of Tongues
            17831,  // Call of the Grave
        };
        // The acolytes: each ziggurat's door opens when its boss dies, and inside, five Thuzadin Acolytes (10399) keep the Ash'ari
        // Crystal above them alive; the crystal dies with the last of them (classic_mobs_cristal_zuggurat). They stand 57 y of walk
        // from the route, out of its sight, so the tank walked on from each ziggurat boss and the square never opened. The tank goes
        // in for them (idle:dungeon_clear_spot).
        a.clear_spots = {
            { 10399, 3847.3f, -3750.0f, 145.2f, 15.f },   // Baroness Anastari's ziggurat
            { 10399, 3839.7f, -3498.3f, 141.3f, 15.f },   // Nerub'enkan's ziggurat
            { 10399, 4059.1f, -3667.0f, 132.7f, 15.f },   // Maleki the Pallid's ziggurat
        };
        // The boss route is the wing the bot stands in, the same as Dire Maul and Scarlet Monastery. One list for both sent a stratud
        // (service gate) party to Hearthsinger Forresten in the Scarlet half and then straight to Baron Rivendare, past the three
        // ziggurats whose bosses open Slaughter Square, so the Baron's gate never opened; a stratlive (main gate) party was sent on to
        // the Baron after Galford, never to Balnazzar. The nearest wing entrance (the stratlive/stratud scenario starts) or boss spawn
        // names the wing. Boss spawns from the Classic creature data (VMaNGOS spawn table, map 329), in encounter order.
        //   Scarlet: Forresten (one of his four spawns), Malor, Galford, Grand Crusader Dathrohan (he turns into Balnazzar).
        //   Undead: Barthilas at the service gate, the three ziggurats (Anastari, Nerub'enkan, Maleki: the instance opens Slaughter
        //   Square when the Thuzadin Acolyte under each one's crystal is dead), Ramstein where the instance summons him once the
        //   square's abominations are dead (classic_instance_stratholme TYPE_RAMSTEIN), Rivendare, who cannot be attacked until
        //   Ramstein is dead.
        struct Spot { float x, y, z; };
        struct Wing { std::vector<uint32_t> bosses; std::vector<Spot> spots; Spot entrance; };
        static std::array<Wing, 2> const wings = {{
            { { 10558,    // Hearthsinger Forresten
                11032,    // Malor the Zealous
                10811,    // Archivist Galford
                10812 },  // Grand Crusader Dathrohan / Balnazzar (Scarlet final)
              { { 3595.8f, -3509.9f, 137.5f }, { 3621.1f, -3126.8f, 135.7f }, { 3456.0f, -3103.4f, 136.5f }, { 3413.6f, -3042.0f, 136.5f } },
              { 3392.92f, -3395.03f, 143.135f } },
            { { 10435,    // Magistrate Barthilas
                10436,    // Baroness Anastari
                10437,    // Nerub'enkan
                10438,    // Maleki the Pallid
                10439,    // Ramstein the Gorger (summoned)
                10440 },  // Baron Rivendare (Undead final)
              { { 3663.2f, -3619.1f, 138.0f }, { 3855.3f, -3715.8f, 148.2f }, { 3855.0f, -3528.8f, 144.3f }, { 4035.8f, -3647.8f, 135.7f },
                { 4032.35f, -3380.57f, 119.74f }, { 4032.8f, -3333.0f, 115.1f } },
              { 3590.87f, -3643.22f, 138.491f } },
        }};
        // Ramstein has no spawn until the abominations are dead: the tank holds the square for them instead of waiving him after
        // the usual 8 s and walking on to the Baron's closed gate.
        a.owed_boss_holds = { { 10439, 300000 } };
        auto add = [&a](Wing const& w)
        {
            a.bosses.insert(a.bosses.end(), w.bosses.begin(), w.bosses.end());
            for (Spot const& p : w.spots)
                a.progression_waypoints.push_back({ p.x, p.y, p.z });
        };
        // No bot in the map (the GM audit and list commands pass an empty view): both wings.
        if (s.map_id() != map_id())
        {
            for (Wing const& w : wings)
                add(w);
            return a;
        }
        float px, py, pz;
        s.position(px, py, pz);
        Wing const* in = &wings[0];
        float best = std::numeric_limits<float>::max();
        for (Wing const& w : wings)
        {
            auto consider = [&](Spot const& p)
            {
                float const d = (p.x - px) * (p.x - px) + (p.y - py) * (p.y - py);
                if (d < best) { best = d; in = &w; }
            };
            consider(w.entrance);
            for (Spot const& p : w.spots)
                consider(p);
        }
        add(*in);
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeStratholmeScript()
{
    return std::make_unique<StratholmeScript>();
}

} // namespace Playerbot
