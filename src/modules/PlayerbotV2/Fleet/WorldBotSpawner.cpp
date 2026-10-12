// WorldBotSpawner - see the header for the model. World thread only.

#include "Fleet/WorldLeash.h"
#include "WorldBotSpawner.h"
#include "WakeSelect.h"
#include "WorldFactionMix.h"
#include "SkyborneIsle.h"
#include "BotPopulationManager.h"
#include "BotIdentityRegistry.h"
#include "OwnerRegistry.h"
#include "../Travel/QuestHubDatabase.h"
#include "../Threading/TickScheduler.h"
#include "../Services.h"
#include "../Session/BotSessionMgr.h"
#include "../Threading/SnapshotPublisher.h"
#include "../Bot/BotRegistry.h"
#include "../Bot/World/ZonePicker.h"
#include "../Bot/World/SpreadSpot.h"
#include "Config.h"
#include "GameTime.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "Creature.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotMovement.h"
#include "Random.h"
#include "StringFormat.h"
#include "Timer.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#ifdef _WIN32
#  ifndef PSAPI_VERSION
#    define PSAPI_VERSION 2
#  endif
#  include <windows.h>
#  include <psapi.h>
#endif

namespace Playerbot::V2::Fleet {

namespace {

constexpr uint32 kConfigReloadMs   = 15000u;
constexpr uint32 kReportMs         = 60000u;
constexpr uint32 kMemReportMs      = 60000u;    // [mem] line
constexpr uint32 kPendingTimeoutMs = 120000u;   // login + setup pipeline + placement
constexpr uint32 kQueryTimeoutMs   = 20000u;

float Dist2D(float ax, float ay, float bx, float by)
{
    float const dx = ax - bx, dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}

bool IsAllianceRace(uint8 race)
{
    return Player::TeamIdForRace(race) == TEAM_ALLIANCE;
}

// WHERE fragment keeping Skyborne pool rows where they belong (Skyborne::PoolRowFitsAnchor): around a Zephras anchor only Skyborne
// bots of the island's levels, anywhere else no Skyborne bot below level 10 (it sleeps on the island).
std::string SkyborneRaceClause(bool anchor_on_isle)
{
    if (anchor_on_isle)
        return Trinity::StringFormat("AND c.race IN ({}, {}) AND c.level BETWEEN {} AND {} ",
            uint32(Skyborne::kRaceAlliance), uint32(Skyborne::kRaceHorde), uint32(Skyborne::kIsleLevelLo), uint32(Skyborne::kIsleLevelHi));
    return Trinity::StringFormat("AND NOT (c.race IN ({}, {}) AND c.level < {}) ",
        uint32(Skyborne::kRaceAlliance), uint32(Skyborne::kRaceHorde), uint32(Skyborne::kGraduationLevel));
}

} // namespace

// Resident memory of this process in KB; 0 when unavailable.
// Process CPU time (user + kernel), ms; the per-bot CPU figure in the [world] line divides its growth by the awake bots.
static uint64 ProcessCpuMs()
{
#ifdef _WIN32
    FILETIME c{}, e{}, k{}, u{};
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
    {
        ULARGE_INTEGER ku{}, uu{};
        ku.LowPart = k.dwLowDateTime; ku.HighPart = k.dwHighDateTime;
        uu.LowPart = u.dwLowDateTime; uu.HighPart = u.dwHighDateTime;
        return (ku.QuadPart + uu.QuadPart) / 10000ull;   // 100 ns units
    }
    return 0;
#else
    timespec ts{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0)
        return uint64(ts.tv_sec) * 1000ull + uint64(ts.tv_nsec) / 1000000ull;
    return 0;
#endif
}

static uint64 ProcessRssKb()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return uint64(pmc.WorkingSetSize / 1024);
    return 0;
#else
    if (FILE* f = std::fopen("/proc/self/statm", "r"))
    {
        unsigned long size = 0, resident = 0;
        int const n = std::fscanf(f, "%lu %lu", &size, &resident);
        std::fclose(f);
        if (n == 2)
            return uint64(resident) * 4u;   // 4 KB pages
    }
    return 0;
#endif
}

// Private (committed, non-shared) bytes in KB; Windows only, 0 elsewhere. The working set can hide memory the OS has paged out.
static uint64 ProcessPrivateKb()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return uint64(pmc.PrivateUsage / 1024);
#endif
    return 0;
}

// Counters only, no per-object work beyond one pass over the loaded maps (shared lock, as the core does it).
// Splits the per-bot RAM figure: grids and the creatures/gameobjects they hold vs players vs the rest of the process.
static std::string MemReport(uint32 bots_online)
{
    struct Row { uint32 id; uint32 inst; uint32 grids; uint64 creatures; uint64 gos; uint32 players; };
    std::vector<Row> rows;
    uint64 grids = 0, creatures = 0, gos = 0;
    uint32 players = 0, maps = 0;
    sMapMgr->DoForAllMaps([&](Map* m)
    {
        Row r{ m->GetId(), m->GetInstanceId(), uint32(m->GridRefManager<NGridType>::size()),
               uint64(m->GetObjectsStore().Size<Creature>()), uint64(m->GetObjectsStore().Size<GameObject>()),
               uint32(m->GetPlayers().size()) };
        ++maps;
        grids += r.grids; creatures += r.creatures; gos += r.gos; players += r.players;
        rows.push_back(r);
    });
    std::sort(rows.begin(), rows.end(), [](Row const& a, Row const& b) { return a.creatures + a.gos > b.creatures + b.gos; });
    std::string top;
    for (size_t i = 0; i < rows.size() && i < 8; ++i)
        top += Trinity::StringFormat("{}{}:{}g/{}c/{}o/{}p", i ? " " : "", rows[i].id, rows[i].grids, rows[i].creatures, rows[i].gos, rows[i].players);
    return Trinity::StringFormat(
        "[mem] rss_mb={} private_mb={} | maps={} grids={} creatures={} gameobjects={} players_on_maps={} sessions={} bots_online={} bot_sessions={} registry={} snapshots={} | top maps (id:grids/creatures/gobjs/players) {}",
        ProcessRssKb() / 1024, ProcessPrivateKb() / 1024, maps, grids, creatures, gos, players, ::World::instance()->GetActiveSessionCount(), bots_online,
        Services::SessionMgr().active_count(), Services::Registry().size(), Services::Snapshots().bot_count(), top);
}

WorldBotSpawner::WorldBotSpawner(BotPopulationManager& owner) : owner_(owner) {}

