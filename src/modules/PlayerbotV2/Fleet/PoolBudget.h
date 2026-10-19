// PoolBudget - the two budgets of ambient characters and the rule for retiring the temporary ones.
//
//   core      the standing roster (PoolMax): level 10+ residents that sleep in the characters table and stay for good;
//   variable  temporary residents (VariableMax): created around a real player where the core has nobody fitting - the island's
//             level 1-9 Skyborne for a new character, a low-level town for a new player - and deleted again once they have been
//             offline for VariableTtl with nobody needing them.
//
// the core was full (5506/3000, mostly level 60), so every creation was refused and a new character on
// Zephras Isle met 12 bots. A full core must not keep the world empty around a new player, and the extra bots must not pile up.
// Pure: no game types, tested in tests/game/ClassicPoolBudget.cpp.

#pragma once

#include <cstdint>

namespace Playerbot::V2::Fleet {

struct PoolBudget
{
    std::int64_t core = 0;           // standing roster rows
    std::int64_t core_max = 0;
    std::int64_t variable = 0;       // temporary rows (and the island residents created before the variable pool existed)
    std::int64_t variable_max = 0;
};

enum class PoolSlot : std::uint8_t { None, Core, Variable };

// Where one more character may be created. The island's residents are always temporary (they only matter while someone is on the
// island); anyone else goes to the core while it has room and to the variable pool when it does not.
constexpr PoolSlot ChoosePoolSlot(PoolBudget const& b, bool isle_resident)
{
    if (!isle_resident && b.core < b.core_max)
        return PoolSlot::Core;
    return b.variable < b.variable_max ? PoolSlot::Variable : PoolSlot::None;
}

// With a level census the standing roster takes a creation when its (faction, level bracket) is below its share of the bracket cap, whatever
// the roster total is (a roster that grew past core_max earlier would otherwise send every missing low-level bot to the temporary pool).
constexpr PoolBudget BalancedBudget(PoolBudget b, bool bracket_has_room)
{
    b.core_max = bracket_has_room ? b.core + 1 : 0;
    return b;
}

constexpr void PoolCountCreated(PoolBudget& b, PoolSlot slot)
{
    if (slot == PoolSlot::Core)
        ++b.core;
    else if (slot == PoolSlot::Variable)
        ++b.variable;
}

// May a temporary character be deleted now? Only when it is flagged temporary, nobody is using it (offline, no owner, no group, no
// queue tag) and it has been offline for the whole TTL. A core, owned, saved, grouped or online character is never retired.
struct RetireFacts
{
    bool          variable = false;
    bool          online = false;
    bool          owned = false;        // a player's companion
    bool          grouped = false;
    bool          queue_tagged = false; // a battleground/queue JIT bot (its own hygiene handles those)
    std::int64_t  offline_seconds = 0;
};

constexpr bool CanRetire(RetireFacts const& f, std::int64_t ttl_seconds)
{
    return f.variable && !f.online && !f.owned && !f.grouped && !f.queue_tagged && f.offline_seconds >= ttl_seconds;
}

} // namespace Playerbot::V2::Fleet
