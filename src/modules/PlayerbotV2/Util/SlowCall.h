// SlowCall - slow-update watchdog. Put `SLOW_CALL("name")` at the top of any world-thread call that can block (database
// round trips, whole-fleet passes). When the scope takes longer than the threshold it logs one line naming the caller, so a
// lag spike is traced to the function that caused it. Per-name rate limit: one line per 5 s, with the count suppressed.

#pragma once

#include "Log.h"
#include "Timer.h"
#include <atomic>

namespace Playerbot {

struct SlowCallSite
{
    char const* name;
    std::atomic<uint32> last_log_ms{ 0 };
    std::atomic<uint32> suppressed{ 0 };
    explicit SlowCallSite(char const* n) : name(n) {}
};

// Slowest call since the last Take(), for the load-test row. World thread (the sites are world-thread calls).
struct SlowCallWorst
{
    char const* name = nullptr;
    uint32 ms = 0;
    static SlowCallWorst& Instance() { static SlowCallWorst w; return w; }
    void Note(char const* n, uint32 took) { if (took > ms) { ms = took; name = n; } }
    SlowCallWorst Take() { SlowCallWorst out = *this; name = nullptr; ms = 0; return out; }
};

class SlowCall
{
public:
    SlowCall(SlowCallSite& site, uint32 warn_ms) : site_(site), warn_ms_(warn_ms), start_(getMSTime()) {}
    SlowCall(SlowCall const&) = delete;
    SlowCall& operator=(SlowCall const&) = delete;
    ~SlowCall()
    {
        uint32 const now = getMSTime();
        uint32 const took = getMSTimeDiff(start_, now);
        if (took >= 1u)
            SlowCallWorst::Instance().Note(site_.name, took);
        if (took < warn_ms_)
            return;
        uint32 const last = site_.last_log_ms.load(std::memory_order_relaxed);
        if (last && getMSTimeDiff(last, now) < 5000u)
        {
            site_.suppressed.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        site_.last_log_ms.store(now ? now : 1u, std::memory_order_relaxed);
        TC_LOG_WARN("playerbot.v2", "[slow_call] {} took {} ms (limit {} ms, {} more since the last line)",
                    site_.name, took, warn_ms_, site_.suppressed.exchange(0, std::memory_order_relaxed));
    }

private:
    SlowCallSite& site_;
    uint32 warn_ms_;
    uint32 start_;
};

} // namespace Playerbot

#define PB_SLOW_CONCAT2(a, b) a##b
#define PB_SLOW_CONCAT(a, b) PB_SLOW_CONCAT2(a, b)
// Default limit 20 ms (a world tick is 50 ms at the target rate). SLOW_CALL_MS(name, limit) overrides it.
#define SLOW_CALL_MS(name, limit) \
    static ::Playerbot::SlowCallSite PB_SLOW_CONCAT(pb_slow_site_, __LINE__){ name }; \
    ::Playerbot::SlowCall PB_SLOW_CONCAT(pb_slow_scope_, __LINE__){ PB_SLOW_CONCAT(pb_slow_site_, __LINE__), (limit) }
#define SLOW_CALL(name) SLOW_CALL_MS(name, 20u)