void WorldBotSpawner::LoadConfig()
{
    Config c;
    c.enable           = sConfigMgr->GetBoolDefault  ("Playerbot.World.Enable", false);
    c.max_bots         = uint32(sConfigMgr->GetIntDefault("Playerbot.World.MaxBots", 100));
    c.bots_per_player  = uint32(sConfigMgr->GetIntDefault("Playerbot.World.BotsPerPlayer", 25));
    c.city_bots        = uint32(sConfigMgr->GetIntDefault("Playerbot.World.CityBots", 0));
    c.zone_bots        = uint32(sConfigMgr->GetIntDefault("Playerbot.World.ZoneBots", 0));
    c.activity_radius  = std::max(50.0f, sConfigMgr->GetFloatDefault("Playerbot.World.ActivityRadius", 300.0f));
    c.despawn_radius   = std::max(c.activity_radius * 1.1f,
                                  sConfigMgr->GetFloatDefault("Playerbot.World.DespawnRadius", c.activity_radius * 1.5f));
    c.ring_min         = std::max(20.0f, sConfigMgr->GetFloatDefault("Playerbot.World.SpawnRingMin", 110.0f));
    c.ring_max         = std::max(c.ring_min + 20.0f, sConfigMgr->GetFloatDefault("Playerbot.World.SpawnRingMax", 200.0f));
    c.ring_max         = std::min(c.ring_max, c.activity_radius * 0.9f);
    c.ring_min         = std::min(c.ring_min, c.ring_max - 10.0f);
    c.tick_ms          = std::max<uint32>(500, uint32(sConfigMgr->GetIntDefault("Playerbot.World.TickMs", 1000)));
    c.spawn_per_tick   = std::max<uint32>(1, uint32(sConfigMgr->GetIntDefault("Playerbot.World.SpawnPerTick", 2)));
    c.global_fill      = sConfigMgr->GetBoolDefault("Playerbot.World.GlobalFill", false);
    c.global_per_tick  = std::max<uint32>(1, uint32(sConfigMgr->GetIntDefault("Playerbot.World.GlobalPerTick", 3)));
    c.filler_hub_only  = sConfigMgr->GetBoolDefault("Playerbot.World.FillerHubOnly", true);
    c.rss_budget_mb    = uint32(sConfigMgr->GetIntDefault("Playerbot.World.RssBudgetMb", 0));
    c.town_pct         = std::min<uint32>(100, uint32(sConfigMgr->GetIntDefault("Playerbot.World.TownPct", 70)));
    c.city_spawn_per_tick = uint32(sConfigMgr->GetIntDefault("Playerbot.World.CitySpawnPerTick", 0));
    c.seed_roster      = sConfigMgr->GetBoolDefault("Playerbot.World.SeedRoster", false);
    c.seed_in_flight   = std::max<uint32>(1, uint32(sConfigMgr->GetIntDefault("Playerbot.World.SeedInFlight", 8)));
    c.wake_radius      = std::max(0.0f, sConfigMgr->GetFloatDefault("Playerbot.World.WakeRadius", 0.0f));
    c.zone_grace_ms    = uint32(sConfigMgr->GetIntDefault("Playerbot.World.ZoneGraceSec", 300)) * 1000u;
    c.despawn_per_tick = std::max<uint32>(1, uint32(sConfigMgr->GetIntDefault("Playerbot.World.DespawnPerTick", 5)));
    c.placed_grace_ms  = uint32(sConfigMgr->GetIntDefault("Playerbot.World.PlacedGraceSec", 30)) * 1000u;
    c.min_level        = std::clamp<uint32>(uint32(sConfigMgr->GetIntDefault("Playerbot.World.MinLevel", 10)), 1, 60);
    c.max_level        = std::clamp<uint32>(uint32(sConfigMgr->GetIntDefault("Playerbot.World.MaxLevel", 60)), c.min_level, 60);
    c.friendly_pct     = std::min<uint32>(100, uint32(sConfigMgr->GetIntDefault("Playerbot.World.FriendlyPct", 80)));
    c.mix.default_pct   = c.friendly_pct;
    c.mix.by_zone       = sConfigMgr->GetBoolDefault("Playerbot.World.ZoneMix", true);
    c.mix.capital_pct   = std::min<uint32>(100, uint32(sConfigMgr->GetIntDefault("Playerbot.World.CapitalFriendlyPct", 90)));
    c.mix.home_pct      = std::min<uint32>(100, uint32(sConfigMgr->GetIntDefault("Playerbot.World.HomeFriendlyPct", 90)));
    c.mix.border_pct    = std::min<uint32>(100, uint32(sConfigMgr->GetIntDefault("Playerbot.World.BorderFriendlyPct", 75)));
    c.mix.contested_pct = std::min<uint32>(100, uint32(sConfigMgr->GetIntDefault("Playerbot.World.ContestedFriendlyPct", 60)));
    c.pool_max         = uint32(sConfigMgr->GetIntDefault("Playerbot.World.PoolMax", std::max<uint32>(c.max_bots * 4, 200)));
    c.pool_horde_pct   = std::clamp<uint32>(uint32(sConfigMgr->GetIntDefault("Playerbot.World.PoolHordePct", 50)), 5, 95);
    c.skyborne_seed_pct = std::min<uint32>(100, uint32(sConfigMgr->GetIntDefault("Playerbot.World.SkyborneSeedPct", 5)));
    cfg_ = c;
}

WorldLoadFacts WorldBotSpawner::Load() const
{
    WorldLoadFacts f;
    f.awake = ambient_online_;
    int64 const pool = pool_count_ < 0 ? 0 : pool_count_;
    f.sleeping = pool > int64(all_online_) ? uint32(pool - int64(all_online_)) : 0u;
    f.rss_mb = uint32(ProcessRssKb() / 1024u);
    f.cpu_pct = cpu_pct_;
    return f;
}

std::string WorldBotSpawner::Report() const
{
    uint64 const rss = ProcessRssKb();
    double const avg = tick_count_ ? double(tick_cost_sum_ms_) / double(tick_count_) : 0.0;
    // RAM cost per ambient bot: growth over the no-bot baseline, only meaningful with some bots online.
    double per_bot_mb = 0.0;
    // Divided by ALL bots in the world (raid, BG fillers, companions, ambient): the growth over the baseline is paid by
    // all of them, dividing by the ambient ones alone read 15 MB when it was about 5.5 (stage B, 2026-10-07).
    if (all_online_ >= 10 && baseline_rss_kb_ && rss > baseline_rss_kb_)
        per_bot_mb = double(rss - baseline_rss_kb_) / 1024.0 / double(all_online_);
    std::string line = Trinity::StringFormat(
        "[world] enable{} anchors={} (in_instance={}) bots={}/{} (all_bots={} far={} global_settled={} rss_cap={}) pending={} placed={} | skipped no_match={} pool_full={} spawn_failed={} login_failed={} | logged_in={} created={} placed_total={} "
        "despawned={} abandoned={} | spawner_tick_ms last={} avg={:.2f} max={} | rss_mb={} baseline_mb={} per_bot_mb={:.2f}",
        cfg_.enable ? 1 : 0, anchors_, anchors_in_instance_, ambient_online_, cfg_.max_bots, all_online_, global_online_, n_global_settled_, rss_capped_ ? 1 : 0, pending_.size(), placed_.size(),
        n_no_match_, n_pool_full_, n_spawn_failed_, n_login_failed_,
        n_logged_in_, n_created_, n_placed_, n_despawned_, n_abandoned_, last_tick_cost_ms_, avg, max_tick_cost_ms_,
        rss / 1024, baseline_rss_kb_ / 1024, per_bot_mb);
    auto const tiers = Services::Scheduler().tier_counts();
    // Persistent residents: the pool is the roster, a bot logged out stays where it stood. sleeping = pool bots offline.
    int64 const pool = pool_count_ < 0 ? 0 : pool_count_;
    int64 const sleeping = pool > int64(all_online_) ? pool - int64(all_online_) : 0;
    double const awake_ram_mb = all_online_ ? double(rss) / 1024.0 / double(all_online_) : 0.0;
    line += Trinity::StringFormat(" | roster pool={} awake={} sleeping={} seeding={} seeded_total={} woken_total={} wake_dry={} wake_radius={:.0f} | cost proc_cpu_pct={:.1f} cpu_pct_per_awake_bot={:.3f} ram_mb_per_awake_bot_incl_base={:.2f} ram_mb_per_sleeping_bot=0 (offline: a row in the characters table)",
        pool, all_online_, sleeping, seeding_.size(), n_seeded_, n_woken_, n_wake_dry_, cfg_.wake_radius, cpu_pct_, all_online_ ? cpu_pct_ / double(all_online_) : 0.0, awake_ram_mb);
    line += Trinity::StringFormat(" | tiers combat={} active={} cruise={} idle={} parked={}", tiers[0], tiers[1], tiers[2], tiers[3], tiers[4]);
    return line;
}

void WorldBotSpawner::CollectAnchors(std::vector<Anchor>& out)
{
    anchors_in_instance_ = 0;
    auto& lifecycle = Services::Lifecycle();
    for (auto const& kv : ::World::instance()->GetAllSessions())
    {
        WorldSession* session = kv.second;
        if (!session) continue;
        Player* p = session->GetPlayer();
        if (!p || !p->IsInWorld() || p->IsBeingTeleported()) continue;
        if (lifecycle.is_bot(p->GetGUID().GetCounter())) continue;   // a bot, not a real person
        Map* m = p->GetMap();
        if (!m) continue;
        if (m->IsDungeon() || m->IsBattlegroundOrArena())
        {
            ++anchors_in_instance_;   // world bots stay in the open world: nothing to spawn around a player inside
            continue;
        }
        Anchor a;
        a.guid     = p->GetGUID();
        a.map      = p->GetMapId();
        a.x        = p->GetPositionX();
        a.y        = p->GetPositionY();
        a.z        = p->GetPositionZ();
        a.zone     = p->GetZoneId();
        a.level    = uint8(p->GetLevel());
        a.alliance = p->GetTeam() == ALLIANCE;
        out.push_back(a);
    }
}

