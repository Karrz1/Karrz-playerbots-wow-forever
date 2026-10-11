#pragma once
// Pre-pull crowd control for a bot-led dungeon squad: before the tank pulls a pack of 3+, it asks for one mob of the pack
// to be held by the group's mage. The tank (a bot rule, never touches the group) opens a request here; the
// PveGroupCoordinator (world thread) answers it: the group's moon icon goes on the mob, and the mage is released for that
// mark (MarksNeedOrder: crowd control on an idle mob is otherwise the owner's call, and a bot-led squad has no owner order).
// The mage's autonomous marked CC (idle:dungeon_auto_cc_marked) does the Polymorph. The tank waits for the mob to read
// is_cc_locked (or a refusal / the wait cap), then runs its pull back (BotAI::PullBackPlan).
#include "ObjectGuid.h"

#include <mutex>
#include <unordered_map>

namespace Playerbot::PrePullCc {

enum class State : uint8 { Pending, Marked, Refused };

struct Request
{
    ObjectGuid mob;
    uint64     caster_low = 0;      // the mage (guid counter)
    uint32     requested_ms = 0;
    uint32     done_ms = 0;         // the tank stopped waiting (0 = still waiting)
    State      state = State::Pending;
};

inline constexpr uint32 kWaitCapMs = 8000;    // the tank waits at most this long for the mob to be held
inline constexpr uint32 kLifeMs    = 30000;   // a request is dropped this long after it was opened
inline constexpr uint32 kAfterDoneMs = 12000; // the mark and the mage's release outlast the tank's wait by this

namespace detail {
inline std::mutex& Mutex() { static std::mutex m; return m; }
inline std::unordered_map<uint64, Request>& Map() { static std::unordered_map<uint64, Request> m; return m; }
}

// A live request of the group (expired ones are dropped), false when none.
inline bool Get(uint64 group_low, uint32 now_ms, Request& out)
{
    std::lock_guard<std::mutex> lk(detail::Mutex());
    auto& m = detail::Map();
    auto it = m.find(group_low);
    if (it == m.end()) return false;
    if (uint32(now_ms - it->second.requested_ms) > kLifeMs) { m.erase(it); return false; }
    out = it->second;
    return true;
}

// Open a request unless the group has a live one. True when opened.
inline bool Open(uint64 group_low, ObjectGuid mob, uint64 caster_low, uint32 now_ms)
{
    Request cur;
    if (Get(group_low, now_ms, cur)) return false;
    std::lock_guard<std::mutex> lk(detail::Mutex());
    Request r;
    r.mob = mob; r.caster_low = caster_low; r.requested_ms = now_ms ? now_ms : 1u;
    detail::Map()[group_low] = r;
    return true;
}

inline void SetState(uint64 group_low, State st)
{
    std::lock_guard<std::mutex> lk(detail::Mutex());
    if (auto it = detail::Map().find(group_low); it != detail::Map().end()) it->second.state = st;
}

// The tank is done waiting.
inline void Done(uint64 group_low, uint32 now_ms)
{
    std::lock_guard<std::mutex> lk(detail::Mutex());
    if (auto it = detail::Map().find(group_low); it != detail::Map().end() && !it->second.done_ms)
        it->second.done_ms = now_ms ? now_ms : 1u;
}

inline void Erase(uint64 group_low)
{
    std::lock_guard<std::mutex> lk(detail::Mutex());
    detail::Map().erase(group_low);
}

} // namespace Playerbot::PrePullCc
