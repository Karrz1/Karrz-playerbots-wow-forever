#include "ApRegistry.h"
#include <unordered_map>

namespace Playerbot::Combat {

namespace {

// Encode (class, spec) into a single key. Class fits in 8 bits, spec in
// the remaining 24 — covers all current and projected ChrSpecialization ids.
constexpr uint32 SpecKey(uint8 cls, uint32 spec) { return (uint32(cls) << 24) | (spec & 0xFFFFFFu); }

// Static — populated at init, read-only afterward. No synchronization needed.
std::unordered_map<uint32, ApRotation> g_rotations;

} // anonymous

void RegisterRotation(uint8 cls, uint32 spec, ApRotation rotation)
{
    g_rotations.emplace(SpecKey(cls, spec), rotation);
}

void RegisterRotationOverride(uint8 cls, uint32 spec, ApRotation rotation)
{
    g_rotations.insert_or_assign(SpecKey(cls, spec), rotation);
}

// Default spec per class — low-level (<10) or freshly created characters
// often have spec=0 (PrimarySpecialization unset). Without a fallback those
// bots would fall through GetRotation() and skip their entire APL — no spell
// casts, just auto-attacks. Pick a leveling-friendly DPS spec per class.
constexpr uint32 DefaultSpecForClass(uint8 cls)
{
    switch (cls)
    {
        case 1:  return 71;   // Warrior   -> Arms
        case 2:  return 70;   // Paladin   -> Retribution
        case 3:  return 253;  // Hunter    -> Beast Mastery
        case 4:  return 259;  // Rogue     -> Assassination
        case 5:  return 258;  // Priest    -> Shadow
        case 6:  return 252;  // DK        -> Unholy
        case 7:  return 263;  // Shaman    -> Enhancement
        case 8:  return 64;   // Mage      -> Frost
        case 9:  return 265;  // Warlock   -> Affliction
        case 10: return 269;  // Monk      -> Windwalker
        case 11: return 102;  // Druid     -> Balance
        case 12: return 577;  // DH        -> Havoc
        case 13: return 1467; // Evoker    -> Devastation
        default: return 0;
    }
}

ApRotation const* GetRotation(uint8 cls, uint32 spec)
{
    auto it = g_rotations.find(SpecKey(cls, spec));
    if (it != g_rotations.end()) return &it->second;
    // Fall back to the class's default spec for unspecced bots.
    if (uint32 def = DefaultSpecForClass(cls); def != 0 && def != spec)
    {
        auto df = g_rotations.find(SpecKey(cls, def));
        if (df != g_rotations.end()) return &df->second;
    }
    return nullptr;
}

std::vector<RotationListEntry> ListRotations()
{
    std::vector<RotationListEntry> out;
    out.reserve(g_rotations.size());
    for (auto const& [key, rot] : g_rotations)
    {
        const uint8  cls  = static_cast<uint8>(key >> 24);
        const uint32 spec = key & 0xFFFFFFu;
        out.push_back({cls, spec, rot.rule_count()});
    }
    return out;
}

// Forward declarations of per-spec registration functions. Each Apl_*.cpp
// defines `void RegisterApl_<Class>_<Spec>()` and we call them from
// RegisterAllRotations.
extern void RegisterApl_DeathKnight_Frost();
extern void RegisterApl_DeathKnight_Unholy();
extern void RegisterApl_DemonHunter_Havoc();
extern void RegisterApl_DemonHunter_Vengeance();
extern void RegisterApl_DemonHunter_Devourer();
extern void RegisterApl_Evoker_Devastation();
extern void RegisterApl_Evoker_Preservation();
extern void RegisterApl_Evoker_Augmentation();
extern void RegisterApl_Monk_Windwalker();
extern void RegisterApl_Monk_Brewmaster();
extern void RegisterApl_Monk_Mistweaver();
extern void RegisterApl_DeathKnight_Blood();
extern void RegisterApl_Baseline();   // per-class spec=0 baseline rotations
extern void RegisterApl_Classic_Warrior();
extern void RegisterApl_Classic_Priest();
extern void RegisterApl_Classic_Paladin();
extern void RegisterApl_Classic_Hunter();
extern void RegisterApl_Classic_Rogue();
extern void RegisterApl_Classic_Shaman();
extern void RegisterApl_Classic_Mage();
extern void RegisterApl_Classic_Warlock();
extern void RegisterApl_Classic_Druid();

void RegisterAllRotations()
{
    // Baselines first so they're guaranteed to be in the map even if
    // a spec-specific registration ever throws / is conditionally
    // skipped. GetRotation prefers exact spec match anyway, so order
    // is functional rather than semantic.
    RegisterApl_Baseline();
    RegisterApl_DeathKnight_Frost();
    RegisterApl_DeathKnight_Unholy();
    RegisterApl_DemonHunter_Havoc();
    RegisterApl_DemonHunter_Vengeance();
    RegisterApl_DemonHunter_Devourer();
    RegisterApl_Evoker_Devastation();
    RegisterApl_Evoker_Preservation();
    RegisterApl_Evoker_Augmentation();
    RegisterApl_Monk_Windwalker();
    RegisterApl_Monk_Brewmaster();
    RegisterApl_Monk_Mistweaver();
    RegisterApl_DeathKnight_Blood();
    // (More classes/specs land here as their files appear under Combat/Apl/.)

    // Classic 1.60 (Forever): vanilla rotations replace the retail ones for
    // the classes that have them. Last, so they override.
    RegisterApl_Classic_Warrior();
    RegisterApl_Classic_Priest();
    RegisterApl_Classic_Paladin();
    RegisterApl_Classic_Hunter();
    RegisterApl_Classic_Rogue();
    RegisterApl_Classic_Shaman();
    RegisterApl_Classic_Mage();
    RegisterApl_Classic_Warlock();
    RegisterApl_Classic_Druid();
}

} // namespace Playerbot::Combat