uint32 WorldBotSpawner::SpawnRateFor(Anchor const& a) const
{
    uint8 lo = 0, hi = 0;
    bool const town = !::Playerbot::V2::World::VanillaZoneLevelRange(a.zone, lo, hi);
    return town && cfg_.city_spawn_per_tick ? cfg_.city_spawn_per_tick : cfg_.spawn_per_tick;
}

uint32 WorldBotSpawner::TargetFor(Anchor const& a) const
{
    // A capital is packed (CityBots), a leveling zone gets an ordinary density spread over ActivityRadius (ZoneBots).
    uint8 lo = 0, hi = 0;
    bool const town = !::Playerbot::V2::World::VanillaZoneLevelRange(a.zone, lo, hi);
    uint32 const t = town ? cfg_.city_bots : cfg_.zone_bots;
    return t ? t : cfg_.bots_per_player;
}

uint32 WorldBotSpawner::CountAmbientNear(Anchor const& a) const
{
    uint32 n = 0;
    // In a capital only bots that stand in the capital count: the 600 y radius also reaches the countryside around
    // it (Elwynn for Stormwind), and bots out there must not fill the city's target.
    uint8 tlo = 0, thi = 0;
    bool const town = !::Playerbot::V2::World::VanillaZoneLevelRange(a.zone, tlo, thi);
    Services::Registry().for_each([&](BotId id, BotRegistryEntry const&)
    {
        ObjectGuid const g = ObjectGuid::Create<HighGuid::Player>(id);
        Player* p = ObjectAccessor::FindConnectedPlayer(g);
        if (!p || !p->IsInWorld() || p->GetMapId() != a.map) return;
        if (town && p->GetZoneId() != a.zone) return;
        if (!owner_.IsAmbientBot(id, p)) return;
        if (Dist2D(p->GetPositionX(), p->GetPositionY(), a.x, a.y) <= cfg_.activity_radius) ++n;
    });
    return n;
}

// A sleeper saved in a crowd (the roster was once placed on one flight master point per town) must not wake into it. It wakes at a
// quest hub of the same zone that suits its level, chosen anew each time, or failing that a few yards off the spot, on the
// ground. A bot with nobody around it stays exactly where it slept.
void WorldBotSpawner::StepOffStack(Player* bot)
{
    // A bot saved on a flight master node stands a couple of yards BELOW the ground (the node height is under the floor mesh): it
    // is off the navmesh, every path from there fails in place. Lift it onto the ground first, whatever the crowd.
    if (bot->GetMap() && !bot->IsInCombat() && bot->IsAlive())
    {
        float const gz = bot->GetMap()->GetHeight(bot->GetPhaseShift(), bot->GetPositionX(), bot->GetPositionY(),
                                                  bot->GetPositionZ() + 3.0f, true, 6.0f);
        if (gz > INVALID_HEIGHT && gz - bot->GetPositionZ() > 0.3f && gz - bot->GetPositionZ() <= 4.0f)
        {
            Playerbot::BotMovement::SafeNearTeleport(bot, bot->GetPositionX(), bot->GetPositionY(), gz + 0.3f, bot->GetOrientation());
            TC_LOG_INFO("playerbot.v2", "[world] {} woke {:.1f} y under the ground, lifted onto it", bot->GetName(), gz - bot->GetPositionZ());
        }
    }
    constexpr float kCrowdYards = 8.0f;
    uint32 near_bots = 0;
    Services::Registry().for_each([&](BotId id, BotRegistryEntry const&)
    {
        if (uint64(id) == bot->GetGUID().GetCounter()) return;
        Player* other = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(id));
        if (other && other->IsInWorld() && other->GetMapId() == bot->GetMapId() && other->GetDistance(bot) < kCrowdYards)
            ++near_bots;
    });
    if (near_bots < 2 || bot->IsInCombat() || !bot->IsAlive() || !bot->GetMap())
        return;
    uint64 const seed = bot->GetGUID().GetCounter() + uint64(n_woken_) * 7919u;
    ::Playerbot::V2::World::DestinationPick here;
    here.map_id = bot->GetMapId(); here.x = bot->GetPositionX(); here.y = bot->GetPositionY(); here.z = bot->GetPositionZ();
    here = ::Playerbot::V2::World::SpreadWithinZone(here, bot->GetLevel(), bot->GetTeam() == ALLIANCE, seed);
    if (std::fabs(here.x - bot->GetPositionX()) < 0.5f && std::fabs(here.y - bot->GetPositionY()) < 0.5f)
    {
        float dx = 0.f, dy = 0.f;
        ::Playerbot::V2::World::SpreadOffset(seed, 24.0f, dx, dy);
        float const x = bot->GetPositionX() + dx, y = bot->GetPositionY() + dy;
        float const z = bot->GetMap()->GetHeight(bot->GetPhaseShift(), x, y, bot->GetPositionZ() + 3.0f, true, 8.0f);
        if (z <= INVALID_HEIGHT || std::fabs(z - bot->GetPositionZ()) > 6.0f)
            return;   // not the same floor: stay where it slept
        if (bot->GetMap()->GetLiquidStatus(bot->GetPhaseShift(), x, y, z + 0.05f, {}) & (LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER))
            return;
        here.x = x; here.y = y; here.z = z + 0.5f;
    }
    Playerbot::BotMovement::SafeNearTeleport(bot, here.x, here.y, here.z, bot->GetOrientation());
}

