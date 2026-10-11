// CompanionQuestRules - a companion (a bot grouped with its owner) quests with him like a party member.
//
//   idle:companion_quest_turnin   the bot's quest is complete and its owner stands at the ender: walk up, hand it in
//                                 (reward picked by the API's class scorer), BotChatter says the turn-in line.
//   idle:companion_quest_accept   the owner holds a quest the giver beside him offers (or the owner ordered
//                                 "accept"): walk up and take it too.
//   idle:companion_quest_object   a chest / prop / node one of the bot's own open objectives needs lies near the
//                                 owner: click it.
//
// Every decision is made on the snapshot (BotSnapshot::companion_quest, filled on the world thread by
// CompanionQuest::BuildState); the rules only emit intents. They are NOT world errands (IsWorldErrand): they
// run for a companion, bounded to the giver / object being near the owner, and the bot goes back to its follow
// slot when done (detour lease, no teleport). Playerbot.Behavior.QuestAccept / QuestTurnIn / QuestLoot switch
// them off; the lists are empty then.

#include "Bot/IdleRule.h"
#include "Group/GroupSnapshot.h"
#include "Bot/BotAI.h"
#include "Bot/BotSnapshotView.h"
#include "Bot/BotIntentEmitter.h"
#include "Bot/Quest/CompanionQuest.h"
#include "Log.h"

#include <cmath>
#include <limits>

