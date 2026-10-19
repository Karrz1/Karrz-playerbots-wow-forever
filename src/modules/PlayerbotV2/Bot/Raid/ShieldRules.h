// ShieldRules - what a bot does about an enemy that carries a shield, as pure rules (tests/game/ClassicRaidFightScript.cpp).
//
// Majordomo Executus puts one of two shields on all eight of his adds every 30 s for 10 s (classic_boss_majordomo_executus.cpp:541-548):
// Magic Reflection sends spells back at the caster, Damage Shield hurts whoever strikes the add in melee. Until now either shield took the
// add off everybody's target list and the cast API refused every harmful spell at it, abilities included, so the whole raid idled for a third
// of the fight and the tanks lost Taunt, Sunder Armor, Revenge, Heroic Strike and Shield Slam for the same 10 s (Majordomo:
// "API_TARGET_REFLECTS x4 (78 6572 7386 23922)" on three tanks while their adds walked onto the healers).

#pragma once

#include <cstdint>

namespace Playerbot::Raid::Shields {

constexpr uint32_t kMagicReflection = 20619;   // the adds reflect spells: the casters stop
constexpr uint32_t kDamageShield    = 21075;   // the adds hurt melee strikes: the melee DPS stop

// Whether a DPS member keeps off an enemy that carries <aura>. melee: a melee DPS; caster: a ranged spell caster. A hunter is neither (shots
// are physical and not a melee strike) and a tank is never asked: its swings are its threat and it has a healer.
inline bool ShieldForbids(uint32_t aura, bool melee, bool caster)
{
    return (aura == kMagicReflection && caster) || (aura == kDamageShield && melee);
}

// Whether the cast API refuses a harmful spell at a target with a spell-reflect aura: only a spell the aura can send back. The core's own
// rule (Spell::m_canReflect, Spell.cpp:592): a magic-class spell that is not an ability, or one flagged to allow reflection, and never a spell
// that pierces reflection or immunities, nor a passive. Sunder Armor, Heroic Strike, Revenge, Shield Slam and Taunt are abilities: they go
// through, so a tank keeps its threat on a Magic Reflection add.
inline bool ReflectRefuses(bool magicDamageClass, bool isAbility, bool allowReflection, bool noReflection, bool noImmunities, bool passive)
{
    return ((magicDamageClass && !isAbility) || allowReflection) && !noReflection && !noImmunities && !passive;
}

} // namespace Playerbot::Raid::Shields