bool WorldBotSpawner::PlaceNear(Player* bot, Player* anchor)
{
    Map* map = anchor->GetMap();
    if (!map) return false;

    // In a capital the districts are small and the player sees a few streets, not 110 y of open ground: use a tighter ring
    // so the crowd stays in the district the player stands in (the LOS preference below still hides the spawn itself).
    uint8 tlo = 0, thi = 0;
    bool const in_town = !::Playerbot::V2::World::VanillaZoneLevelRange(anchor->GetZoneId(), tlo, thi);
    float const ring_min = in_town ? std::min(cfg_.ring_min, 45.0f) : cfg_.ring_min;
    float const ring_max = in_town ? std::min(cfg_.ring_max, 110.0f) : cfg_.ring_max;

    // Spawn spots: quest hubs (and, in a capital, the hubs of the town) around the anchor, so the crowd stands where
    // bots actually live and then goes about its own business there. A ring spot around the player is only the
    // fallback; a crowd that is always rebuilt on a ring reads as bots following the player.
    struct Spot { float x, y, z, r, d; };
    Anchor self;   // the anchor as a plain record, to count the bots already around him
    self.guid = anchor->GetGUID(); self.map = anchor->GetMapId(); self.x = anchor->GetPositionX(); self.y = anchor->GetPositionY();
    self.zone = anchor->GetZoneId();
    bool const arriving = CountAmbientNear(self) < 20;
    std::vector<Spot> hubs, near_hubs;   // near_hubs: within ~150 y of the player (capitals favour them)
    {
        float const ax = anchor->GetPositionX(), ay = anchor->GetPositionY();
        float const min_d = in_town ? 35.0f : 60.0f;
        Services::Hubs().ForEach([&](::Playerbot::V2::Travel::QuestHub const& h)
        {
            if (h.mapId != anchor->GetMapId()) return;
            if (in_town && h.zoneId != anchor->GetZoneId()) return;   // a city's crowd stays in the city, not in the hubs of the countryside around it
            float const d = Dist2D(h.location.GetPositionX(), h.location.GetPositionY(), ax, ay);
            if (d >= min_d && d <= cfg_.activity_radius * 0.9f)
            {
                Spot const sp{ h.location.GetPositionX(), h.location.GetPositionY(), h.location.GetPositionZ(), std::min(h.radius, 40.0f), d };
                hubs.push_back(sp);
                if (d <= 150.0f) near_hubs.push_back(sp);
            }
        });
    }

    float bx = 0, by = 0, bz = 0;
    bool have = false, hidden = false;
    for (int attempt = 0; attempt < 12 && !hidden; ++attempt)
    {
        float x, y, dist;
        // Capital: 60% a quest-giver hub near the player (the busy spots), 25% a spot on a tight ring, 15% any hub of the
        // city, so the crowd is around him and not in the far districts. Elsewhere: any hub of the zone.
        uint32 const roll = urand(0, 99);
        bool const use_hub = !hubs.empty() && (!in_town || (roll < 60 && !near_hubs.empty()) || roll >= 85);
        if (use_hub)
        {
            std::vector<Spot> const& base = (in_town && roll < 60 && !near_hubs.empty()) ? near_hubs : hubs;
            // Arrival: while fewer than 20 bots stand around the player, the first ones go to the hubs closest to him, so
            // the nearest ones appear first; the rest spread as usual.
            std::vector<Spot> pool = base;
            if (arriving && pool.size() > 4)
            {
                std::partial_sort(pool.begin(), pool.begin() + 4, pool.end(), [](Spot const& l, Spot const& r) { return l.d < r.d; });
                pool.resize(4);
            }
            Spot const& h = pool[urand(0u, uint32(pool.size() - 1))];
            float const a = frand(0.0f, 2.0f * float(M_PI));
            float const rr = frand(0.0f, h.r);
            x = h.x + std::cos(a) * rr;
            y = h.y + std::sin(a) * rr;
            dist = Dist2D(x, y, anchor->GetPositionX(), anchor->GetPositionY());
        }
        else
        {
            float const ang = frand(0.0f, 2.0f * float(M_PI));
            dist = frand(ring_min, ring_max);
            x = anchor->GetPositionX() + std::cos(ang) * dist;
            y = anchor->GetPositionY() + std::sin(ang) * dist;
        }
        float const z = map->GetHeight(anchor->GetPhaseShift(), x, y, anchor->GetPositionZ() + 20.0f, true, 60.0f);
        if (z <= INVALID_HEIGHT)
            continue;
        // The ground height of a canal or pond is its floor: never put a bot on a spot that has water over it (even ankle-deep: the check is at the floor, not at head height).
        if (map->GetLiquidStatus(anchor->GetPhaseShift(), x, y, z + 0.05f, {}) & (LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER))
            continue;
        bx = x; by = y; bz = z;
        last_place_dist_ = dist;
        have = true;
        // Prefer a spot the anchor cannot see, so bots do not pop into view; keep the last candidate otherwise.
        hidden = !anchor->IsWithinLOS(x, y, z + 1.5f);
    }
    if (!have)
        return false;

    float const o = frand(0.0f, 2.0f * float(M_PI));
    if (bot->GetMapId() == anchor->GetMapId())
        return ::Playerbot::BotMovement::SafeNearTeleport(bot, bx, by, bz, o);
    return ::Playerbot::BotMovement::SafeTeleport(bot, anchor->GetMapId(), bx, by, bz, o, /*options*/ 0);
}

void WorldBotSpawner::DrivePending(uint32 now_ms, std::vector<Anchor> const&)
{
    for (auto it = pending_.begin(); it != pending_.end();)
    {
        uint64 const guid_low = it->first;
        Pending& pd = it->second;
        ObjectGuid const g = ObjectGuid::Create<HighGuid::Player>(guid_low);
        Player* bot = ObjectAccessor::FindConnectedPlayer(g);

        bool drop = getMSTimeDiff(pd.queued_ms, now_ms) >= kPendingTimeoutMs;
        if (pd.global)
        {
            // World-wide filler: nothing to place, the setup pipeline already sends it to a leveling town.
            if (pd.wake && !pd.unstacked && !drop && bot && bot->IsInWorld() && !bot->IsBeingTeleported() && owner_.SetupDone(guid_low))
            {
                StepOffStack(bot);
                pd.unstacked = true;
            }
            if (drop || (bot && owner_.SetupDone(guid_low) && (!pd.wake || pd.unstacked)))
            {
                if (!drop) ++n_global_settled_;
                it = pending_.erase(it);
            }
            else
                ++it;
            continue;
        }
        if (!drop && bot && bot->IsInWorld() && !bot->IsBeingTeleported() && owner_.SetupDone(guid_low))
        {
            // A bot that already queued for (or entered) a battleground, or is in an instance or a group, is not ours
            // to move any more: leave it alone, the BG / group logic owns it.
            if (bot->InBattleground() || bot->InBattlegroundQueue() || bot->GetGroup()
                || (bot->GetMap() && (bot->GetMap()->IsDungeon() || bot->GetMap()->IsBattlegroundOrArena())))
            {
                TC_LOG_INFO("playerbot.v2", "[world] abandon {} ({}): bg={} queued={} group={} instance={}", bot->GetName(), guid_low,
                            bot->InBattleground(), bot->InBattlegroundQueue(), bot->GetGroup() != nullptr,
                            bot->GetMap() && bot->GetMap()->IsDungeon());
                it = pending_.erase(it);
                ++n_abandoned_;
                continue;
            }
            Player* anchor = ObjectAccessor::FindConnectedPlayer(pd.anchor);
            if (!anchor || !anchor->IsInWorld() || !anchor->GetMap() || anchor->GetMap()->IsDungeon()
                || anchor->GetMap()->IsBattlegroundOrArena())
            {
                // The player left; the despawn pass will log the bot out once its grace is up.
                TC_LOG_INFO("playerbot.v2", "[world] abandon {} ({}): anchor left the open world", bot->GetName(), guid_low);
                placed_[guid_low] = Placed{ now_ms };
                it = pending_.erase(it);
                ++n_abandoned_;
                continue;
            }
            if (!bot->IsInCombat() && bot->IsAlive() && PlaceNear(bot, anchor))
            {
                TC_LOG_INFO("playerbot.v2", "[world] placed {} ({}) L{} zone {} at ({:.0f},{:.0f},{:.0f}), {:.0f} y from anchor, after {} ms",
                            bot->GetName(), guid_low, bot->GetLevel(), bot->GetZoneId(), bot->GetPositionX(), bot->GetPositionY(),
                            bot->GetPositionZ(), last_place_dist_, getMSTimeDiff(pd.queued_ms, now_ms));
                placed_[guid_low] = Placed{ now_ms };
                ++n_placed_;
                // A town (capital: no zone level range): most bots stay there and do town things, so it looks busy.
                uint8 tlo = 0, thi = 0;
                if (!::Playerbot::V2::World::VanillaZoneLevelRange(anchor->GetZoneId(), tlo, thi) && urand(0, 99) < cfg_.town_pct)
                    if (BotAI* ai = Services::Registry().ai(guid_low))
                        ai->force_activity_mode(BotAI::ActivityMode::Professioning, GameTime::GetGameTimeMS() + urand(45u, 90u) * 60u * 1000u);
                it = pending_.erase(it);
                continue;
            }
            // In combat / dead / no ground found: retry next tick until the timeout.
        }
        if (drop)
        {
            TC_LOG_INFO("playerbot.v2", "[world] abandon {}: login or setup did not finish in time (online={} setup_done={})", guid_low,
                        bot != nullptr, owner_.SetupDone(guid_low));
            placed_[guid_low] = Placed{ now_ms };   // despawn pass cleans it up if it did come online
            it = pending_.erase(it);
            ++n_abandoned_;
            continue;
        }
        ++it;
    }
}

