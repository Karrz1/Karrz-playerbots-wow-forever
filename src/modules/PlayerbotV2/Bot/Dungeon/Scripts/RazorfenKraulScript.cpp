// RazorfenKraulScript — Razorfen Kraul (map 47, vanilla 25-35).
// Quilboar / pig-people dungeon. Notable:
//   * Charlga Razorflank's Earthbind Totem + Curse of Blood — totem
//     priority-kill, debuff dispel.
//   * Aggem Thorncurse summons Razorfen pests; light add priority.
//   * Death Speaker Jargba's Bone Shield + Power Word: Shield —
//     interruptible cast.

#include "../DungeonScript.h"
#include "../../ClassTables.h"

namespace Playerbot {

namespace {

// Classic 1.60 (Forever): the vanilla dungeon. The script below carries the retail remake's ids, which do not exist there.
DungeonAdvice RazorfenKraulClassicAdvice()
{
    DungeonAdvice a;
    // Creature ids and spell ids from the VMaNGOS data Forever's Classic world was built from (creature_template, creature spawns,
    // spell_template); the casts come from the converted EventAI / spell lists in 2026_09_28_15_world_vanilla_smartai.sql.
    a.high_priority_kill_entries = {
        4517,   // Death's Head Priest
        4518,   // Death's Head Sage
        4519,   // Death's Head Seer
        4440,   // Razorfen Totemic
        4516,   // Death's Head Adept
        4515,   // Death's Head Acolyte
        4520,   // Razorfen Geomancer
        4525,   // Razorfen Earthbreaker
        4427,   // Ward Guardian
    };
    a.mandatory_interrupt_spells = {
        6063,   // Heal
        959,   // Healing Wave
        14900,   // Chain Heal
        8292,   // Chain Bolt
        14515,   // Dominate Mind
        15785,   // Mana Burn
        9672,   // Frostbolt
        113,   // Chains of Ice
        9613,   // Shadow Bolt
        9532,   // Lightning Bolt
        8272,   // Mind Tremor
        6728,   // Enveloping Winds
    };
    a.cc_priority_entries = {
        4517,   // Death's Head Priest
        4516,   // Death's Head Adept
        4520,   // Razorfen Geomancer
        4525,   // Razorfen Earthbreaker
    };
    a.bosses = {
        4424,   // Aggem Thorncurse
        4428,   // Death Speaker Jargba
        4420,   // Overlord Ramtusk
        4422,   // Agathelos the Raging
        4421,   // Charlga Razorflank
    };
    a.progression_waypoints = {
        { 2082.3f, 1463.5f, 73.2f },   // Aggem Thorncurse
        { 2146.4f, 1411.2f, 74.0f },   // Death Speaker Jargba
        { 2203.1f, 1640.0f, 85.9f },   // Overlord Ramtusk
        { 1994.4f, 1976.4f, 63.3f },   // Agathelos the Raging
        { 2190.4f, 1864.3f, 79.1f },   // Charlga Razorflank
    };
    return a;
}

class RazorfenKraulScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 47; }
    char const* name() const override { return "razorfen_kraul"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        if (IsVanillaRealm())
            return RazorfenKraulClassicAdvice();
        DungeonAdvice a;
        a.high_priority_kill_entries = {
            7437,   // Earthbind Totem (Charlga)
        };
        a.mandatory_interrupt_spells = {
            12544,  // Frost Armor (Razorfen Defenders)
            7657,   // Bone Shield (Death Speaker)
        };
        // Boss progression — aligned to the LIVE instance_razorfen_kraul
        // encounter list (RFKBossIds order). The pre-revamp entries this list
        // used to carry (Aggem Thorncurse 4424, Death Speaker Jargba 4428) are
        // NOT tracked encounters in the modern RFK script and 4424 has no static
        // spawn at all, so the tank-advance rule was steering to a creature that
        // never exists. All five below are verified map-47 spawns and match the
        // InstanceScript's SetBossState credit creatures.
        a.bosses = {
            75001,  // Hunter Bonetusk
            6168,   // Roogug
            74462,  // Warlord Ramtusk
            75247,  // Groyat, the Blind Hunter
            4421,   // Charlga Razorflank (final)
        };
        // Progression waypoints — Razorfen Kraul is a Barrens quilboar
        // open-air dungeon with linear path.
        a.progression_waypoints = {
            { 1942.0f, 1546.0f,  77.0f },   // entry
            { 2000.0f, 1610.0f,  82.0f },   // Roogug courtyard
            { 2058.0f, 1638.0f,  92.0f },   // Aggem altar
            { 2095.0f, 1740.0f,  92.0f },   // Jargba ritual
            { 2050.0f, 1812.0f,  93.0f },   // Charlga throne
        };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeRazorfenKraulScript()
{
    return std::make_unique<RazorfenKraulScript>();
}

} // namespace Playerbot
