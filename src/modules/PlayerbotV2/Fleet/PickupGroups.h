// PickupGroups - the game groups two world bots form on their own (idle:invite_nearby_player, Playerbot.Behavior.InvitePassingBots).
//
// Such a group used to last until the server restarted: its members counted as "tied up" (outside the world cap, like a dungeon group),
// the spawner never logged a grouped bot out, and the runtime group hygiene left every pure-bot group alone. Bots paired off as they
// logged in and the grouped count only grew, 59 -> 191 in 13 minutes on a 300-bot realm until memory ran out (GitHub #345).
// A pickup group is now a world thing like a BotParties party:
//   - its members count toward Playerbot.World.MaxBots (WorldBotSpawner::DriveDespawn);
//   - it breaks up once no member is near a player any more (the spawner then logs them out as it does any far world bot);
//   - and it breaks up after its life, the way strangers team up for a few kills and go their own ways (module group hygiene).
// Only groups made through API::accept_group_invite with a bot as the new leader are noted; a group with a real player in it is a
// player's group and is forgotten here.
//
// World thread only (the intent executor and the world tick).
#pragma once

#include <cstdint>
#include <unordered_map>

namespace Playerbot::V2::PickupGroups
{

// How long a pickup group lasts: 10 to 20 minutes, spread by group id so the pairs do not all break up at once.
constexpr std::uint32_t kLifeMinMs = 10u * 60u * 1000u;
constexpr std::uint32_t kLifeSpreadMs = 10u * 60u * 1000u;

struct Entry
{
    std::uint32_t formed_ms = 0;
    std::uint32_t life_ms = kLifeMinMs;
};

inline std::unordered_map<std::uint64_t, Entry>& Table()
{
    static std::unordered_map<std::uint64_t, Entry> table;
    return table;
}

inline std::uint32_t LifeFor(std::uint64_t group_id)
{
    return kLifeMinMs + std::uint32_t((group_id * 2654435761ull) % kLifeSpreadMs);
}

inline void Note(std::uint64_t group_id, std::uint32_t now_ms)
{
    Table()[group_id] = Entry{ now_ms, LifeFor(group_id) };
}

inline bool Has(std::uint64_t group_id) { return Table().contains(group_id); }
inline void Forget(std::uint64_t group_id) { Table().erase(group_id); }

// Its life is over (wrap-safe on the 32-bit millisecond clock).
inline bool Expired(Entry const& e, std::uint32_t now_ms)
{
    return std::uint32_t(now_ms - e.formed_ms) >= e.life_ms;
}

// What the spawner saw of one pickup group's online members this pass.
struct Seen
{
    bool near_player = false;   // a member stands within DespawnRadius of a player (or in his bubble by the spawner's other tests)
    bool busy = false;          // a member fights, is being teleported, or stands in an instance / battleground / its queue
};

// The spawner breaks a group up when nobody of it is near a player and nobody of it is busy: logging a bot out mid-fight or
// mid-teleport would break the fight or the move, and the next pass looks again.
inline bool BreakUpForDistance(Seen const& s)
{
    return !s.near_player && !s.busy;
}

} // namespace Playerbot::V2::PickupGroups