void WorldBotSpawner::DriveDespawn(uint32 now_ms, std::vector<Anchor> const& anchors)
{
    // Forget placement records of bots that are gone.
    for (auto it = placed_.begin(); it != placed_.end();)
    {
        if (!ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(it->first)))
            it = placed_.erase(it);
        else
            ++it;
    }

    struct Victim { ObjectGuid guid; float score; };   // score: larger = kicked first
    std::vector<Victim> victims;
    uint32 ambient = 0;
    uint32 far_bots = 0;
    uint32 all_bots = 0;
    Services::Registry().for_each([&](BotId id, BotRegistryEntry const&)
    {
        ObjectGuid const g = ObjectGuid::Create<HighGuid::Player>(id);
        Player* p = ObjectAccessor::FindConnectedPlayer(g);
        if (!p || !p->IsInWorld()) return;
        ++all_bots;
        if (!owner_.IsAmbientBot(id, p)) return;
        // Bots queued for / inside a battleground, in a group or in an instance are not part of the open-world population.
        if (p->InBattleground() || p->InBattlegroundQueue() || p->GetGroup()
            || (p->GetMap() && (p->GetMap()->IsDungeon() || p->GetMap()->IsBattlegroundOrArena())))
            return;
        ++ambient;
        if (pending_.contains(uint64(id)) || seeding_.contains(uint64(id))) return;

        // Distance to the nearest anchor on the bot's own map.
        float nearest = 1e9f;
        for (Anchor const& a : anchors)
            if (a.map == p->GetMapId())
                nearest = std::min(nearest, Dist2D(p->GetPositionX(), p->GetPositionY(), a.x, a.y));
        if (nearest > cfg_.activity_radius) ++far_bots;
        bool const orphan = nearest > cfg_.despawn_radius;
        if (!orphan)
            return;

        // Not while it is doing something that logging out would break.
        if (p->IsInCombat() || p->GetGroup() || p->IsBeingTeleported()) return;
        if (Map* m = p->GetMap(); m && (m->IsDungeon() || m->IsBattlegroundOrArena())) return;
        if (p->InBattlegroundQueue() || p->InBattleground()) return;
        if (owner_.IsKickProtected(id, now_ms)) return;
        if (auto pit = placed_.find(uint64(id)); pit != placed_.end()
            && getMSTimeDiff(pit->second.placed_ms, now_ms) < cfg_.placed_grace_ms)
            return;
        // Bots of the world-wide filler are only recycled once their setup is done (not mid-pipeline).
        if (cfg_.global_fill && !owner_.SetupDone(uint64(id))) return;
        victims.push_back({ g, nearest });
    });
    ambient_online_ = ambient;
    global_online_ = far_bots;
    all_online_ = all_bots;

    // With the world-wide filler a far bot is kept while there is budget; it is only recycled (farthest first) to
    // make room for the bubble around a real player that still has a deficit.
    uint32 limit = cfg_.despawn_per_tick;
    if (cfg_.global_fill && !anchors.empty())
    {
        uint32 deficit = 0;
        for (Anchor const& a : anchors)
        {
            uint32 pend = 0;
            for (auto const& [gl, pd] : pending_)
                if (pd.anchor == a.guid) ++pend;
            uint32 const have = CountAmbientNear(a) + pend;
            uint32 const tgt = TargetFor(a);
            if (have < tgt) deficit += tgt - have;
        }
        uint32 const used = ambient + uint32(pending_.size());
        uint32 const free_budget = cfg_.max_bots > used ? cfg_.max_bots - used : 0;
        limit = deficit > free_budget ? std::min(limit, deficit - free_budget) : 0;
    }

    // Over the global cap with every bot still near someone: nothing to do here, DriveSpawn simply stops adding.
    std::sort(victims.begin(), victims.end(), [](Victim const& a, Victim const& b) { return a.score > b.score; });
    uint32 n = 0;
    for (Victim const& v : victims)
    {
        if (n >= limit) break;
        if (Services::SessionMgr().LogoutBot(v.guid))
        {
            ++n;
            ++n_despawned_;
            placed_.erase(v.guid.GetCounter());
        }
    }
}

void WorldBotSpawner::WindowFor(Anchor const& a, uint8& lo, uint8& hi) const
{
    // Zephras Isle: its own levels, below MinLevel. The island's bots are Skyborne questers; they are not lifted to 10, which
    // is where a bot leaves the island (DriveGraduation).
    if (Skyborne::IsOnIsle(a.map))
    {
        Skyborne::Window const w = Skyborne::IsleWindow();
        lo = w.lo;
        hi = w.hi;
        return;
    }
    if (!::Playerbot::V2::World::VanillaZoneLevelRange(a.zone, lo, hi))
    {
        // Capitals and other towns: a town is full of every level, so spread over the whole range.
        lo = uint8(cfg_.min_level);
        hi = uint8(cfg_.max_level);
    }
    else
    {
        lo = uint8(std::max<int>(1, int(lo) - 2));
        hi = uint8(std::min<int>(60, int(hi) + 2));
    }
    lo = uint8(std::clamp<uint32>(lo, cfg_.min_level, cfg_.max_level));
    hi = uint8(std::clamp<uint32>(hi, lo, cfg_.max_level));
}

void WorldBotSpawner::DriveSpawn(uint32 now_ms, std::vector<Anchor> const& anchors)
{
    if (anchors.empty() || query_inflight_)
        return;
    if (pool_count_ < 0)
        return;   // the pool size is not known yet
    if (ambient_online_ + pending_.size() >= cfg_.max_bots)
        return;
    if (cfg_.rss_budget_mb && rss_kb_ / 1024 > cfg_.rss_budget_mb * 115 / 100)
        return;   // far over the budget: even the bubble waits

    // Pick the next anchor, round robin, that still has a deficit.
    for (size_t k = 0; k < anchors.size(); ++k)
    {
        Anchor const& a = anchors[(next_anchor_ + k) % anchors.size()];
        uint32 pending_for_anchor = 0;
        for (auto const& [guid, pd] : pending_)
            if (pd.anchor == a.guid) ++pending_for_anchor;
        uint32 const have = CountAmbientNear(a) + pending_for_anchor;
        uint32 const target = TargetFor(a);
        if (have >= target)
            continue;

        next_anchor_ = (next_anchor_ + k + 1) % anchors.size();

        uint32 want = std::min({ target - have, SpawnRateFor(a),
                                 cfg_.max_bots - uint32(ambient_online_ + pending_.size()) });
        if (!want) return;

        // Persistent residents first: pool bots that sleep (offline, saved) around this player wake up where they stand. A
        // pass that finds nobody asleep pauses for 15 s and the ring path below places new residents instead; they stay
        // wherever they are when logged out, so the next visit finds them.
        if (cfg_.wake_radius > 0.0f)
        {
            auto const dry = wake_dry_until_.find(a.guid.GetCounter());
            if (dry == wake_dry_until_.end() || int32(dry->second - now_ms) <= 0)
            {
                DriveWake(now_ms, a, want);
                return;
            }
        }

        // Level range that suits the anchor's zone; capitals and the like have none, so spread around the player.
        uint8 lo = 0, hi = 0;
        WindowFor(a, lo, hi);

        std::vector<bool>  slots;        // true = alliance
        std::vector<uint8> slot_levels;
        for (uint32 i = 0; i < want; ++i)
        {
            slots.push_back(BotIsAlliance(a.alliance, urand(0, 99), FriendlyPctFor(cfg_.mix, a.zone)));
            slot_levels.push_back(uint8(urand(lo, hi)));
        }

        query_inflight_ = true;
        query_started_ms_ = now_ms;
        queries_.AddCallback(CharacterDatabase.AsyncQuery(Trinity::StringFormat(
            "SELECT pv.character_guid_low, c.level, c.race, c.map, c.position_x, c.position_y, c.position_z FROM playerbot_v2_character pv "
            "JOIN characters c ON c.guid = pv.character_guid_low "
            "WHERE c.level BETWEEN {} AND {} AND c.online = 0 AND pv.distribution_level > 0 {}"
            "ORDER BY pv.last_seen_at ASC LIMIT {}",
            uint32(lo), uint32(hi), SkyborneRaceClause(Skyborne::IsOnIsle(a.map)), std::max<uint32>(want * 20u, 100u)).c_str())
            .WithCallback([this, a, slots, slot_levels](QueryResult res)
        {
            query_inflight_ = false;
            FinishSpawn(a, slots, slot_levels, std::move(res));
        }));
        return;
    }

    DriveGlobalSpawn(now_ms, anchors);
}

