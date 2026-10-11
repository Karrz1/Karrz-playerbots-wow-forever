// ApSchoolHint - the Classic APLs' side of the raid damage-school rule
// (PveOrder::school_hints: Death Talon vulnerability, Plague Slime immunity).
//
// Two halves, split by where each can be decided correctly:
//   * AVOID is enforced centrally in BotIntentEmitter: a harmful spell of an
//     avoided school aimed at a hinted enemy is never pushed (the rotation
//     falls through to its next rule), and StartAttack carries the avoided
//     schools so the API holds the swing / Auto Shot / wand on that target.
//     No rotation code is needed for it.
//   * PREFER needs to know which of a class's damage spells are its fillers,
//     so each Classic rotation lists them once (SchoolChoice) with the rule
//     predicate it already uses for that spell (cooldown, mana, range, dot
//     already up, healer may-dps gate) and puts one PreferredSchoolRule at
//     the head of its damage block. With a prefer mask on the victim that
//     rule casts the first listed spell of a preferred, non-avoided school
//     whose predicate passes; otherwise it is silent and the normal damage
//     rules run (minus the avoided schools, via the emitter gate). Heals,
//     defensives, interrupts and buffs above the damage block are untouched.

#pragma once

#include "../ApRotation.h"
#include "Bot/BotIntentEmitter.h"
#include "Bot/BotSnapshot.h"
#include "Bot/BotSnapshotView.h"
#include <span>

namespace Playerbot::Combat::SchoolHint {

// One damage spell of a rotation and the predicate that already gates it.
struct SchoolChoice
{
    uint32      spell;
    ApPredicate usable;
};

// The raid layer's hint on the bot's victim, or nullptr.
inline PveOrder::SchoolHint const* VictimHint(ApPredicateContext const& ctx)
{
    return ctx.bot.raw().pve_order.school_hint_for(ctx.bot.victim());
}

// Preferred schools on the victim that are not also avoided (a school in
// both masks counts as avoided) and that the bot can cast at all (Shadowform
// locks out holy: a holy preference then is no preference, and the normal
// shadow rotation runs). 0 = no preference.
inline uint8 VictimPreferMask(ApPredicateContext const& ctx)
{
    PveOrder::SchoolHint const* h = VictimHint(ctx);
    if (!h) return 0;
    return uint8(h->prefer_mask & ~h->avoid_mask & ~BotIntentEmitter::LockedSchools(ctx.bot.raw()));
}

// First listed spell of a preferred school whose own predicate passes, or 0.
inline uint32 PickPreferred(ApPredicateContext const& ctx, std::span<SchoolChoice const> choices)
{
    uint8 const prefer = VictimPreferMask(ctx);
    if (!prefer) return 0;
    uint8 const wand = ctx.bot.raw().consumables.wand_school_mask;
    for (SchoolChoice const& c : choices)
        if ((BotIntentEmitter::SpellSchoolMaskOf(c.spell, wand) & prefer) != 0 && c.usable(ctx))
            return c.spell;
    return 0;
}

// Rule body shared by every Classic rotation's preferred-school rule:
//   bool ShouldSchool(ctx)       { return SchoolHint::ShouldCast(ctx, kSchoolChoices); }
//   void DoSchool(ctx, e)        { SchoolHint::DoCast(ctx, e, kSchoolChoices); }
inline bool ShouldCast(ApPredicateContext const& ctx, std::span<SchoolChoice const> choices)
{
    return PickPreferred(ctx, choices) != 0;
}

// Casts the first passing preferred spell the emitter accepts (an emit-dedup
// drop moves on to the next preferred one; none pushed = the rotation falls
// through to its normal damage rules).
inline void DoCast(ApPredicateContext const& ctx, BotIntentEmitter& e, std::span<SchoolChoice const> choices)
{
    uint8 const prefer = VictimPreferMask(ctx);
    if (!prefer) return;
    uint8 const wand = ctx.bot.raw().consumables.wand_school_mask;
    for (SchoolChoice const& c : choices)
        if ((BotIntentEmitter::SpellSchoolMaskOf(c.spell, wand) & prefer) != 0 && c.usable(ctx)
            && e.cast(c.spell, ctx.bot.victim()))
            return;
}

} // namespace Playerbot::Combat::SchoolHint
