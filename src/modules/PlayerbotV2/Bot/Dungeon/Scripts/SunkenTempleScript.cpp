// SunkenTempleScript — Temple of Atal'Hakkar / Sunken Temple
// (map 109, vanilla 50-60). Two-tier troll temple.
//   * Avatar of Hakkar event — 6 Atal'ai Defenders summon Avatar.
//     Adds priority kill.
//   * Jammal'an the Prophet + Ogom the Wretched — Jammal'an heals
//     Ogom; interrupt the heal.
//   * Hakkari Bloodkeepers / Priests — caster trash, CC priority.
//   * Shade of Eranikus (final tier) — Sleep aura debuff.

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class SunkenTempleScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 109; }
    char const* name() const override { return "sunken_temple"; }
    // Zero static spawns in the Classic creature data: raised by an event the bots never trigger, so a full clear does not wait for it.
    std::vector<uint32_t> event_summoned_bosses() const override
    {
        return {
            8443,   // Avatar of Hakkar: summoned by the Atal'ai braziers
        };
    }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        a.high_priority_kill_entries = {
            5273,   // Atal'ai High Priest
            5259,   // Atal'ai Witch Doctor
            5271,   // Atal'ai Deathwalker
            5291,   // Hakkari Frostwing
        };
        a.mandatory_interrupt_spells = {
            12039,  // Holy Light (Jammal'an heals Ogom)
            12559,  // Mind Blast (Hakkari Priest)
            12544,  // Frost Armor (Hakkari)
            // Classic spell ids from the VMaNGOS spell_template / converted mob casts
            11986,   // Healing Wave
            12058,   // Chain Lightning
            9532,   // Lightning Bolt
            12096,   // Fear
            8398,   // Frostbolt Volley
            9591,   // Acid Spit
            12151,   // Summon Atal'ai Skeleton
        };
        a.cc_priority_entries = {
            5259,   // Atal'ai Witch Doctor
            5271,   // Atal'ai Deathwalker
            5273,   // Atal'ai High Priest
        };
        a.dangerous_auras = {
            12480,  // Sleep (Shade of Eranikus) — interrupted by damage
        };
        // How the temple opens (classic_instance_sunken_temple):
        //   The six Atal'ai Statues on the lower floor, used in the order S, N, SW, SE, NW, NE, raise Atal'alarion in the pit
        //   below them; a statue used out of turn springs a trap. Each used in turn turns unselectable. Atal'alarion is an encounter
        //   of the instance, so a temple with him alive never counted as cleared. The tank uses them (idle:dungeon_key_use, in
        //   order, tank only); the statues are 370-540 y of walk apart, each on its own ledge.
        //   The six Atal'ai Defenders (Zolo, Gasher, Loro, Hukku, Zul'Lor, Mijan), each on his own balcony 23 y above the upper floor
        //   at the top of its own stairs, open Jammal'an's forcefield (149431) and make Jammal'an and Ogom attackable when the last
        //   of them dies. The route stays on the floors and never went up, so the tank stood at the forcefield for good. The tank
        //   goes up for them (idle:dungeon_clear_spot, reach widened to the whole temple: 300-450 y of walk between balconies).
        //   Jammal'an's death makes the Shade of Eranikus attackable and brings out Weaver and Dreamscythe (hidden till then).
        // Spawn positions from the Classic data (VMaNGOS spawn tables, map 109).
        a.key_uses = {
            { 148830, -515.6f, 95.3f, -148.7f, 0, 0, 0, 250.f, 70.f, true, true },   // Atal'ai Statue, S
            { 148831, -419.8f, 94.5f, -148.7f, 0, 0, 0, 250.f, 70.f, true, true },   // N
            { 148832, -491.4f, 136.0f, -148.7f, 0, 0, 0, 250.f, 70.f, true, true },  // SW
            { 148833, -491.5f, 53.5f, -148.7f, 0, 0, 0, 250.f, 70.f, true, true },   // SE
            { 148834, -443.9f, 136.1f, -148.7f, 0, 0, 0, 250.f, 70.f, true, true },  // NW
            { 148835, -443.4f, 53.8f, -148.7f, 0, 0, 0, 250.f, 70.f, true, true },   // NE
        };
        a.clear_spots = {
            { 5715, -405.5f, 60.5f, -67.1f, 10.f, 400.f, 130.f },    // Hukku
            { 5717, -406.2f, 131.1f, -66.9f, 10.f, 400.f, 130.f },   // Mijan
            { 5716, -467.4f, 166.0f, -66.7f, 10.f, 400.f, 130.f },   // Zul'Lor
            { 5712, -528.6f, 130.2f, -66.8f, 10.f, 400.f, 130.f },   // Zolo
            { 5713, -528.0f, 59.5f, -66.7f, 10.f, 400.f, 130.f },    // Gasher
            { 5714, -467.2f, 26.3f, -67.0f, 10.f, 400.f, 130.f },    // Loro
        };
        // The boss route, index for index with the waypoints below (the 0053 route walks them in this order): Atal'alarion in the
        // pit, Morphaz and Hazzas, Jammal'an, Weaver and Dreamscythe (their spawn points; they walk a path once they come out), the
        // Shade of Eranikus. The old list had Weaver and Dreamscythe first, before they exist.
        // The six Defenders are kill spots above, not entries here: the owed boss is bosses[n] for n instance encounters done, and
        // they are not encounters of the instance (its eight encounters are the seven below and the Avatar). As entries, every
        // later kill would point the owed index back at a dead Defender, and the tank would walk back up the balconies to wait
        // out each one.
        a.bosses = {
            8580,   // Atal'alarion (the statues)
            5719,   // Morphaz
            5722,   // Hazzas
            5710,   // Jammal'an the Prophet (the six Defenders)
            5720,   // Weaver (comes out at Jammal'an's death)
            5721,   // Dreamscythe (comes out at Jammal'an's death)
            5709,   // Shade of Eranikus (final)
        };
        a.progression_waypoints = {
            { -466.5f, 95.2f, -188.9f },   // Atal'alarion
            { -671.6f, 73.3f, -90.8f },    // Morphaz
            { -670.1f, 68.7f, -90.8f },    // Hazzas
            { -426.7f, -85.7f, -88.1f },   // Jammal'an the Prophet
            { -425.1f, 105.8f, -90.8f },   // Weaver
            { -508.8f, 85.1f, -90.9f },    // Dreamscythe
            { -660.0f, -34.0f, -90.7f },   // Shade of Eranikus
        };
        // Atal'alarion is hidden until the six statues are used (a long walk round the lower floor), Weaver and Dreamscythe until
        // Jammal'an dies: the tank waits for them at their waypoints instead of waiving them after the usual 8 s.
        a.owed_boss_holds = { { 8580, 600000 }, { 5720, 120000 }, { 5721, 120000 } };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeSunkenTempleScript()
{
    return std::make_unique<SunkenTempleScript>();
}

} // namespace Playerbot