void WorldBotSpawner::DriveGraduation(uint32 now_ms)
{
    // Every few seconds, at most two bots per pass: a bot reaching level 10 on the isle is moved the way a new level-10 bot is
    // placed, once its own setup is done (the pipeline may still be running its steps).
    constexpr uint32 kGraduationPassMs = 5000u;
    constexpr uint32 kGraduationPerPass = 2u;
    if (last_grad_ms_ && getMSTimeDiff(last_grad_ms_, now_ms) < kGraduationPassMs)
        return;
    last_grad_ms_ = now_ms;
    uint32 moved = 0;
    Services::Registry().for_each([&](BotId id, BotRegistryEntry const&)
    {
        if (moved >= kGraduationPerPass) return;
        Player* p = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(id));
        if (!p || !p->IsInWorld() || !p->IsAlive() || !p->GetMap() || !Skyborne::IsSkyborneRace(p->GetRace())) return;
        bool const owned = !owner_.IsAmbientBot(id, p);   // a player's companion or alt is never ours to move
        bool const in_transit = p->IsBeingTeleported() || p->IsInFlight() || p->GetTransport() != nullptr;
        if (!Skyborne::ShouldGraduate(p->GetMapId(), p->GetLevel(), owned, p->GetGroup() != nullptr, p->IsInCombat(), in_transit)) return;
        if (!owner_.SetupDone(uint64(id)) || pending_.contains(uint64(id)) || seeding_.contains(uint64(id))) return;
        if (!owner_.GraduateSkyborne(p)) return;
        ++moved;
        ++n_graduated_;
        placed_[uint64(id)] = Placed{ now_ms };   // freshly placed: the despawn pass leaves it alone for the grace
        TC_LOG_INFO("playerbot.v2", "[world] skyborne graduate {} ({}) L{}: left Zephras Isle for map {} zone {}", p->GetName(), uint64(id),
                    uint32(p->GetLevel()), p->GetMapId(), p->GetZoneId());
    });
}

void WorldBotSpawner::DriveSeed(uint32 now_ms)
{
    // Finish what is in flight first: once a seeded bot's setup is done and it has stood still for a few seconds at its
    // zone hub (the pipeline's own teleport), log it out. Its position is saved, so it sleeps right there.
    constexpr uint32 kSeedSettleMs = 8000u, kSeedTimeoutMs = 10u * 60u * 1000u;
    for (auto it = seeding_.begin(); it != seeding_.end();)
    {
        ObjectGuid const g = ObjectGuid::Create<HighGuid::Player>(it->first);
        Player* bot = ObjectAccessor::FindConnectedPlayer(g);
        Seeding& sd = it->second;
        bool const timed_out = getMSTimeDiff(sd.started_ms, now_ms) >= kSeedTimeoutMs;
        if (!bot || timed_out)
        {
            if (bot)
                Services::SessionMgr().LogoutBot(g);   // never finished setup: do not keep it awake forever
            it = seeding_.erase(it);
            continue;
        }
        if (bot->IsInWorld() && !bot->IsBeingTeleported() && !bot->IsInCombat() && !bot->GetGroup() && owner_.SetupDone(it->first))
        {
            if (!sd.setup_done_ms)
                sd.setup_done_ms = now_ms ? now_ms : 1u;
            else if (getMSTimeDiff(sd.setup_done_ms, now_ms) >= kSeedSettleMs)
            {
                if (Services::SessionMgr().LogoutBot(g))
                {
                    ++n_seeded_;
                    it = seeding_.erase(it);
                    continue;
                }
            }
        }
        ++it;
    }

    if (!cfg_.seed_roster || pool_count_ < 0 || rss_capped_ || pool_count_ >= int64(cfg_.pool_max))
        return;
    if (seeding_.size() >= cfg_.seed_in_flight || ambient_online_ + pending_.size() + seeding_.size() >= cfg_.max_bots)
        return;
    // One new resident per tick: the setup pipeline (gear, talents, spells, teleport) is the cost, not the creation.
    // The census decides which faction and level bracket the roster lacks most; until it is read the seeder waits.
    if (!pool_counts_ms_)
        return;
    SeedChoice const want = PickSeed(pool_counts_, cfg_.pool_horde_pct, double(urand(0, 999999)) / 1000000.0);
    bool const alliance = want.deficit > 0 ? want.alliance : urand(0, 99) >= cfg_.pool_horde_pct;
    // A small share of the seeds are Skyborne residents of Zephras Isle: level 1-9, asleep on the island (their pipeline leaves
    // them at the start), so a player who arrives there finds the island lived in. The rest are the usual leveling-town bots.
    bool const isle = urand(0, 99) < cfg_.skyborne_seed_pct;
    uint8 level;
    if (isle)
        level = uint8(urand(Skyborne::kIsleLevelLo, Skyborne::kIsleLevelHi));
    else if (want.deficit > 0)
        level = uint8(urand(std::max(cfg_.min_level, PoolBracketLo(want.bracket)), std::max(cfg_.min_level, std::min(cfg_.max_level, PoolBracketHi(want.bracket)))));
    else
        level = uint8(urand(cfg_.min_level, cfg_.max_level));
    if (uint64 const guid = owner_.SpawnAmbientBot(level, alliance, isle ? Skyborne::RaceForTeam(alliance) : uint8(0)))
    {
        seeding_[guid] = Seeding{ now_ms ? now_ms : 1u, 0 };
        ++pool_count_;
        if (!isle)
            PoolAdd(pool_counts_, alliance, level);
    }
    else
        ++n_spawn_failed_;
}

void WorldBotSpawner::DriveWake(uint32 now_ms, Anchor const& a, uint32 want)
{
    uint8 lo = 0, hi = 0;
    WindowFor(a, lo, hi);
    float const r = cfg_.wake_radius;
    uint8 tlo = 0, thi = 0;
    bool const town = !::Playerbot::V2::World::VanillaZoneLevelRange(a.zone, tlo, thi);
    query_inflight_ = true;
    query_started_ms_ = now_ms;
    queries_.AddCallback(CharacterDatabase.AsyncQuery(Trinity::StringFormat(
        "SELECT pv.character_guid_low, c.level, c.race, c.map, c.position_x, c.position_y, c.zone, c.position_z FROM playerbot_v2_character pv "
        "JOIN characters c ON c.guid = pv.character_guid_low "
        "WHERE c.online = 0 AND pv.distribution_level > 0 AND c.map = {} AND c.level BETWEEN {} AND {} "
        "AND c.position_x BETWEEN {:.1f} AND {:.1f} AND c.position_y BETWEEN {:.1f} AND {:.1f} {}{}"
        "ORDER BY RAND() LIMIT {}",
        a.map, uint32(lo), uint32(hi), a.x - r, a.x + r, a.y - r, a.y + r,
        SkyborneRaceClause(Skyborne::IsOnIsle(a.map)),
        town ? Trinity::StringFormat("AND c.zone = {} ", a.zone) : std::string(),   // a capital wakes its own zone only (WakeSelect.h)
        std::max<uint32>(want * 6u, 30u)).c_str())
        .WithCallback([this, a, want, town](QueryResult res)
    {
        query_inflight_ = false;
        uint32 woken = 0;
        uint32 const now = now_ms_;
        if (res)
        {
            // Rule in Fleet/WakeSelect.h (tested): the distance window, the faction roll and, for a capital, the capital's own zone.
            WakeRule rule;
            rule.min_dist = 60.0f;
            rule.max_dist = cfg_.wake_radius;
            rule.capital = town;
            rule.anchor_zone = a.zone;
            std::vector<WakeCandidate> cands;
            do
            {
                Field* f = res->Fetch();
                uint64 const guid = f[0].GetUInt64();
                if (pending_.contains(guid) || seeding_.contains(guid)) continue;
                if (Services::Owners().GetOwner(guid).account_id != 0) continue;   // a companion is never a resident
                if (ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(guid))) continue;
                WakeCandidate c;
                c.guid = guid;
                c.dist = Dist2D(f[4].GetFloat(), f[5].GetFloat(), a.x, a.y);
                c.zone = f[6].GetUInt32();
                if (Player* ap = ObjectAccessor::FindConnectedPlayer(a.guid); ap && ap->GetMap())
                    c.wet = (ap->GetMap()->GetLiquidStatus(ap->GetPhaseShift(), f[4].GetFloat(), f[5].GetFloat(), f[7].GetFloat() + 0.05f, {})
                             & (LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER)) != 0;
                c.faction_ok = IsAllianceRace(f[2].GetUInt8()) == BotIsAlliance(a.alliance, urand(0, 99), FriendlyPctFor(cfg_.mix, a.zone));
                cands.push_back(c);
            } while (res->NextRow());
            // Never more than the target still lacks, and never past the global cap.
            uint32 const room = cfg_.max_bots > uint32(ambient_online_ + pending_.size())
                ? cfg_.max_bots - uint32(ambient_online_ + pending_.size()) : 0u;
            for (uint64 const guid : SelectWake(cands, rule, std::min(want, room)))
            {
                auto lr = Services::SessionMgr().LoginBot(ObjectGuid::Create<HighGuid::Player>(guid));
                if (!lr.ok) { ++n_login_failed_; continue; }
                pending_[guid] = Pending{ a.guid, now, /*global*/ true, /*wake*/ true };   // already standing at home: no placement
                ++n_logged_in_;
                ++n_woken_;
                ++woken;
            }
        }
        if (!woken)
        {
            ++n_wake_dry_;
            wake_dry_until_[a.guid.GetCounter()] = now + 15000u;
        }
    }));
}

