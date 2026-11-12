// BotPool - one bot pool with a role tag, and one eligibility rule for every filler.
//
// Every bot is Owned (a companion or a player's own character), Ambient (the persistent world population) or Bg (an
// ambient bot currently committed to a battleground / queue fill). Gear tier comes from the role. Every system that
// draws free bots (battleground queue fill, premade forming, top-up, auto-join, group building) asks CheckFree()
// instead of keeping its own list of conditions, so they can never disagree about who is available.
// Pure: no game types, tested in tests/game/ClassicBotPool.cpp. Player-backed helpers are in BotPool.cpp.

#pragma once

#include <cstdint>

namespace Playerbot::Pool {

enum class BotRole : uint8_t { Owned, Ambient, Bg };

// Gear: the world tier (Playerbot.World.Gear.*) for everything nobody owns, the raid tier for the owner's bots.
constexpr bool UsesWorldGear(BotRole r) { return r != BotRole::Owned; }

struct RoleFacts
{
    bool owner_bound = false;       // has an owner account (companion)
    bool non_pool_account = false;  // lives on a real player's account (an alt)
    bool bg_committed = false;      // ambient bot confined to a battleground / queue fill (jit purpose)
    bool scenario = false;          // a scenario test squad member (Diagnostics/ScenarioRunner): held like an owner's bot
};

constexpr BotRole ClassifyRole(RoleFacts const& f)
{
    if (f.owner_bound || f.non_pool_account || f.scenario) return BotRole::Owned;
    return f.bg_committed ? BotRole::Bg : BotRole::Ambient;
}

struct FreeFacts
{
    BotRole role = BotRole::Ambient;
    bool in_world = true;
    bool alive = true;
    bool grouped = false;
    bool in_combat = false;
    bool teleporting = false;
    bool on_instance_map = false;   // dungeon, raid, battleground or arena map
    bool world_busy = false;        // world spawner still setting it up, or it stands as a resident for a player
};

enum class Verdict : uint8_t { Free, NotAmbient, Committed, Absent, Dead, Grouped, Combat, Instanced, WorldBusy };

// The one rule: ambient, in the world, alive, ungrouped, out of combat, not in an instance, not owner-bound, not held by
// the world spawner. A Bg-committed bot is not free for a new fill (it is already confined to one).
constexpr Verdict CheckFree(FreeFacts const& f)
{
    if (f.role == BotRole::Owned)  return Verdict::NotAmbient;
    if (f.role == BotRole::Bg)     return Verdict::Committed;
    if (!f.in_world || f.teleporting) return Verdict::Absent;
    if (!f.alive)                  return Verdict::Dead;
    if (f.grouped)                 return Verdict::Grouped;
    if (f.in_combat)               return Verdict::Combat;
    if (f.on_instance_map)         return Verdict::Instanced;
    if (f.world_busy)              return Verdict::WorldBusy;
    return Verdict::Free;
}

} // namespace Playerbot::Pool

#include "Define.h"
#include <vector>
class Player;

namespace Playerbot::Pool {

// Role of a bot by id. Not a lifecycle bot (a real player) counts as Owned.
BotRole RoleOf(uint64 bot_id);
// A scenario test squad member is Owned without an owner account: raid-tier gear, never scrubbed to the world tier, never freed or
// drawn into a battleground fill mid-run. Marked by the scenario runner before the bot's setup, and at the first world tick for every
// catalogue character that already exists (Scenario::MarkTestCharacters), so a restart does not turn them into pool bots; stays for the session.
void MarkScenarioBot(uint64 bot_id);
bool IsScenarioBot(uint64 bot_id);
// Every character marked so far (the scenario runner's idle sweep logs out the ones still online).
std::vector<uint64> ScenarioBots();
// Facts for a connected bot and the one-call verdict.
FreeFacts FreeFactsOf(Player const* bot, uint64 bot_id);
inline Verdict CheckFreeBot(Player const* bot, uint64 bot_id) { return CheckFree(FreeFactsOf(bot, bot_id)); }

} // namespace Playerbot::Pool
