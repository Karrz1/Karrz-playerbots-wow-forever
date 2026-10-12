// What killed a raid member, kept from the death hook (LogPlayerDeath, the [bot_death] line) until the raid plan's
// [raid_death] line reads it. The plan's own "hit by" only lists mobs whose current victim was the member, so an area
// spell (Baron Geddon's Inferno, a breath, a Smash splash) read "nothing seen" (MC Geddon 2026-10-11).
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Playerbot::V2 {

class DeathCause
{
public:
    static void Note(uint64_t guid_low, std::string text)
    {
        std::lock_guard<std::mutex> lk(Mu());
        Map()[guid_low] = std::move(text);
    }
    // Reads and forgets the cause; empty when none was noted.
    static std::string Take(uint64_t guid_low)
    {
        std::lock_guard<std::mutex> lk(Mu());
        auto it = Map().find(guid_low);
        if (it == Map().end())
            return {};
        std::string text = std::move(it->second);
        Map().erase(it);
        return text;
    }

private:
    static std::mutex& Mu() { static std::mutex m; return m; }
    static std::unordered_map<uint64_t, std::string>& Map() { static std::unordered_map<uint64_t, std::string> m; return m; }
};

} // namespace Playerbot::V2