void WorldBotSpawner::DriveGlobalSpawn(uint32 now_ms, std::vector<Anchor> const& anchors)
{
    if (!cfg_.global_fill || anchors.empty() || query_inflight_ || rss_capped_)
        return;
    // Keep room for the bubble around every player: the filler only uses the rest of the budget.
    uint32 const reserve = std::min<uint32>(uint32(anchors.size()) * cfg_.bots_per_player, cfg_.max_bots / 2);
    uint32 const cap = cfg_.max_bots - reserve;
    uint32 const used = ambient_online_ + uint32(pending_.size());
    if (used >= cap)
        return;
    uint32 const want = std::min(cfg_.global_per_tick, cap - used);

    Anchor g = anchors.front();   // faction reference for FriendlyPct only
    g.guid = ObjectGuid::Empty;
    std::vector<bool>  slots;
    std::vector<uint8> slot_levels;
    for (uint32 i = 0; i < want; ++i)
    {
        slots.push_back(urand(0, 99) < cfg_.friendly_pct ? g.alliance : !g.alliance);
        slot_levels.push_back(uint8(urand(cfg_.min_level, cfg_.max_level)));
    }
    query_inflight_ = true;
    query_started_ms_ = now_ms;
    queries_.AddCallback(CharacterDatabase.AsyncQuery(Trinity::StringFormat(
        "SELECT pv.character_guid_low, c.level, c.race, c.map, c.position_x, c.position_y, c.position_z FROM playerbot_v2_character pv "
        "JOIN characters c ON c.guid = pv.character_guid_low "
        "WHERE c.level BETWEEN {} AND {} AND c.online = 0 AND pv.distribution_level > 0 {}"
        "ORDER BY pv.last_seen_at ASC LIMIT {}",
        cfg_.min_level, cfg_.max_level, SkyborneRaceClause(false), std::max<uint32>(want * 40u, 200u)).c_str())
        .WithCallback([this, g, slots, slot_levels](QueryResult res)
    {
        query_inflight_ = false;
        FinishSpawn(g, slots, slot_levels, std::move(res), /*global*/ true);
    }));
}

void WorldBotSpawner::FinishSpawn(Anchor const& a, std::vector<bool> const& slots, std::vector<uint8> const& slot_levels,
                                  QueryResult res, bool global)
{
    struct Row { uint64 guid; bool alliance; bool used; bool in_hub; };
    std::vector<Row> rows;
    if (res)
    {
        do
        {
            Field* f = res->Fetch();
            uint64 const guid = f[0].GetUInt64();
            if (pending_.contains(guid)) continue;
            // A player's companion (owner-bound, e.g. dismissed with `.bot dismiss`) is never an ambient pool bot: logging it
            // back in would bring it back following its owner.
            if (Services::Owners().GetOwner(guid).account_id != 0) continue;
            if (ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(guid))) continue;
            // World-wide filler: only pool bots that were saved inside a quest hub / town. Memory grows with the number of
            // distinct places that have a Player in them (grids, mmap tiles), so the filler is kept on shared hubs.
            // Hub rows are preferred; rows saved elsewhere are the fallback (stage B, 2026-10-07: a hub-only filter left
            // every slot without a match, the pool was full, and nothing could be created, so growth stopped at 235).
            bool in_hub = true;
            if (global && cfg_.filler_hub_only)
            {
                Position const pos(f[4].GetFloat(), f[5].GetFloat(), f[6].GetFloat());
                ::Playerbot::V2::Travel::QuestHub const* hub = Services::Hubs().GetQuestHubAtPosition(pos);
                in_hub = hub && hub->mapId == f[3].GetUInt32();
            }
            rows.push_back({ guid, IsAllianceRace(f[2].GetUInt8()), false, in_hub });
        } while (res->NextRow());
    }

    // Same clock as OnWorldTick (module uptime), NOT getMSTime(): mixing them made every pending bot look 120 s old.
    uint32 const now = now_ms_;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (ambient_online_ + pending_.size() >= cfg_.max_bots)
            break;
        bool const alliance = slots[i];
        bool done = false;
        for (int pass = 0; pass < 2 && !done; ++pass)    // pass 0: rows saved in a hub, pass 1: any row
            for (Row& r : rows)
            {
                if (r.used || r.alliance != alliance || (pass == 0 && !r.in_hub)) continue;
                r.used = true;
                auto lr = Services::SessionMgr().LoginBot(ObjectGuid::Create<HighGuid::Player>(r.guid));
                if (lr.ok)
                {
                    pending_[r.guid] = Pending{ a.guid, now, global };
                    ++n_logged_in_;
                    done = true;
                    break;
                }
                ++n_login_failed_;
            }
        if (done) continue;

        // No pool bot of that faction at that level: create one (bounded by PoolMax).
        ++n_no_match_;
        if (pool_count_ >= int64(cfg_.pool_max))
        {
            ++n_pool_full_;
            continue;
        }
        // Around a Zephras anchor every new resident is Skyborne (the other races cannot leave their start for the isle's quests).
        uint8 const race = !global && Skyborne::IsOnIsle(a.map) ? Skyborne::RaceForTeam(alliance) : uint8(0);
        if (uint64 const guid = owner_.SpawnAmbientBot(slot_levels[i], alliance, race))
        {
            pending_[guid] = Pending{ a.guid, now, global };
            ++pool_count_;
            ++n_created_;
            PoolAdd(pool_counts_, alliance, slot_levels[i]);
        }
        else
            ++n_spawn_failed_;
    }

    // One line per 30 s while a pass came up short, so a silent "nothing happens" is never a mystery.
    if (!global && n_no_match_ + n_login_failed_ && getMSTimeDiff(last_why_ms_, now) >= 30000u)
    {
        last_why_ms_ = now;
        TC_LOG_INFO("playerbot.v2",
            "[world] spawn pass around anchor (zone {}, L{}): wanted {} slot(s), {} offline pool row(s) fetched; "
            "totals no_match={} pool_full={} (pool {}/{}) spawn_failed={} login_failed={}",
            a.zone, uint32(a.level), slots.size(), rows.size(), n_no_match_, n_pool_full_, pool_count_,
            cfg_.pool_max, n_spawn_failed_, n_login_failed_);
    }
}

