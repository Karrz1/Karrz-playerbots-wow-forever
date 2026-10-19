// TransportWaitMemory - what bots have learned about a same-map transport they waited for in vain. A bot that stands on a dock for
// kGiveUpMs without the ride coming in (the Deeprun Tram, 2026-10-18: eight soak legs spent their whole 20 minutes on the Stormwind
// platform) gives that ride up; once kMinGiveUps bots have, the router leaves the ride out of its routes for a while, so the next plans
// take another way instead of queueing at the same dock. Keyed by the ride's transport template id. Pure: no game types, tested in
// tests/game/ClassicTransportWaitMemory.cpp.

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace Playerbot::V2::Travel {

class TransportWaitMemory
{
public:
    static constexpr std::uint32_t kGiveUpMs = 300u * 1000u;         // a ride comes in every minute or two; five minutes without it is not waiting
    static constexpr std::uint32_t kTtlMs = 30u * 60u * 1000u;       // a restart or a script fix is the usual cure, so try again after a while
    static constexpr std::uint32_t kMinGiveUps = 2;

    // True when `since_ms` (0 = not waiting yet) is old enough to give the ride up.
    static bool ShouldGiveUp(std::uint32_t since_ms, std::uint32_t now_ms)
    {
        return since_ms != 0 && std::uint32_t(now_ms - since_ms) >= kGiveUpMs;
    }

    void NoteGaveUp(std::uint32_t route, std::uint32_t now_ms)
    {
        if (route == 0)
            return;
        std::lock_guard<std::mutex> lk(mx_);
        Entry& e = routes_[route];
        if (e.give_ups == 0 || std::uint32_t(now_ms - e.last_ms) >= kTtlMs)
            e.give_ups = 0;
        ++e.give_ups;
        e.last_ms = now_ms;
    }

    // A ride that was boarded proves it runs: forget the give-ups.
    void NoteBoarded(std::uint32_t route)
    {
        std::lock_guard<std::mutex> lk(mx_);
        routes_.erase(route);
    }

    bool Unusable(std::uint32_t route, std::uint32_t now_ms) const
    {
        if (route == 0)
            return false;
        std::lock_guard<std::mutex> lk(mx_);
        auto const it = routes_.find(route);
        return it != routes_.end() && it->second.give_ups >= kMinGiveUps && std::uint32_t(now_ms - it->second.last_ms) < kTtlMs;
    }

    static TransportWaitMemory& Instance()
    {
        static TransportWaitMemory inst;
        return inst;
    }

private:
    struct Entry { std::uint32_t last_ms = 0; std::uint32_t give_ups = 0; };
    mutable std::mutex mx_;
    std::unordered_map<std::uint32_t, Entry> routes_;
};

} // namespace Playerbot::V2::Travel
