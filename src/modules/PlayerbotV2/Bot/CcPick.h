// CcPick - which enemy a CC-capable dungeon bot (mage Polymorph, rogue Sap, ...) crowd-controls from the script's CC priority
// list: the pure choice behind State_Idle idle:dungeon_auto_cc_script.
//
// An eligible candidate is a live enemy of the script's priority list that no teammate has locked yet, that is not hitting the
// bot itself, is not the skull (the group's focus-kill mark) and whose creature type the CC spell accepts. Among them the
// group's kill focus (PveOrder::kill_focus, the first priority add of the script's list that is engaged) goes first: it is the
// most dangerous caster, and once it is locked the coordinator moves the focus on to the next add (PveGroupCoordinator skips
// adds a member has CC-locked), so the DPS burn a different one. Deadmines 2026-10-09: the Defias Evoker's Flamestrike (546 damage)
// killed two group members while the mage's one Polymorph went to a lesser caster.
// Pure: no game types, tested in tests/game/ClassicCcPick.cpp.

#pragma once

#include <cstddef>
#include <vector>

namespace Playerbot {

struct CcCandidate
{
    bool in_priority_list = false;   // the entry is in the script's cc_priority_entries
    bool locked = false;             // a teammate's CC already holds it
    bool hits_me = false;            // it is attacking the bot itself
    bool is_skull = false;           // the group's focus-kill mark
    bool wrong_type = false;         // the CC spell cannot take its creature type (mechanical, ...)
    bool is_kill_focus = false;      // the coordinator's current kill focus
};

constexpr size_t kNoCcTarget = size_t(-1);

inline size_t PickCcTarget(std::vector<CcCandidate> const& candidates)
{
    size_t first = kNoCcTarget;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        CcCandidate const& c = candidates[i];
        if (!c.in_priority_list || c.locked || c.hits_me || c.is_skull || c.wrong_type)
            continue;
        if (c.is_kill_focus)
            return i;
        if (first == kNoCcTarget)
            first = i;
    }
    return first;
}

}  // namespace Playerbot
