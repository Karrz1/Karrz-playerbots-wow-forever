// One-button warlock ("lazy lock"): the decision, on plain numbers.
//
// A press of the key gives the single next spell to cast on the player's target, from what the warlock knows, what is ready, what
// is already on the target and the resources. LazyLock.cpp gathers those facts from the live Player and casts the answer; this
// header holds no game objects, so the order of the rules is unit-tested (tests/game/ClassicLazyLock.cpp).
#pragma once

#include <cstdint>

namespace Playerbot::Lazy {

enum Cast : uint8_t
{
    None = 0,
    DemonArmor,        // Demon Skin below its level
    SummonPet,         // Voidwalker once known, else Imp
    DeathCoil,
    DrainLife,
    LifeTap,
    ShadowBoltTrance,  // the Nightfall instant bolt
    Racial,            // Blood Fury / Berserking
    AmplifyCurse,
    CurseOfTongues,
    CurseOfAgony,
    Corruption,
    Immolate,
    SiphonLife,
    Conflagrate,
    Shadowburn,
    DrainSoul,
    ShadowBolt,
    Wand,
    Count
};

constexpr uint32_t Bit(Cast s) { return 1u << s; }

struct Input
{
    uint32_t known = 0;          // Bit(spell): in the spellbook (any rank)
    uint32_t ready = 0;          // Bit(spell): off cooldown and global cooldown
    uint32_t on_target = 0;      // Bit(spell): this warlock's own aura of it is on the target (curses: CurseOfTongues, CurseOfAgony)

    bool casting = false;        // a cast or channel is going
    bool in_combat = false;
    bool has_target = false;     // a living enemy is selected
    bool target_caster = false;  // the target has a mana pool (Curse of Tongues slows its casts)
    bool target_boss = false;
    bool target_in_range = true; // within cast range (30y)
    bool has_pet = false;
    bool armor_up = false;
    bool trance = false;         // Shadow Trance proc on the warlock
    bool wand_equipped = false;
    bool wand_shooting = false;  // the wand is already on auto-repeat

    bool another_tongues = false; // another warlock's Curse of Tongues is already on the target

    int hp_pct = 100;
    int mana_pct = 100;
    int target_hp_pct = 100;
    int soul_shards = 0;
};

inline bool Can(Input const& in, Cast s) { return (in.known & Bit(s)) && (in.ready & Bit(s)); }
inline bool Up(Input const& in, Cast s) { return (in.on_target & Bit(s)) != 0; }

// Do not sink dots into a mob that is about to die (bosses always get them).
inline bool DotWorthwhile(Input const& in)
{
    return in.mana_pct >= 10 && (in.target_boss || in.target_hp_pct >= 35);
}

inline Cast Pick(Input const& in)
{
    if (in.casting)
        return None;

    // Upkeep that needs no target.
    if (in.hp_pct <= 30 && in.in_combat && in.has_target && in.target_in_range && Can(in, DeathCoil)) return DeathCoil;
    if (in.mana_pct <= 20 && in.hp_pct >= 50 && Can(in, LifeTap)) return LifeTap;
    if (!in.armor_up && Can(in, DemonArmor)) return DemonArmor;
    if (!in.has_pet && !in.in_combat && Can(in, SummonPet)) return SummonPet;

    if (!in.has_target || !in.target_in_range)
        return None;

    if (in.hp_pct <= 35 && in.in_combat && in.mana_pct >= 10 && Can(in, DrainLife)) return DrainLife;

    // The free instant bolt first.
    if (in.trance && in.mana_pct >= 8 && Can(in, ShadowBoltTrance)) return ShadowBoltTrance;

    // Cooldowns: a racial on a fight that will last (a boss, or a target still above 60%).
    if (in.in_combat && in.mana_pct >= 30 && (in.target_boss || in.target_hp_pct >= 60) && Can(in, Racial)) return Racial;

    if (DotWorthwhile(in))
    {
        // One curse per target: Tongues on a caster (slows its casts), Agony on the rest. A curse of ours on the target already is
        // enough - casting another would replace it.
        bool const tongues = in.target_caster && !in.another_tongues && (in.known & Bit(CurseOfTongues));
        Cast const curse = tongues ? CurseOfTongues : CurseOfAgony;
        bool const has_curse = Up(in, CurseOfTongues) || Up(in, CurseOfAgony);
        if (!has_curse)
        {
            if (curse == CurseOfAgony && Can(in, AmplifyCurse)) return AmplifyCurse;
            if (Can(in, curse)) return curse;
        }
        if (!Up(in, Corruption) && Can(in, Corruption)) return Corruption;
        if (!Up(in, Immolate) && Can(in, Immolate)) return Immolate;
        if (!Up(in, SiphonLife) && Can(in, SiphonLife)) return SiphonLife;
    }

    if (in.mana_pct >= 10 && Up(in, Immolate) && Can(in, Conflagrate)) return Conflagrate;
    // Shadowburn costs a shard: finisher only, and never the last two.
    if (in.mana_pct >= 10 && in.target_hp_pct <= 20 && in.soul_shards >= 3 && Can(in, Shadowburn)) return Shadowburn;
    if (!in.target_boss && in.target_hp_pct <= 25 && in.soul_shards < 6 && in.mana_pct >= 10 && Can(in, DrainSoul)) return DrainSoul;

    // Filler: Shadow Bolt; a warlock with a wand shoots below 35% on ordinary fights and casts down to 8% on a boss.
    int const floor = in.wand_equipped && !in.target_boss ? 35 : 8;
    if (in.mana_pct >= floor && Can(in, ShadowBolt)) return ShadowBolt;
    if (in.wand_equipped && !in.wand_shooting && Can(in, Wand)) return Wand;
    return None;
}

} // namespace Playerbot::Lazy
