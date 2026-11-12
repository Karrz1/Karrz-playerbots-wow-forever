// PlayerWorkAreas - where the real players are working, so the world bots leave them their mobs. A real player who is clearing a camp
// or doing a kill / collect quest should not find it emptied by the crowd around him: a bot of the world (no owner, not led by a player)
// does not open on a mob next to a real player it is not grouped with, nor on a mob that player's quests need while he is near.
// Mobs that attack the bot are still fought (that is the combat state, not this). Playerbot.World.ShareMobs.* switch and size it.
//
// Pure part: the table and the claim test. The world thread publishes the table once a second (WorldBotSpawner::CollectAnchors, from
// each real player's position and open quest objectives); the idle rules read it from any thread. Tested in
// tests/game/ClassicPlayerWorkAreas.cpp.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Playerbot::V2::WorkAreas
{

struct Area
{
    std::uint64_t player = 0;                 // guid counter of the real player
    std::uint64_t group = 0;                  // his group's guid counter, 0 = not grouped
    std::uint32_t map = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    std::vector<std::uint32_t> kill_entries;  // creature entries his open kill objectives need (with kill-credit aliases)
    std::vector<std::uint32_t> item_entries;  // items his open collect objectives need
};

struct Settings
{
    bool  enabled = true;
    float camp_radius = 35.0f;    // a mob this close to a real player is his
    float quest_radius = 120.0f;  // a mob his quests need is his while he is this close
};

enum class Claim : std::uint8_t { None, Camp, Quest };

inline bool Contains(std::vector<std::uint32_t> const& v, std::uint32_t e)
{
    return std::find(v.begin(), v.end(), e) != v.end();
}

// Is the mob at (mx, my) of `entry` on `map` a real player's? `bot_group` is the asking bot's group (0 = none): a player in the bot's
// own group shares his mobs. `drops(item, entry)` says whether the creature drops a quest item.
template <class DropsFn>
Claim ClaimOf(std::vector<Area> const& areas, Settings const& cfg, std::uint32_t map, float mx, float my, std::uint32_t entry,
              std::uint64_t bot_group, DropsFn&& drops)
{
    if (!cfg.enabled)
        return Claim::None;
    Claim best = Claim::None;
    for (Area const& a : areas)
    {
        if (a.map != map || (bot_group && a.group == bot_group))
            continue;
        float const d = std::hypot(a.x - mx, a.y - my);
        if (d <= cfg.camp_radius)
            return Claim::Camp;
        if (d > cfg.quest_radius || best != Claim::None)
            continue;
        if (Contains(a.kill_entries, entry))
        {
            best = Claim::Quest;
            continue;
        }
        for (std::uint32_t item : a.item_entries)
            if (drops(item, entry))
            {
                best = Claim::Quest;
                break;
            }
    }
    return best;
}

// Is any real player (not in `bot_group`) within `radius` of (x, y) on `map`? Used to keep a bot from walking to a grind spot that a
// player is working.
inline bool PlayerNear(std::vector<Area> const& areas, std::uint32_t map, float x, float y, float radius, std::uint64_t bot_group = 0)
{
    for (Area const& a : areas)
        if (a.map == map && !(bot_group && a.group == bot_group) && std::hypot(a.x - x, a.y - y) <= radius)
            return true;
    return false;
}

// The published table. Readers take a shared pointer to an immutable copy; the world thread swaps in a new one.
class Board
{
public:
    static Board& Get()
    {
        static Board b;
        return b;
    }
    void Publish(std::vector<Area> areas)
    {
        auto next = std::make_shared<std::vector<Area> const>(std::move(areas));
        std::lock_guard<std::mutex> g(mtx_);
        cur_ = std::move(next);
    }
    std::shared_ptr<std::vector<Area> const> Current() const
    {
        std::lock_guard<std::mutex> g(mtx_);
        return cur_;
    }
    std::atomic<std::uint32_t> camp_skips{0}, quest_skips{0}, spot_skips{0};

private:
    mutable std::mutex mtx_;
    std::shared_ptr<std::vector<Area> const> cur_ = std::make_shared<std::vector<Area> const>();
};

} // namespace Playerbot::V2::WorkAreas