void WorldBotSpawner::OnWorldTick(uint32 now_ms)
{
    // Config reload also covers "enabled at runtime"; while disabled this costs one config read per 15 s.
    if (!last_cfg_ms_ || getMSTimeDiff(last_cfg_ms_, now_ms) >= kConfigReloadMs)
    {
        last_cfg_ms_ = now_ms;
        LoadConfig();
    }
    // The [mem] line runs whatever the population mode: the 2026-10-11 leak hunt had World bots off and saw nothing.
    if (!last_mem_ms_ || getMSTimeDiff(last_mem_ms_, now_ms) >= kMemReportMs)
    {
        last_mem_ms_ = now_ms;
        TC_LOG_INFO("playerbot.v2", "{}", MemReport(all_online_));
    }
    if (!cfg_.enable)
    {
        WorldLeash::Clear();
        return;
    }
    if (uint32(sConfigMgr->GetIntDefault("Playerbot.Population.TotalTarget", 0)) > 0)
    {
        if (!warned_exclusive_)
        {
            TC_LOG_WARN("playerbot.v2", "[World] Playerbot.World.Enable is set but Playerbot.Population.TotalTarget > 0: "
                        "the two population modes are exclusive, world spawner stays off.");
            warned_exclusive_ = true;
        }
        return;
    }

    queries_.ProcessReadyCallbacks();
    if (query_inflight_ && getMSTimeDiff(query_started_ms_, now_ms) >= kQueryTimeoutMs)
        query_inflight_ = false;   // a QueryCallback always fires; this only guards a lost one

    if (last_tick_ms_ && getMSTimeDiff(last_tick_ms_, now_ms) < cfg_.tick_ms)
        return;
    last_tick_ms_ = now_ms;
    now_ms_ = now_ms;
    if (!rss_read_ms_ || getMSTimeDiff(rss_read_ms_, now_ms) >= 5000u)
    {
        rss_read_ms_ = now_ms;
        rss_kb_ = ProcessRssKb();
        if (uint64 const cpu = ProcessCpuMs())
        {
            if (cpu_prev_at_ && now_ms > cpu_prev_at_)
                cpu_pct_ = 100.0 * double(cpu - cpu_prev_ms_) / double(now_ms - cpu_prev_at_);
            cpu_prev_ms_ = cpu;
            cpu_prev_at_ = now_ms;
        }
        bool const was = rss_capped_;
        rss_capped_ = cfg_.rss_budget_mb && rss_kb_ / 1024 > cfg_.rss_budget_mb;
        if (rss_capped_ != was)
            TC_LOG_INFO("playerbot.v2", "[world] RSS {} MB {} the budget of {} MB: {}", rss_kb_ / 1024, rss_capped_ ? "is over" : "is back under",
                        cfg_.rss_budget_mb, rss_capped_ ? "no new filler bots" : "filler resumes");
    }
    uint32 const t0 = getMSTime();

    if (pool_count_ < 0 && !pool_count_inflight_)
    {
        pool_count_inflight_ = true;
        queries_.AddCallback(CharacterDatabase.AsyncQuery(
            "SELECT COUNT(*) FROM playerbot_v2_character WHERE distribution_level > 0 "
            "AND (jit_for_queue IS NULL OR jit_for_queue = '')")
            .WithCallback([this](QueryResult res)
        {
            pool_count_ = res ? int64((*res)[0].GetUInt64()) : 0;
            pool_count_inflight_ = false;
        }));
    }

    // The roster by faction and level bracket, read every two minutes (creations in between are added as they happen): the seeder
    // creates whatever is furthest below the target ratio (PoolBalance.h).
    if (cfg_.seed_roster && !pool_counts_inflight_ && (!pool_counts_ms_ || getMSTimeDiff(pool_counts_ms_, now_ms) >= 120000u))
    {
        pool_counts_inflight_ = true;
        queries_.AddCallback(CharacterDatabase.AsyncQuery(
            "SELECT c.race, c.level, COUNT(*) FROM playerbot_v2_character pv JOIN characters c ON c.guid = pv.character_guid_low "
            "WHERE pv.distribution_level > 0 AND (pv.jit_for_queue IS NULL OR pv.jit_for_queue = '') AND c.level >= 10 "
            "GROUP BY c.race, c.level")
            .WithCallback([this, now_ms](QueryResult res)
        {
            PoolCounts counts;
            if (res)
                do
                {
                    Field* f = res->Fetch();
                    ((IsAllianceRace(f[0].GetUInt8()) ? counts.alliance : counts.horde))[PoolBracketOf(f[1].GetUInt8())] += uint32(f[2].GetUInt64());
                } while (res->NextRow());
            pool_counts_ = counts;
            pool_counts_ms_ = now_ms ? now_ms : 1u;
            pool_counts_inflight_ = false;
            uint32 a = 0, h = 0;
            for (std::size_t b = 0; b < kPoolBrackets; ++b) { a += counts.alliance[b]; h += counts.horde[b]; }
            TC_LOG_INFO("playerbot.v2", "[world] roster census: Alliance {} Horde {} (target Horde {}%); per bracket A/H: {}/{} {}/{} {}/{} {}/{} {}/{}",
                a, h, cfg_.pool_horde_pct, counts.alliance[0], counts.horde[0], counts.alliance[1], counts.horde[1], counts.alliance[2],
                counts.horde[2], counts.alliance[3], counts.horde[3], counts.alliance[4], counts.horde[4]);
        }));
    }

    std::vector<Anchor> anchors;
    CollectAnchors(anchors);
    anchors_ = uint32(anchors.size());

    // A player who went into an instance or jumped far (hearth, flight end) leaves a ghost anchor for ZoneGraceSec, so the
    // bots around the spot he left are not logged out and rebuilt on the way back.
    {
        std::unordered_set<uint64> seen;
        for (Anchor const& a : anchors)
        {
            uint64 const k = a.guid.GetCounter();
            seen.insert(k);
            auto it = prev_anchor_.find(k);
            if (it != prev_anchor_.end() && cfg_.zone_grace_ms
                && (it->second.map != a.map || Dist2D(it->second.x, it->second.y, a.x, a.y) > cfg_.despawn_radius))
                ghosts_.push_back({ it->second, now_ms + cfg_.zone_grace_ms });
            prev_anchor_[k] = a;
        }
        for (auto it = prev_anchor_.begin(); it != prev_anchor_.end();)
        {
            if (seen.count(it->first)) { ++it; continue; }
            // Gone from the open world (instance, battleground, logout): keep his bots for the grace.
            if (cfg_.zone_grace_ms)
                ghosts_.push_back({ it->second, now_ms + cfg_.zone_grace_ms });
            it = prev_anchor_.erase(it);
        }
        ghosts_.erase(std::remove_if(ghosts_.begin(), ghosts_.end(),
            [&](Ghost const& g) { return int32(g.until_ms - now_ms) <= 0; }),
            ghosts_.end());
    }
    std::vector<Anchor> keep_anchors = anchors;   // despawn looks at ghosts too; spawning and placing do not
    for (Ghost const& g : ghosts_)
        keep_anchors.push_back(g.a);

    {
        std::vector<WorldLeash::Anchor> pub;
        pub.reserve(keep_anchors.size());
        for (Anchor const& a : keep_anchors)
            pub.push_back({ a.map, a.x, a.y });
        // Objectives farther than 85% of the despawn radius from every anchor are not pursued (0 = no anchors, no leash).
        WorldLeash::Publish(std::move(pub), cfg_.despawn_radius * 0.85f);
    }

    DrivePending(now_ms, anchors);
    DriveDespawn(now_ms, keep_anchors);
    DriveSpawn(now_ms, anchors);
    DriveSeed(now_ms);
    DriveGraduation(now_ms);

    last_tick_cost_ms_ = getMSTimeDiff(t0, getMSTime());
    max_tick_cost_ms_ = std::max(max_tick_cost_ms_, last_tick_cost_ms_);
    tick_cost_sum_ms_ += last_tick_cost_ms_;
    ++tick_count_;
    // Baseline for the per-bot RAM figure: the last reading taken while no ambient bot was online.
    if (ambient_online_ == 0 && !baseline_rss_kb_)
        baseline_rss_kb_ = ProcessRssKb();   // first reading only: later ones include memory the process keeps after logouts

    if (!last_report_ms_ || getMSTimeDiff(last_report_ms_, now_ms) >= kReportMs)
    {
        last_report_ms_ = now_ms;
        TC_LOG_INFO("playerbot.v2", "{}", Report());
    }
}

} // namespace Playerbot::V2::Fleet
