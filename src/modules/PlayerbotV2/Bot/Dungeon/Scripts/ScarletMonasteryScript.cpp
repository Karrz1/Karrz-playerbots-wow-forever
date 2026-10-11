// ScarletMonasteryScript — Scarlet Monastery (map 189, vanilla 28-40).
// Four wings (Graveyard/Library/Armory/Cathedral). Bosses include
// Whitemane + Mograine duo, Headmaster Houndmaster Loksey, Arcanist
// Doan, Bloodmage Thalnos, Herod, Interrogator Vishas, Headless
// Horseman (event).
//
// Authoritative spell IDs from TC source:
//   src/server/scripts/EasternKingdoms/ScarletMonastery/boss_*.cpp

#include "../DungeonScript.h"
#include "../../ClassTables.h"

namespace Playerbot {

namespace {

class ScarletMonasteryScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 1004; }   // Scarlet Monastery MoP remake (189 = classic-era SM; audit B31)
    char const* name() const override { return "scarlet_monastery"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        a.high_priority_kill_entries = {
            60033,  // Frenzied Spirit (Thalnos the Soulrender adds)
        };
        a.mandatory_interrupt_spells = {
            // Arcanist Doan
            8988,   // Silence
            9433,   // Arcane Explosion
            9435,   // Detonation
            9438,   // Arcane Bubble
            13323,  // Polymorph
            // Bloodmage Thalnos
            17831,  // Call of the Grave
            7399,   // Terrify
            7290,   // Soul Siphon
            // Herod
            8053,   // Flame Shock
            1106,   // Shadowbolt
            8814,   // Flame Spike
            16079,  // Fire Nova
        };
        a.cc_priority_entries = {
            58590,  // Scarlet Zealot (heals)
            58569,  // Scarlet Purifier (Flash of Steel / Purifying Flames)
        };
        a.dangerous_auras = {
            // Headless Horseman event
            42413,  // Head Visual
            42587,  // Cleave
            42380,  // Conflagration
            43116,  // Horseman's Whirlwind
        };
        // Boss progression — map 1004 is the MoP-remake Scarlet
        // Monastery (Graveyard + Cathedral). Classic-era boss entries
        // (Loksey/Doan/Herod/Mograine, old map 189) have no spawns
        // here; the remake roster is spawn-verified on map 1004.
        a.bosses = {
            59789,  // Thalnos the Soulrender (Graveyard)
            59223,  // Brother Korloff (training grounds)
            60040,  // Commander Durand (Cathedral, duo with Whitemane)
            3977,   // High Inquisitor Whitemane (final)
        };
        // Progression waypoints — boss spawn positions from world.creature
        // (map 1004), in encounter order. Coarse route skeleton; the
        // pathfinder handles corridors between them.
        a.progression_waypoints = {
            { 1124.7f,  688.5f,   1.3f },   // Thalnos the Soulrender (Graveyard)
            {  858.5f,  598.6f,  10.1f },   // Brother Korloff (training grounds)
            {  748.0f,  606.0f,  15.1f },   // Commander Durand (Cathedral)
            {  700.5f,  605.8f,  11.6f },   // High Inquisitor Whitemane (final)
        };
        return a;
    }
};

// Scarlet Monastery as Classic 1.60 has it (map 189: Graveyard, Library, Armory and Cathedral in one map). The class above is the
// MoP remake (map 1004). Whitemane resurrects Mograine and heals: she is killed first and her heals are kicked.
class ScarletMonasteryClassicScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 189; }
    char const* name() const override { return "scarlet_monastery_classic"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        if (!IsVanillaRealm())
            return DungeonAdvice{};
        DungeonAdvice a;
        // Creature ids and spell ids from the VMaNGOS data Forever's Classic world was built from (creature_template, creature spawns,
        // spell_template); the casts come from the converted EventAI / spell lists in 2026_09_28_15_world_vanilla_smartai.sql.
        a.high_priority_kill_entries = {
            3977,   // High Inquisitor Whitemane
            4296,   // Scarlet Adept
            4303,   // Scarlet Abbot
            4299,   // Scarlet Chaplain
            4289,   // Scarlet Evoker
            4291,   // Scarlet Diviner
            4294,   // Scarlet Sorcerer
            4293,   // Scarlet Scryer
            4300,   // Scarlet Wizard
            4297,   // Scarlet Conjuror
            4306,   // Scarlet Torturer
            4288,   // Scarlet Beastmaster
            4292,   // Scarlet Protector
        };
        a.mandatory_interrupt_spells = {
            6063,   // Heal
            6064,   // Heal
            12039,   // Heal
            15493,   // Holy Light
            1026,   // Holy Light
            9734,   // Holy Smite
            9481,   // Holy Smite
            14515,   // Dominate Mind
            9053,   // Fireball
            8422,   // Flamestrike
            1090,   // Sleep
            9613,   // Shadow Bolt
            9672,   // Frostbolt
            8364,   // Blizzard
        };
        a.cc_priority_entries = {
            4289,   // Scarlet Evoker
            4291,   // Scarlet Diviner
            4294,   // Scarlet Sorcerer
            4293,   // Scarlet Scryer
            4300,   // Scarlet Wizard
            4297,   // Scarlet Conjuror
        };
        a.bosses = {
            3983,   // Interrogator Vishas
            4543,   // Bloodmage Thalnos
            3974,   // Houndmaster Loksey
            6487,   // Arcanist Doan
            3975,   // Herod
            4542,   // High Inquisitor Fairbanks
            3976,   // Scarlet Commander Mograine
            3977,   // High Inquisitor Whitemane
        };
        a.progression_waypoints = {
            { 1786.5f, 1124.4f, 7.6f },   // Interrogator Vishas
            { 1820.3f, 1416.7f, -7.9f },   // Bloodmage Thalnos
            { 119.9f, -260.9f, 18.6f },   // Houndmaster Loksey
            { 148.3f, -428.7f, 18.5f },   // Arcanist Doan
            { 1965.1f, -431.6f, 6.3f },   // Herod
            { 1158.9f, 1354.3f, 30.4f },   // High Inquisitor Fairbanks
            { 1153.9f, 1398.4f, 32.6f },   // Scarlet Commander Mograine
            { 1202.1f, 1399.1f, 29.1f },   // High Inquisitor Whitemane
        };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeScarletMonasteryClassicScript()
{
    return std::make_unique<ScarletMonasteryClassicScript>();
}

std::unique_ptr<DungeonScript> MakeScarletMonasteryScript()
{
    return std::make_unique<ScarletMonasteryScript>();
}

} // namespace Playerbot
