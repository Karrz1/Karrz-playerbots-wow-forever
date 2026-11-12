#include "BotPool.h"
#include "BotIdentityRegistry.h"
#include "BotAccountMgr.h"
#include "BotPopulationManager.h"
#include "OwnerRegistry.h"
#include "../Services.h"
#include "../Bot/BotAI.h"
#include "../Bot/BotRegistry.h"
#include "Map.h"
#include "Player.h"
#include "WorldSession.h"

#include <mutex>
#include <unordered_set>

namespace Playerbot::Pool {

namespace {
std::mutex g_scenario_mu;
std::unordered_set<uint64> g_scenario_bots;
}

void MarkScenarioBot(uint64 bot_id)
{
    std::lock_guard<std::mutex> lock(g_scenario_mu);
    g_scenario_bots.insert(bot_id);
}

bool IsScenarioBot(uint64 bot_id)
{
    std::lock_guard<std::mutex> lock(g_scenario_mu);
    return g_scenario_bots.count(bot_id) != 0;
}

std::vector<uint64> ScenarioBots()
{
    std::lock_guard<std::mutex> lock(g_scenario_mu);
    return { g_scenario_bots.begin(), g_scenario_bots.end() };
}

BotRole RoleOf(uint64 bot_id)
{
    if (!Services::Initialized() || !Services::Lifecycle().is_bot(bot_id))
        return BotRole::Owned;
    RoleFacts f;
    f.owner_bound = Services::Owners().GetOwner(bot_id).account_id != 0;
    f.scenario = IsScenarioBot(bot_id);
    if (BotAI* ai = Services::Registry().ai(bot_id))
        f.bg_committed = ai->is_jit_purpose();
    return ClassifyRole(f);
}

FreeFacts FreeFactsOf(Player const* bot, uint64 bot_id)
{
    RoleFacts rf;
    rf.owner_bound = Services::Owners().GetOwner(bot_id).account_id != 0;
    rf.scenario = IsScenarioBot(bot_id);
    if (WorldSession const* sess = bot->GetSession())
        rf.non_pool_account = !Services::Accounts().is_pool_account(sess->GetAccountId());
    if (BotAI* ai = Services::Registry().ai(bot_id))
        rf.bg_committed = ai->is_jit_purpose();

    FreeFacts f;
    f.role = ClassifyRole(rf);
    f.in_world = bot->IsInWorld();
    f.alive = bot->IsAlive();
    f.grouped = bot->GetGroup() != nullptr;
    f.in_combat = bot->IsInCombat();
    f.teleporting = bot->IsBeingTeleported();
    Map const* map = bot->GetMap();
    f.on_instance_map = map && (map->IsDungeon() || map->IsBattlegroundOrArena());
    f.world_busy = Services::Population().WorldSpawnerPending(bot_id) || Services::Population().IsWorldPlaced(bot_id);
    return f;
}

} // namespace Playerbot::Pool
