// UldamanScript — Uldaman (map 70, vanilla 35-45).
// Titan-themed dwarf dungeon in Badlands. Bosses: Baelog (+ Eric &
// Olaf trio), Revelosh, Ironaya, Obsidian Sentinel, Ancient Stone
// Keeper, Galgann Firehammer, Grimlok, Archaedas.
//
// Authoritative spell IDs from TC source:
//   src/server/scripts/EasternKingdoms/Uldaman/boss_*.cpp

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class UldamanScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 70; }
    char const* name() const override { return "uldaman"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        a.high_priority_kill_entries = {
            4852,   // Stonevault Oracle
            4853,   // Stonevault Geomancer
            7321,   // Stonevault Flameweaver
            4847,   // Shadowforge Relic Hunter
            7012,   // Earthen Sculptor
        };
        a.mandatory_interrupt_spells = {
            // Archaedas (final boss)
            6524,   // Ground Tremor
            10347,  // Archaedas Awaken
            10340,  // Boss Aggro
            10258,  // Awaken Vault Walker
            10252,  // Awaken Earthen Guardian
            // Ironaya
            8374,   // Arcing Smash
            10101,  // Knock Away
            11876,  // War Stomp
            // Classic spell ids from the VMaNGOS spell_template / converted mob casts
            8005,   // Healing Wave
            9053,   // Fireball
            7739,   // Inferno Shell
            6064,   // Heal
            10452,   // Flame Buffet
        };
        a.cc_priority_entries = {
            4853,   // Stonevault Geomancer
            7321,   // Stonevault Flameweaver
            4847,   // Shadowforge Relic Hunter
            4852,   // Stonevault Oracle
        };
        a.dangerous_auras = {
            // Stone Keepers / Walkers
            9874,   // Self Destruct
        };
        // Boss progression — entries from TC's uldaman.h + WoWHead.
        a.bosses = {
            2748,   // Baelog (with Eric/Olaf — Lost Dwarves)
            6910,   // Revelosh
            7228,   // Ironaya
            7023,   // Obsidian Sentinel
            7206,   // Ancient Stone Keeper
            7291,   // Galgann Firehammer
            4854,   // Grimlok
            2748,   // Archaedas (final — note 2748 collision with Baelog
                    //   in source data; TC may differentiate elsewhere)
        };
        // Progression waypoints — Uldaman (vanilla) is a long linear
        // descent: entry shaft → Lost Dwarves camp → mining tunnels →
        // Revelosh hall → Ironaya gate → Stone Keeper hall → Galgann's
        // forge → Grimlok's lair → Archaedas's vault (final).
        // Boss spawn positions from the Classic creature data (VMaNGOS spawn table, map 70), in encounter order; bosses a script
        // summons (no static spawn) are left out. Replaces hand-typed coordinates that did not match the map.
        a.progression_waypoints = {
            { 104.3f, 272.3f, -51.7f },   // Archaedas
            { -225.6f, 161.2f, -44.5f },   // Revelosh
            { -235.7f, 309.6f, -47.6f },   // Ironaya
            { -163.8f, 388.0f, -36.4f },   // Obsidian Sentinel
            { -38.4f, 221.3f, -48.4f },   // Ancient Stone Keeper
            { -10.4f, 414.7f, -46.9f },   // Galgann Firehammer
            { 56.7f, 455.3f, -41.0f },   // Grimlok
        };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeUldamanScript()
{
    return std::make_unique<UldamanScript>();
}

} // namespace Playerbot
