// LoadTestRecorder - one CSV row per minute for a load-test session: tick time percentiles, the slowest caller, memory, CPU and
// bot counts by kind, with a pass or fail against fixed limits. The same scenario (a 40-bot Molten Core raid, a full Warsong
// Gulch and N sleeping world bots) is run on every build and the rows are compared, so a regression shows as a failed row.
// Pure: no game types, tested in tests/game/ClassicLoadTest.cpp.

#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace Playerbot::Diagnostics {

// Per-tick wall time histogram in 1 ms buckets up to 499 ms; the last bucket collects everything slower.
class TickHistogram
{
public:
    static constexpr uint32_t kBuckets = 500;

    void Add(uint32_t ms)
    {
        ++counts_[ms < kBuckets ? ms : kBuckets - 1];
        ++n_;
        sum_ += ms;
        if (ms > max_) max_ = ms;
    }
    uint32_t Count() const { return n_; }
    uint32_t Max() const { return max_; }
    double   Avg() const { return n_ ? double(sum_) / double(n_) : 0.0; }
    // Smallest tick time t (ms) such that at least p percent of ticks took <= t. p in (0, 100].
    uint32_t Percentile(double p) const
    {
        if (!n_) return 0;
        uint64_t const need = uint64_t((p / 100.0) * double(n_) + 0.999999);
        uint64_t run = 0;
        for (uint32_t i = 0; i < kBuckets; ++i)
        {
            run += counts_[i];
            if (run >= need) return i;
        }
        return kBuckets - 1;
    }
    void Reset() { counts_.fill(0); n_ = 0; sum_ = 0; max_ = 0; }

private:
    std::array<uint32_t, kBuckets> counts_{};
    uint32_t n_ = 0;
    uint64_t sum_ = 0;
    uint32_t max_ = 0;
};

struct LoadRow
{
    uint64_t    unix_time = 0;
    std::string build;
    std::string label;
    uint32_t    bots_awake = 0;       // every bot logged in
    uint32_t    bots_raid = 0;        // in a raid group
    uint32_t    bots_bg = 0;          // in a battleground
    uint32_t    bots_world = 0;       // ambient residents awake
    uint32_t    bots_sleeping = 0;    // roster bots offline
    uint32_t    ticks = 0;
    double      tick_avg_ms = 0;
    uint32_t    tick_p50_ms = 0;
    uint32_t    tick_p99_ms = 0;
    uint32_t    tick_max_ms = 0;
    std::string slowest_call;         // name of the slowest [slow_call] in the window, empty if none
    uint32_t    slowest_call_ms = 0;
    uint32_t    rss_mb = 0;
    double      cpu_pct = 0;
};

struct LoadLimits
{
    uint32_t max_p99_ms = 100;
    double   max_avg_ms = 40.0;
    uint32_t max_rss_mb = 9000;
    uint32_t max_slow_call_ms = 250;
};

// Empty = pass. Otherwise the failed checks joined by ';'.
inline std::string Verdict(LoadRow const& r, LoadLimits const& l)
{
    std::string out;
    auto add = [&out](std::string const& s) { if (!out.empty()) out += ';'; out += s; };
    if (r.tick_p99_ms > l.max_p99_ms)           add("p99>" + std::to_string(l.max_p99_ms));
    if (r.tick_avg_ms > l.max_avg_ms)           add("avg>" + std::to_string(uint32_t(l.max_avg_ms)));
    if (r.rss_mb > l.max_rss_mb)                add("rss>" + std::to_string(l.max_rss_mb));
    if (r.slowest_call_ms > l.max_slow_call_ms) add("slow_call>" + std::to_string(l.max_slow_call_ms));
    return out;
}

inline std::string CsvHeader()
{
    return "unix_time,build,label,bots_awake,bots_raid,bots_bg,bots_world,bots_sleeping,ticks,tick_avg_ms,tick_p50_ms,tick_p99_ms,"
           "tick_max_ms,slowest_call,slowest_call_ms,rss_mb,cpu_pct,result,failed";
}

// A field never contains a comma or a quote after this.
inline std::string CsvSafe(std::string s)
{
    for (char& c : s)
        if (c == ',' || c == '"' || c == '\n' || c == '\r') c = ' ';
    return s;
}

inline std::string CsvRow(LoadRow const& r, LoadLimits const& l)
{
    std::string const v = Verdict(r, l);
    char avg[32], cpu[32];
    std::snprintf(avg, sizeof(avg), "%.2f", r.tick_avg_ms);
    std::snprintf(cpu, sizeof(cpu), "%.1f", r.cpu_pct);
    return std::to_string(r.unix_time) + ',' + CsvSafe(r.build) + ',' + CsvSafe(r.label) + ',' + std::to_string(r.bots_awake) + ','
        + std::to_string(r.bots_raid) + ',' + std::to_string(r.bots_bg) + ',' + std::to_string(r.bots_world) + ','
        + std::to_string(r.bots_sleeping) + ',' + std::to_string(r.ticks) + ',' + avg + ',' + std::to_string(r.tick_p50_ms) + ','
        + std::to_string(r.tick_p99_ms) + ',' + std::to_string(r.tick_max_ms) + ',' + CsvSafe(r.slowest_call) + ','
        + std::to_string(r.slowest_call_ms) + ',' + std::to_string(r.rss_mb) + ',' + cpu + ',' + (v.empty() ? "PASS" : "FAIL") + ','
        + CsvSafe(v);
}

} // namespace Playerbot::Diagnostics
