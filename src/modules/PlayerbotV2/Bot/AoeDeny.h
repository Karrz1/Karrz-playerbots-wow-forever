// AoeDeny.h - the owner's "aoe off": the bot casts nothing that hits more than one enemy until "aoe on" (BotCommandParser "aoe",
// "aoe_all", the add-on's AoE switch and menu entry). Kept on BotAI (memory only), mirrored into BotSnapshot::aoe_denied;
// BotIntentEmitter drops such a cast, so a rotation falls through to its single-target rule.
// Pure: the emitter passes in what SpellInfo says of the spell (no SpellMgr here).
#pragma once

#include <cstdint>

namespace Playerbot::AoeDeny {

// Totems that do their damage round themselves: the summon spell is not harmful, the totem's own pulse is (rank 1 ids).
inline bool AreaTotem(uint32_t first_rank)
{
    return first_rank == 8190     // Magma Totem
        || first_rank == 1535;    // Fire Nova Totem
}

// Does "aoe off" leave out this spell? A harmful spell that picks its targets from an area or a cone, puts down a damaging
// area (Blizzard, Consecration, Rain of Fire), or chains to further enemies (Chain Lightning, Multi-Shot, Cleave), and the
// area totems. `chain_targets` is the largest ChainTargets of its effects.
inline bool IsAoe(bool positive, bool affecting_area, uint32_t chain_targets, uint32_t first_rank)
{
    if (AreaTotem(first_rank))
        return true;
    if (positive)
        return false;
    return affecting_area || chain_targets > 1;
}

} // namespace Playerbot::AoeDeny
