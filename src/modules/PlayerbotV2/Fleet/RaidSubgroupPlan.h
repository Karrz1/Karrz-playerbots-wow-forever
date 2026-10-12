// RaidSubgroupPlan - where each companion of a raid goes back to, as pure logic (tests/game/ClassicRaidSubgroupPlan.cpp).
//
// A companion joins the owner's raid in the first free sub-group (Group::AddMember), whatever order the bots log in. The owner built
// the raid on purpose (two tanks and two healers in group 1, the DPS behind them), so each owned bot has its sub-group stored
// (OwnerRegistry::StoredSubgroup, migration 0026) and, once members are in, this plan lists the moves that put them back: a move into a
// sub-group with room, or a swap with a bot standing there that does not belong there. Humans are never moved. Bots whose place is not
// reachable yet (a bot of that group has not logged in) stay where they are; the next join runs the plan again.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Playerbot::V2::Fleet {

constexpr std::uint8_t kRaidSubgroupSize = 5;     // MAX_GROUP_SIZE
constexpr std::uint8_t kRaidSubgroups    = 8;     // MAX_RAID_SUBGROUPS
constexpr std::uint8_t kNoSubgroup       = 0xFF;

// True while the restore itself moves members: the sub-group hook must not store those moves (a swap puts the bot that stood in the way
// somewhere that is not its own place).
inline thread_local bool g_subgroup_restoring = false;
struct SubgroupRestoreScope
{
    SubgroupRestoreScope() { g_subgroup_restoring = true; }
    ~SubgroupRestoreScope() { g_subgroup_restoring = false; }
};

struct SubgroupMember
{
    std::uint64_t guid = 0;
    std::uint8_t  group = 0;              // 0-based sub-group the member stands in now
    std::uint8_t  want = kNoSubgroup;     // stored sub-group; kNoSubgroup = none
    bool          movable = false;        // an owned companion (a human is never moved)
};

struct SubgroupMove
{
    bool          swap = false;
    std::uint64_t guid = 0;               // the member moved
    std::uint64_t other = 0;              // swap: the member that takes its place
    std::uint8_t  group = 0;              // move: the target sub-group
};

// The moves, in order, that bring every movable member with a stored sub-group to it as far as room allows. The input is not changed.
inline std::vector<SubgroupMove> PlanSubgroupMoves(std::vector<SubgroupMember> members)
{
    std::vector<SubgroupMove> out;
    auto count = [&](std::uint8_t g)
    {
        std::uint32_t n = 0;
        for (SubgroupMember const& m : members)
            n += m.group == g ? 1u : 0u;
        return n;
    };
    // Every pass fixes one member or ends: a move needs a sub-group with room, a swap an occupant that is not at home there.
    for (std::uint32_t pass = 0; pass < 200; ++pass)
    {
        bool changed = false;
        for (SubgroupMember& m : members)
        {
            if (!m.movable || m.want == kNoSubgroup || m.want >= kRaidSubgroups || m.group == m.want)
                continue;
            if (count(m.want) < kRaidSubgroupSize)
            {
                out.push_back({ false, m.guid, 0, m.want });
                m.group = m.want;
                changed = true;
                break;
            }
            // Full: trade places with a bot that does not belong there, the one that wants m's sub-group first.
            SubgroupMember* pick = nullptr;
            for (SubgroupMember& o : members)
            {
                if (&o == &m || !o.movable || o.group != m.want || o.want == m.want)
                    continue;
                if (!pick || (o.want == m.group && pick->want != m.group))
                    pick = &o;
            }
            if (!pick)
                continue;
            out.push_back({ true, m.guid, pick->guid, 0 });
            std::uint8_t const g = m.group;
            m.group = pick->group;
            pick->group = g;
            changed = true;
            break;
        }
        if (!changed)
            break;
    }
    return out;
}

struct OrderMember
{
    std::uint64_t guid = 0;
    bool          movable = false;        // an owned companion
    std::uint16_t order = 0xFFFF;         // stored list index; 0xFFFF = none
};

// The member list the group should have: the places held by owned members that have a stored order are handed out again, in stored
// order, to those members. Humans and members without a stored order keep their place. Returns the guids in list order; empty when
// the list is already as wanted.
inline std::vector<std::uint64_t> PlanMemberOrder(std::vector<OrderMember> const& members)
{
    std::vector<std::size_t> slots;      // list indices held by orderable members
    std::vector<std::size_t> by_order;   // the same members, sorted by stored order
    for (std::size_t i = 0; i < members.size(); ++i)
        if (members[i].movable && members[i].order != 0xFFFF)
        {
            slots.push_back(i);
            by_order.push_back(i);
        }
    std::stable_sort(by_order.begin(), by_order.end(),
        [&](std::size_t a, std::size_t b) { return members[a].order < members[b].order; });
    std::vector<std::uint64_t> out;
    out.reserve(members.size());
    for (OrderMember const& m : members)
        out.push_back(m.guid);
    bool changed = false;
    for (std::size_t k = 0; k < slots.size(); ++k)
    {
        if (out[slots[k]] != members[by_order[k]].guid)
            changed = true;
        out[slots[k]] = members[by_order[k]].guid;
    }
    if (!changed)
        out.clear();
    return out;
}

} // namespace Playerbot::V2::Fleet
