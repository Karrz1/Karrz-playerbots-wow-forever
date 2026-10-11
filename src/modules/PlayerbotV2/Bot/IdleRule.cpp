// IdleRule — registry implementation.

#include "IdleRule.h"
#include "BotSnapshotView.h"
#include "BotAI.h"
#include "BotIntentEmitter.h"
#include "Group/GroupSnapshot.h"

#include <algorithm>
#include <mutex>
#include "Log.h"   // TC_LOG_ERROR

namespace Playerbot {

void IdleRuleRegistry::register_rule(IdleRule r)
{
    // finalize() runs after every register_rule from RegisterAllIdleRules.
    // Hitting this branch means someone is registering a rule AFTER init,
    // which would race with AI workers calling tick(). Refuse instead.
    if (finalized_)
    {
        TC_LOG_ERROR("playerbot.v2",
            "[IdleRuleRegistry] register_rule('{}', prio={}) called after "
            "finalize() — refusing to mutate vector while AI workers tick. "
            "All rule registration must complete in RegisterAllIdleRules.",
            r.name, r.priority);
        return;
    }
    rules_.push_back(std::move(r));
}

namespace
{
// Rules a companion in its owner's group must not run: each sends the bot about the world on its own business. Upkeep
// (food, buffs, heals, rez, supplies, loot, rolls, equipping), survival, owner orders and assisting the group stay, and
// so do accepting the group's LFG proposal or BG port and playing the dungeon/BG once inside (bg_*, dungeon_dispatch,
// lfg_proposal_accept): the owner queued the group. Repairs and new trainer spells come free (BackfillOne).
bool IsWorldErrand(std::string_view name)
{
    static constexpr std::string_view kPrefixes[] = {
        "idle:ah_", "idle:autoact_dispatch", "idle:bags_full_recover", "idle:bank_deposit",
        "idle:calendar_", "idle:capital_bag_run", "idle:craft_order_", "idle:critical_repair", "idle:dual_spec_switch",
        "idle:dungeon_loop_requeue", "idle:dungeon_quest_approach", "idle:far_same_map_travel", "idle:gather",
        "idle:guild_", "idle:homebind", "idle:invite_nearby_player", "idle:legacy_vendor_dispatch",
        "idle:look_at_npc", "idle:loot_chest", "idle:mail_drain", "idle:mat_share_to_owner",
        "idle:pet_swap_from_stable", "idle:pursue_quest_", "idle:pvp_help_callout", "idle:assist_friend_pvp",
        "idle:quest_", "idle:react_to_passerby", "idle:resolve_junk_quests", "idle:swim_stuck_hearth",
        "idle:swim_stuck_walk_to_hub", "idle:tame_beast", "idle:taxi_discover", "idle:transport_ride", "idle:vendor_visit",
        "idle:walk_",
        "idle:wq_", "idle:yell_lfg", "idle:ambient_sit", "idle:rpg_",
    };
    for (std::string_view prefix : kPrefixes)
        if (name.starts_with(prefix))
            return true;
    return false;
}
}

void IdleRuleRegistry::finalize()
{
    for (IdleRule& rule : rules_)
        rule.world_errand = IsWorldErrand(rule.name);
    std::stable_sort(rules_.begin(), rules_.end(),
        [](IdleRule const& a, IdleRule const& b) { return a.priority > b.priority; });
    finalized_ = true;
}

std::string_view IdleRuleRegistry::tick(BotSnapshotView const& s, BotAI& ai,
                                        GroupSnapshotView const& g,
                                        BotIntentEmitter& emit, uint32 now_ms,
                                        int min_priority)
{
    // finalize() is called single-threaded from RegisterAllIdleRules
    // before any worker starts. If a future change forgets to call it,
    // self-heal once + warn (still hazardous if multiple workers reach
    // here on the same first tick, but better than silent UB).
    if (!finalized_)
    {
        static std::once_flag warn_once;
        std::call_once(warn_once, [] {
            TC_LOG_ERROR("playerbot.v2",
                "[IdleRuleRegistry] tick() reached an unfinalized registry — "
                "calling finalize() defensively. RegisterAllIdleRules must "
                "call finalize() before workers tick.");
        });
        finalize();
    }
    for (auto const& r : rules_)
    {
        if (r.priority < min_priority) break;   // rules_ sorted desc — bail early
        if (!r.enabled) continue;
        if (!r.gate)    continue;
        if (r.world_errand && s.is_companion_with_owner()) continue;
        // Per-rule gate-throttle. Rules with min_interval_ms > 0 skip
        // the gate entirely until the throttle window elapses. Saves
        // the gate-call cost for low-urgency rules (mail, calendar,
        // auction, charter) that don't need 5Hz responsiveness.
        if (r.min_interval_ms != 0)
        {
            if (r.last_gate_attempt_ms != 0
                && (now_ms - r.last_gate_attempt_ms) < r.min_interval_ms)
                continue;
            r.last_gate_attempt_ms = now_ms;
        }
        if (!r.gate(s, ai, g, now_ms)) continue;
        if (r.fire && r.fire(s, ai, g, emit, now_ms))
        {
            r.last_fired_ms = now_ms;
            return r.name;
        }
    }
    return {};
}

std::vector<IdleRuleRegistry::RuleDiagnostic>
IdleRuleRegistry::diagnose(BotSnapshotView const& s, BotAI& ai,
                           GroupSnapshotView const& g, uint32 now_ms) const
{
    // ensure_sorted() is non-const; do a stable copy + sort instead so
    // diagnose can run from a const handle. Cheap — rule count is small.
    std::vector<IdleRule const*> ordered;
    ordered.reserve(rules_.size());
    for (auto const& r : rules_) ordered.push_back(&r);
    std::stable_sort(ordered.begin(), ordered.end(),
        [](IdleRule const* a, IdleRule const* b) { return a->priority > b->priority; });

    std::vector<RuleDiagnostic> out;
    out.reserve(ordered.size());
    for (auto const* r : ordered)
    {
        bool gate_true = false;
        if (r->enabled && r->gate)
        {
            try { gate_true = r->gate(s, ai, g, now_ms); }
            catch (...) { gate_true = false; }
        }
        out.push_back({r->name, r->priority, gate_true, r->enabled, r->last_fired_ms});
    }
    return out;
}

void RegisterAllIdleRules(IdleRuleRegistry& r)
{
    RegisterVendorRules(r);
    RegisterAuctionRules(r);
    RegisterMailRules(r);
    RegisterCalendarRules(r);
    RegisterQuestRules(r);
    RegisterCompanionQuestRules(r);   // idle:companion_quest_* @699-702 (companions only)
    RegisterBankRules(r);
    RegisterGatheringRules(r);
    RegisterGuildRules(r);
    RegisterTrainerRules(r);
    RegisterHearthRules(r);
    RegisterHunterPetRules(r);
    RegisterLootDrainRules(r);
    RegisterAmbientRules(r);
    RegisterSurvivalRules(r);
    RegisterCoreFlowRules(r);
    RegisterOocConsumableRules(r);
    RegisterMaintainRules(r);
    RegisterDungeonBgRules(r);
    RegisterPrologueRules(r);
    RegisterElevatorRules(r);
    RegisterCraftOrderRules(r);
    RegisterFarTravelRules(r);   // idle:far_same_map_travel @697
    RegisterClassicUpkeepRules(r);   // idle:classic_* @992
    RegisterRpgRules(r);             // idle:rpg_* @110-135 (world bots only)
    RegisterRaidIdleRules(r);        // idle:raid_prepull @991
    // Future passes: RegisterTravelRules(r); RegisterDungeonRules(r);
    // RegisterBgRules(r); ...

    // CRITICAL: finalize sorts rules_ + locks against further register_rule.
    // Must run BEFORE any AI worker calls tick(). Services::Init calls
    // RegisterAllIdleRules immediately before starting the AI worker pool
    // (see Services.cpp), so finalize() runs single-threaded here.
    r.finalize();
}

} // namespace Playerbot