namespace Playerbot {

namespace {

constexpr float kInteractSq    = 5.0f * 5.0f;
// Emit inside this radius only: the live CanInteractWithQuestGiver gate is checked at dispatch, after the bot
// may have drifted (same margin as idle:quest_accept).
constexpr float kActEmitSq     = 4.5f * 4.5f;
constexpr float kStopShortYd   = 2.5f;
// A GO is clicked from this close.
constexpr float kUseSq         = 4.0f * 4.0f;
constexpr uint64 kTurnInKeyBit = uint64(1) << 40;
constexpr uint64 kObjectKeyBit = uint64(1) << 41;

bool FindPos(BotSnapshotView const& s, ObjectGuid guid, float& x, float& y, float& z)
{
    for (auto const& u : s.raw().combat.nearby_friends)
        if (u.guid == guid) { x = u.x; y = u.y; z = u.z; return true; }
    for (auto const& o : s.raw().world_objects.nearby_objects)
        if (o.guid == guid) { x = o.x; y = o.y; z = o.z; return true; }
    return false;
}

// The owner or the bot is fighting: quest errands wait (assist / defend first).
bool GroupFighting(BotSnapshotView const& s, GroupSnapshotView const& g)
{
    if (s.in_combat()) return true;
    ObjectGuid const owner = s.raw().owner_guid;
    if (auto const* members = g.members())
        for (GroupMemberSummary const& m : *members)
            if (m.guid == owner)
                return m.online && m.in_combat;
    return false;
}

bool CommonGate(BotSnapshotView const& s, GroupSnapshotView const& g)
{
    if (!s.is_alive() || s.is_casting() || s.in_battleground()) return false;
    if (!s.raw().companion_quest.owner_here) return false;
    return !GroupFighting(s, g);
}

// A short "reading the quest" pause (0.6-1.8 s, per bot) before an accept / turn-in, like a person at the
// window. `slot` is the BotAI hesitation timer; 0 = not armed. Returns true once the pause is over.
bool PauseOver(BotSnapshotView const& s, uint32 slot_value, void (BotAI::*set)(uint32), BotAI& ai)
{
    uint32 const now_ms = s.published_at_ms();
    if (slot_value == 0)
    {
        uint32 const jitter = 600u + (uint32(s.bot_id()) * 2654435761u) % 1200u;
        (ai.*set)(now_ms + jitter);
        return false;
    }
    return now_ms >= slot_value;
}

// Walk the last stretch toward (gx,gy,gz), stopping kStopShortYd short. The detour lease keeps the group
// follow from pulling the bot back mid-walk (bounded: a fight or a 45 y separation overrides it).
void Approach(BotSnapshotView const& s, BotAI& ai, BotIntentEmitter& emit, float gx, float gy, float gz, float dsq)
{
    float bx, by, bz; s.position(bx, by, bz);
    float const dist = std::sqrt(dsq);
    float const scale = dist > kStopShortYd ? (dist - kStopShortYd) / dist : 0.f;
    emit.move_to(bx + (gx - bx) * scale, by + (gy - by) * scale, gz, /*run*/ true);
    ai.grant_detour(s.published_at_ms());
}

bool MayWalk(BotSnapshotView const& s, BotAI& ai)
{
    return !ai.holding_position(s.published_at_ms()) && !s.is_rooted();
}

// ---------- idle:companion_quest_turnin ----------
bool TurnInGate(BotSnapshotView const& s, BotAI& ai, GroupSnapshotView const& g, uint32)
{
    auto const& cq = s.raw().companion_quest;
    if (cq.turn_in.empty())
    {
        if (ai.pending_quest_turnin_at_ms() != 0) ai.set_pending_quest_turnin_at_ms(0);
        return false;
    }
    return CommonGate(s, g);
}

bool TurnInFire(BotSnapshotView const& s, BotAI& ai, GroupSnapshotView const&, BotIntentEmitter& emit, uint32)
{
    uint32 const now_ms = s.published_at_ms();
    float bx, by, bz; s.position(bx, by, bz);
    float near_x = 0.f, near_y = 0.f, near_z = 0.f, near_dsq = std::numeric_limits<float>::max();
    for (CompanionQuestAction const& a : s.raw().companion_quest.turn_in)
    {
        if (ai.quest_reward_failed_recent(a.quest_id, now_ms)) continue;
        if (ai.action_recently_tried(BotAI::ActionKind::CompanionQuest, kTurnInKeyBit | a.quest_id, now_ms)) continue;
        float gx, gy, gz;
        if (!FindPos(s, a.giver, gx, gy, gz)) continue;
        float const dx = gx - bx, dy = gy - by, dz = gz - bz;
        float const dsq = dx * dx + dy * dy + dz * dz;
        if (dsq > kActEmitSq)
        {
            if (dsq < near_dsq) { near_dsq = dsq; near_x = gx; near_y = gy; near_z = gz; }
            continue;
        }
        if (!PauseOver(s, ai.pending_quest_turnin_at_ms(), &BotAI::set_pending_quest_turnin_at_ms, ai))
            return false;
        emit.face_target(a.giver);
        emit.complete_quest(a.giver, a.quest_id, /*reward_choice*/ 0xFF);   // 0xFF: best for the class
        ai.note_action_retry(BotAI::ActionKind::CompanionQuest, kTurnInKeyBit | a.quest_id, now_ms);
        ai.set_pending_quest_turnin_at_ms(0);
        ai.grant_detour(now_ms);
        ai.set_last_rule_fired("idle:companion_quest_turnin");
        TC_LOG_INFO("playerbot.v2", "[quest] {} turn_in {} {}{}", s.name(), a.quest_id,
            CompanionQuest::QuestLabel(a.quest_id), a.via_order ? " (owner order)" : " (with owner)");
        return true;
    }
    if (near_dsq < std::numeric_limits<float>::max() && MayWalk(s, ai))
    {
        Approach(s, ai, emit, near_x, near_y, near_z, near_dsq);
        ai.set_last_rule_fired("idle:companion_quest_turnin_approach");
        return true;
    }
    return false;
}

// ---------- idle:companion_quest_accept ----------
bool AcceptGate(BotSnapshotView const& s, BotAI& ai, GroupSnapshotView const& g, uint32)
{
    auto const& cq = s.raw().companion_quest;
    if (cq.accept.empty())
    {
        if (ai.pending_quest_accept_at_ms() != 0) ai.set_pending_quest_accept_at_ms(0);
        return false;
    }
    // Full bags: a quest that hands out an item fails on accept and would re-pin the bot; free space first.
    if (s.bag_free_slots() == 0) return false;
    return CommonGate(s, g);
}

bool AcceptFire(BotSnapshotView const& s, BotAI& ai, GroupSnapshotView const&, BotIntentEmitter& emit, uint32)
{
    uint32 const now_ms = s.published_at_ms();
    float bx, by, bz; s.position(bx, by, bz);
    float near_x = 0.f, near_y = 0.f, near_z = 0.f, near_dsq = std::numeric_limits<float>::max();
    for (CompanionQuestAction const& a : s.raw().companion_quest.accept)
    {
        if (ai.action_recently_tried(BotAI::ActionKind::CompanionQuest, a.quest_id, now_ms)) continue;
        float gx, gy, gz;
        if (!FindPos(s, a.giver, gx, gy, gz)) continue;
        float const dx = gx - bx, dy = gy - by, dz = gz - bz;
        float const dsq = dx * dx + dy * dy + dz * dz;
        if (dsq > kActEmitSq)
        {
            if (dsq < near_dsq) { near_dsq = dsq; near_x = gx; near_y = gy; near_z = gz; }
            continue;
        }
        if (!PauseOver(s, ai.pending_quest_accept_at_ms(), &BotAI::set_pending_quest_accept_at_ms, ai))
            return false;
        emit.face_target(a.giver);
        emit.accept_quest(a.giver, a.quest_id);
        ai.note_action_retry(BotAI::ActionKind::CompanionQuest, a.quest_id, now_ms);
        ai.set_pending_quest_accept_at_ms(0);
        ai.grant_detour(now_ms);
        ai.set_last_rule_fired("idle:companion_quest_accept");
        TC_LOG_INFO("playerbot.v2", "[quest] {} accept {} {}{}", s.name(), a.quest_id,
            CompanionQuest::QuestLabel(a.quest_id), a.via_order ? " (owner order)" : " (with owner)");
        return true;
    }
    if (near_dsq < std::numeric_limits<float>::max() && MayWalk(s, ai))
    {
        Approach(s, ai, emit, near_x, near_y, near_z, near_dsq);
        ai.set_last_rule_fired("idle:companion_quest_accept_approach");
        return true;
    }
    return false;
}

// ---------- idle:companion_quest_object ----------
bool ObjectGate(BotSnapshotView const& s, BotAI&, GroupSnapshotView const& g, uint32)
{
    if (s.raw().companion_quest.objects.empty()) return false;
    if (s.bag_free_slots() == 0) return false;
    return CommonGate(s, g);
}

bool ObjectFire(BotSnapshotView const& s, BotAI& ai, GroupSnapshotView const&, BotIntentEmitter& emit, uint32)
{
    uint32 const now_ms = s.published_at_ms();
    float bx, by, bz; s.position(bx, by, bz);
    float near_x = 0.f, near_y = 0.f, near_z = 0.f, near_dsq = std::numeric_limits<float>::max();
    for (CompanionQuestObject const& o : s.raw().companion_quest.objects)
    {
        uint64 const key = kObjectKeyBit | o.go.GetCounter();
        if (ai.action_recently_tried(BotAI::ActionKind::CompanionQuest, key, now_ms)) continue;
        float gx, gy, gz;
        if (!FindPos(s, o.go, gx, gy, gz)) continue;
        float const dx = gx - bx, dy = gy - by, dz = gz - bz;
        float const dsq = dx * dx + dy * dy + dz * dz;
        if (dsq > kUseSq)
        {
            if (dsq < near_dsq) { near_dsq = dsq; near_x = gx; near_y = gy; near_z = gz; }
            continue;
        }
        emit.use_game_object(o.go);
        ai.note_action_retry(BotAI::ActionKind::CompanionQuest, key, now_ms);
        ai.grant_detour(now_ms);
        ai.set_last_rule_fired("idle:companion_quest_object");
        TC_LOG_INFO("playerbot.v2", "[quest] {} use_object {} (entry {}) for {}", s.name(), o.go.GetCounter(), o.entry,
            CompanionQuest::QuestLabel(o.quest_id));
        return true;
    }
    if (near_dsq < std::numeric_limits<float>::max() && MayWalk(s, ai))
    {
        Approach(s, ai, emit, near_x, near_y, near_z, near_dsq);
        ai.set_last_rule_fired("idle:companion_quest_object_approach");
        return true;
    }
    return false;
}

} // anonymous namespace

void RegisterCompanionQuestRules(IdleRuleRegistry& r)
{
    // 702 / 701 / 699: above the OOC upkeep band (660-696) so a quest action beats eating and buffing, below
    // assist / combat / survival (>= 703); the gates also wait while the owner or the bot is fighting.
    {
        IdleRule rule;
        rule.name     = "idle:companion_quest_turnin";
        rule.priority = 702;
        rule.gate     = &TurnInGate;
        rule.fire     = &TurnInFire;
        r.register_rule(std::move(rule));
    }
    {
        IdleRule rule;
        rule.name     = "idle:companion_quest_accept";
        rule.priority = 701;
        rule.gate     = &AcceptGate;
        rule.fire     = &AcceptFire;
        r.register_rule(std::move(rule));
    }
    {
        IdleRule rule;
        rule.name     = "idle:companion_quest_object";
        rule.priority = 699;
        rule.gate     = &ObjectGate;
        rule.fire     = &ObjectFire;
        r.register_rule(std::move(rule));
    }
}

} // namespace Playerbot
