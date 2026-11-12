// One-button warlock ("lazy lock"): the decision, on plain numbers.
//
// A press of the key gives the single next spell to cast on the player's target, from what the warlock knows, what is ready, what
// is already on the target and the resources. LazyLock.cpp gathers those facts from the live Player and casts the answer; this
// header holds no game objects, so the order of the rules is unit-tested (tests/game/ClassicLazyLock.cpp).
//
// Priority (vanilla affliction / destruction), first rule that applies wins:
//   1. emergency  Death Coil <=30% hp; an enemy hitting the warlock in melee range: Death Coil, then Howl of Terror on two or more,
//                 then Fear (outside dungeons and raids, or on a player; never a boss); Life Tap <=25% mana while healthy;
//                 Drain Life <=35% hp
//   2. upkeep     Demon Armor and the pet only with no enemy selected (an enemy selected always gets a spell, never a 10 s summon);
//                 the pet only out of combat (a missing pet, shard or summon never holds the rest back)
//   3. proc       Shadow Trance instant Shadow Bolt
//   4. cooldowns  both on-use trinkets and the racial whenever ready on a fight that will last, then Amplify Curse before Agony.
//                 Trinkets, racial and Amplify Curse cost no global cooldown: the caller casts them and picks again in the same press
//   5. dots       ONE curse (Tongues on mana users, else Agony), Corruption, Immolate, Siphon Life; not into a dying mob
//   6. execute    Conflagrate on Immolate; Shadowburn in the last 20% (keeps one shard); Drain Soul on a dying mob when shards are low
//   7. filler     Shadow Bolt; the wand only when Shadow Bolt cannot be cast (out of mana)
// `blocked` lets the caller take a spell out of the choice for a while (it failed for want of a shard, mana, ...): the rest
// of the list moves up instead of the key repeating the same refused cast.
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
    Fear,
    HowlOfTerror,
    Trinket1,          // on-use trinket in the top trinket slot
    Trinket2,
    Count
};

constexpr uint32_t Bit(Cast s) { return 1u << s; }

struct Input
{
    uint32_t known = 0;          // Bit(spell): in the spellbook (any rank)
    uint32_t ready = 0;          // Bit(spell): off cooldown and global cooldown
    uint32_t on_target = 0;      // Bit(spell): this warlock's own aura of it is on the target (curses: CurseOfTongues, CurseOfAgony)
    uint32_t blocked = 0;        // Bit(spell): refused lately (no shard, no mana, ...): leave it out

    bool casting = false;        // a cast or channel is going
    bool gcd = false;            // the global cooldown is running (the next press picks again)
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
    bool target_player = false;  // the target is a player (PvP)
    bool target_close = false;   // the target is in melee range (8y) and attacking the warlock
    bool target_feared = false;  // the target is already feared or horrified
    bool fear_safe = true;       // a feared mob running off is fine here (outside dungeons and raids)
    int attackers_close = 0;     // enemies attacking the warlock within Howl of Terror's 10y

    int hp_pct = 100;
    int mana_pct = 100;
    int target_hp_pct = 100;
    int soul_shards = 0;
};

inline bool Can(Input const& in, Cast s) { return (in.known & Bit(s)) && (in.ready & Bit(s)) && !(in.blocked & Bit(s)); }
inline bool Up(Input const& in, Cast s) { return (in.on_target & Bit(s)) != 0; }

// Do not sink dots into a mob that is about to die (bosses always get them).
inline bool DotWorthwhile(Input const& in)
{
    return in.mana_pct >= 10 && (in.target_boss || in.target_hp_pct >= 35);
}

// Costs no global cooldown: cast it and pick again in the same press.
constexpr bool OffGlobalCooldown(Cast s) { return s == Racial || s == AmplifyCurse || s == Trinket1 || s == Trinket2; }

// A fight worth a cooldown: a boss, a player, or a target still above half health.
inline bool LongFight(Input const& in) { return in.target_boss || in.target_player || in.target_hp_pct >= 50; }

// The next spell, and in `why` (when given) the rule that chose it, for the press log.
inline Cast Pick(Input const& in, char const** why = nullptr)
{
    auto pick = [&](Cast c, char const* reason) { if (why) *why = reason; return c; };
    if (why) *why = "";

    if (in.casting)
        return pick(None, "busy casting");
    if (in.gcd)
        return pick(None, "global cooldown");

    // Emergencies and upkeep.
    if (in.hp_pct <= 30 && in.in_combat && in.has_target && in.target_in_range && Can(in, DeathCoil)) return pick(DeathCoil, "health <=30%");

    // An enemy on the warlock in melee range: horrify it, then fear it, so the next presses cast from range.
    if (in.in_combat && in.has_target && in.target_close && !in.target_feared)
    {
        bool const fear_ok = !in.target_boss && (in.fear_safe || in.target_player);
        if (Can(in, DeathCoil)) return pick(DeathCoil, "enemy in melee: Death Coil");
        if (fear_ok && in.attackers_close >= 2 && Can(in, HowlOfTerror)) return pick(HowlOfTerror, "two or more in melee: Howl of Terror");
        if (fear_ok && Can(in, Fear)) return pick(Fear, "enemy in melee: Fear");
    }

    if (in.mana_pct <= 25 && in.hp_pct >= 50 && Can(in, LifeTap)) return pick(LifeTap, "mana <=25%, health ok");

    // Upkeep only with no enemy selected: a press at an enemy always fights.
    if (!in.has_target)
    {
        if (!in.armor_up && Can(in, DemonArmor)) return pick(DemonArmor, "armor down");
        if (!in.has_pet && !in.in_combat && Can(in, SummonPet)) return pick(SummonPet, "no pet");
        return pick(None, "no target");
    }
    if (!in.target_in_range) return pick(None, "target out of range");

    if (in.hp_pct <= 35 && in.in_combat && in.mana_pct >= 10 && Can(in, DrainLife)) return pick(DrainLife, "health <=35%");

    // The free instant bolt first.
    if (in.trance && in.mana_pct >= 8 && Can(in, ShadowBoltTrance)) return pick(ShadowBoltTrance, "Shadow Trance proc");

    // Cooldowns whenever ready on a fight that will last: both trinkets, then the racial.
    if (in.in_combat && LongFight(in))
    {
        if (Can(in, Trinket1)) return pick(Trinket1, "long fight: trinket 1");
        if (Can(in, Trinket2)) return pick(Trinket2, "long fight: trinket 2");
        if (in.mana_pct >= 30 && Can(in, Racial)) return pick(Racial, "long fight: racial");
    }

    if (DotWorthwhile(in))
    {
        // One curse per target: Tongues on a caster (slows its casts), Agony on the rest. A curse of ours on the target already is
        // enough - casting another would replace it.
        bool const tongues = in.target_caster && !in.another_tongues && Can(in, CurseOfTongues);
        Cast const curse = tongues ? CurseOfTongues : CurseOfAgony;
        bool const has_curse = Up(in, CurseOfTongues) || Up(in, CurseOfAgony);
        if (!has_curse)
        {
            if (curse == CurseOfAgony && Can(in, AmplifyCurse)) return pick(AmplifyCurse, "before Curse of Agony");
            if (Can(in, curse)) return pick(curse, tongues ? "caster: curse of Tongues" : "curse missing");
        }
        if (!Up(in, Corruption) && Can(in, Corruption)) return pick(Corruption, "Corruption missing");
        if (!Up(in, Immolate) && Can(in, Immolate)) return pick(Immolate, "Immolate missing");
        if (!Up(in, SiphonLife) && Can(in, SiphonLife)) return pick(SiphonLife, "Siphon Life missing");
    }

    if (in.mana_pct >= 10 && Up(in, Immolate) && Can(in, Conflagrate)) return pick(Conflagrate, "Immolate up: Conflagrate");
    // Shadowburn costs a shard: finisher only, in the last 20%, and never the last one.
    if (in.mana_pct >= 10 && in.target_hp_pct <= 20 && in.soul_shards >= 2 && Can(in, Shadowburn)) return pick(Shadowburn, "execute");
    if (!in.target_boss && in.target_hp_pct <= 25 && in.soul_shards < 6 && in.mana_pct >= 10 && Can(in, DrainSoul)) return pick(DrainSoul, "dying mob, shards low");

    // Filler: Shadow Bolt; the wand only when Shadow Bolt cannot be cast.
    if (in.mana_pct >= 5 && Can(in, ShadowBolt)) return pick(ShadowBolt, "filler");
    if (in.wand_equipped && !in.wand_shooting && Can(in, Wand)) return pick(Wand, "out of mana: wand");
    return pick(None, in.wand_shooting ? "wand already shooting" : "nothing castable");
}

} // namespace Playerbot::Lazy
