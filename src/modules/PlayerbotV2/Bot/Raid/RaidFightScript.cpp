// A boss fight script's brain pass. See RaidFightScript.h.
//
// Runs after the generic rule passes of a plan. For the DPS bots of a scripted boss it replaces the threat hold with a fixed start wait,
// names one kill target for everybody, and drops any crowd-control job aimed at the fight's own enemies. Tanks and healers are not touched:
// their assignment, pick-up and healing rules are the generic ones.

#include "RaidBrainInternal.h"
#include "RaidFightScript.h"

#include "Creature.h"
#include "Group.h"
#include "Log.h"
#include "Player.h"

#include <algorithm>
#include <unordered_map>

namespace Playerbot::Raid::detail {

namespace {

struct FightState
{
    uint32_t since = 0;    // first plan pass with an engaged enemy of the fight
    uint32_t last = 0;     // last plan pass with one
    ObjectGuid focus;      // last pick (hysteresis, change-only log)
    uint32_t startLogged = 0;
};
std::unordered_map<uint64_t, FightState> g_fights;   // (group, map)
std::unordered_map<uint64_t, uint64_t> g_ownPick;     // bot -> its last pick when it could not take the raid's focus (Fight::schoolAware)

constexpr uint32_t kFightGapMs = 8000;   // no engaged enemy for this long: the next pull starts a new fight
constexpr float kCampSpellReach = 30.0f;  // Fight::campReach: an enemy farther than this is not one a bot at the camp casts at
// Fight::campReach: the raid's focus stays "at the camp" out to campReach + this. nef-karrz-1114c (Karrz's 12:43 pull): the tanks held their
// drakonids 21-32 y from the camp centre, right across the 28 y edge, and a held drakonid stepping out by a yard dropped it from every bot's
// list while another stood inside. The focus changed 30 times in 100 s, often back and forth before a kill (660 -> 661 -> 660), the damage
// was split over two or three drakonids, 3,000 raid damage a second against the waves, and the adds went from 5 to 20.
constexpr float kCampFocusKeep = 8.0f;
constexpr uint8_t kSkullIcon = 7, kCrossIcon = 6;

std::unordered_map<uint64_t, uint64_t> g_jobPick;   // bot -> the enemy of its area job (Who::AoeShare row) it was last given

// A boss of the fight last seen on a raider who is no tank (its tank died, or a DPS pulled it off): enemy -> when. For kBossRetakeMs after
// that the threat hold (PublishThreatHolds) stays for the DPS on it, so the tank that takes it back keeps it. The script drops every threat
// hold after its start wait, and bwl_nefarian 0136-r8 run 1 shows what that does when a tank dies: the main tank died at 30% (396 s), three
// tanks taunted him in turn over the next 45 s while the DPS kept their full damage on him, he went back to a DPS bot after each taunt and
// killed five of them, and the raid reached the Bone Constructs at 20% with 31 of 40 alive.
std::unordered_map<uint64_t, uint32_t> g_bossOffTank;

// A boss that joins a fight already under way (Nefarian landing in Victor's fight): enemy -> when it was first seen engaged, and since when a
// tank of the raid has held it (0 = not now). The DPS keep off it until a tank has held it for kLateBossTankMs, at least kLateBossMinMs after
// it came and at most kLateBossMaxMs (no tank ever taking it must not freeze the raid). Karrz, 2026-11-14 15:20, after the 15:10 wipe: when
// Nefarian lands everyone but the tanks should stand back, the tank picks him up and turns him, then the raid goes in. In that pull the raid
// stood round him as he landed and 6 died to his swings before a tank had him, and 5 more to the first Shadow Flame.
struct LateBoss { uint32_t seen = 0; uint32_t onTank = 0; uint32_t last = 0; bool released = false; };
std::unordered_map<uint64_t, LateBoss> g_lateBoss;
constexpr uint32_t kLateBossTankMs = 4000;
constexpr uint32_t kLateBossMinMs = 6000;
constexpr uint32_t kLateBossMaxMs = 20000;
uint32_t g_retakeLogMs = 0;
constexpr uint32_t kBossRetakeMs = 20000;

// The enemy of this member's area job: the first Priority row for Who::AoeShare that names it while one of that row's enemies is up. Its
// previous one while it lives and it may hit it, else one that hits a raider who is no tank (the healers and casters it walks into), then
// the nearest. One in the member's sight before one behind a wall. None: no job now.
Unit* AreaJobTarget(Plan& plan, Member const& m, RaidOrder const& o)
{
    if (!plan.rules)
        return nullptr;
    uint64_t const self = m.p->GetGUID().GetCounter();
    auto const usable = [&](Unit* e, uint32_t entry)
    {
        return e->GetEntry() == entry && e->IsAlive() && e->GetVictim() && !e->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE) &&
               std::find(o.forbidden_targets.begin(), o.forbidden_targets.end(), e->GetGUID()) == o.forbidden_targets.end();
    };
    for (Priority const& row : plan.rules->priority)
    {
        if (row.who != Who::AoeShare || row.value <= 0 || !Matches(row.who, m, plan.mainTank))
            continue;
        if (auto prev = g_jobPick.find(self); prev != g_jobPick.end())
            for (Unit* e : plan.enemies)
                if (e->GetGUID().GetCounter() == prev->second && usable(e, row.enemy))
                    return e;
        Unit* best = nullptr;
        bool bestSeen = false, bestOnRaider = false;
        float bestDist = 0.f;
        for (Unit* e : plan.enemies)
        {
            if (!usable(e, row.enemy))
                continue;
            bool const seen = m.p->IsWithinLOSInMap(e);
            Member const* vm = nullptr;
            if (Player* vp = e->GetVictim()->ToPlayer())
                vm = FindMember(plan, vp);
            bool const onRaider = vm && !vm->tank;
            float const dist = m.p->GetExactDist2d(e);
            bool better = !best;
            if (!better && seen != bestSeen)
                better = seen;
            else if (!better && onRaider != bestOnRaider)
                better = onRaider;
            else if (!better)
                better = dist < bestDist;
            if (better)
            {
                best = e;
                bestSeen = seen;
                bestOnRaider = onRaider;
                bestDist = dist;
            }
        }
        if (best)
        {
            g_jobPick[self] = best->GetGUID().GetCounter();
            return best;
        }
    }
    g_jobPick.erase(self);
    return nullptr;
}

} // namespace

void ApplyFightScript(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders, uint32_t now, std::ostringstream& dump)
{
    Script::Fight const* fight = nullptr;
    for (Unit* enemy : plan.enemies)
        if ((fight = Script::FightFor(plan.mapId, enemy->GetEntry())))
            break;
    if (!fight)
        return;

    // Only enemies the fight names count (a stray pack mob near the room does not start the clock or become the focus).
    std::vector<Unit*> mine;
    for (Unit* enemy : plan.enemies)
        if (Script::FightFor(plan.mapId, enemy->GetEntry()) == fight && enemy->GetVictim())
            mine.push_back(enemy);
    if (mine.empty())
        return;

    for (Unit* enemy : mine)
        if (IsBoss(enemy->ToCreature(), plan.rules))
            if (Player* vp = enemy->GetVictim()->ToPlayer())
                if (Member const* vm = FindMember(plan, vp); !vm || !vm->tank)
                    g_bossOffTank[enemy->GetGUID().GetCounter()] = now;
    if (g_bossOffTank.size() > 256)
        for (auto it = g_bossOffTank.begin(); it != g_bossOffTank.end();)
            it = (now - it->second > 120000) ? g_bossOffTank.erase(it) : std::next(it);
    size_t retakeHeld = 0;

    uint64_t const key = (plan.groupLow << 16) ^ uint64_t(plan.mapId);
    FightState& st = g_fights[key];
    if (!st.since || now - st.last > kFightGapMs)
    {
        st.since = now;
        st.focus = ObjectGuid::Empty;
        st.startLogged = 0;
    }
    st.last = now;
    bool const wait = Script::StartWait(st.since, now, fight->startWaitMs);
    // The late bosses of this fight the DPS still keep off (see g_lateBoss).
    std::vector<ObjectGuid> lateHold;
    for (Unit* enemy : mine)
    {
        Creature const* c = enemy->ToCreature();
        if (!c || enemy->GetEntry() == fight->boss || !IsBoss(c, plan.rules))
            continue;
        LateBoss& lb = g_lateBoss[enemy->GetGUID().GetCounter()];
        // Its clock starts where it can be fought: not while it flies in, untouchable. Karrz's pull 2026-11-14 15:40: Nefarian was engaged
        // (victim Roland) from his take-off at 15:40:23, the hold "released after 6405 ms (a tank holds it)" at 15:40:29, and he landed
        // only at 15:40:35: the raid walked in under him as he came down, and his first swings and Shadow Flame took three of it.
        bool const fightable = !enemy->IsFlying() && !enemy->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE) && !enemy->IsUninteractible() &&
                               !(c->HasUnitFlag(UNIT_FLAG_IMMUNE_TO_PC));
        if (lb.seen && now - lb.last > kFightGapMs)
            lb = LateBoss{};
        if (!lb.seen && !fightable)
        {
            if (now - st.since >= fight->startWaitMs)
                lateHold.push_back(enemy->GetGUID());   // still coming in: nobody goes near it
            continue;
        }
        if (!lb.seen)
            lb = LateBoss{ now, 0, now, now - st.since < fight->startWaitMs };   // there from the start: the start wait covers it
        lb.last = now;
        if (lb.released)
            continue;
        bool onTank = false;
        if (Player* vp = enemy->GetVictim()->ToPlayer())
            if (Member const* vm = FindMember(plan, vp); vm && vm->tank)
                onTank = true;
        lb.onTank = onTank ? (lb.onTank ? lb.onTank : now) : 0;
        uint32_t const age = now - lb.seen;
        if ((lb.onTank && now - lb.onTank >= kLateBossTankMs && age >= kLateBossMinMs) || age >= kLateBossMaxMs)
        {
            lb.released = true;
            TC_LOG_INFO("playerbot.v2", "[fight_script] group={} map={} {}: {} released to the DPS after {} ms ({})", plan.groupLow, plan.mapId,
                        fight->name, enemy->GetName(), age, lb.onTank ? "a tank holds it" : "no tank took it in time");
            continue;
        }
        lateHold.push_back(enemy->GetGUID());
    }
    if (g_lateBoss.size() > 64)
        for (auto it = g_lateBoss.begin(); it != g_lateBoss.end();)
            it = (now - it->second.last > 120000) ? g_lateBoss.erase(it) : std::next(it);
    // A timed kill the fight needs now, whatever the kill order says (Zul'Gurub: a Son of Hakkar before Blood Siphon).
    Unit* const forced = plan.mapId == 309 ? ZgForcedFocus(plan, now) : nullptr;

    // The owner's skull, else cross.
    Group const* grp = nullptr;
    for (Member const& m : plan.members)
        if (m.p && (grp = m.p->GetGroup()))
            break;
    ObjectGuid ownerMark;
    if (grp)
        for (uint8_t icon : { kSkullIcon, kCrossIcon })
        {
            if (fight->skullOnly && icon != kSkullIcon)
                continue;
            ObjectGuid const g = grp->GetTargetIcon(icon);
            if (g.IsEmpty())
                continue;
            for (Unit* enemy : mine)
                if (enemy->GetGUID() == g)
                    ownerMark = g;
            if (!ownerMark.IsEmpty())
                break;
        }

    // Fight::heldFirst: the enemies a tank of the raid holds.
    std::vector<Unit*> held;
    if (fight->heldFirst)
        for (Unit* enemy : mine)
            if (Unit* v = enemy->GetVictim())
                if (Player* vp = v->ToPlayer())
                    if (Member const* vm = FindMember(plan, vp); vm && vm->tank)
                        held.push_back(enemy);

    // Fight::schoolAware: the schools each enemy of the fight avoids (EnemySchoolMasks, as the generic target pick reads them).
    std::vector<uint8_t> avoid(mine.size(), 0);
    if (fight->schoolAware)
    {
        Unit const* const schoolCaster = SchoolReferenceCaster(plan);
        for (size_t i = 0; i < mine.size(); ++i)
        {
            if (Creature const* c = mine[i]->ToCreature(); c && c->IsInEvadeMode())
                continue;
            uint8_t prefer = 0;
            EnemySchoolMasks(mine[i], schoolCaster, prefer, avoid[i]);
        }
    }

    ObjectGuid chosen;   // the raid's focus this pass: the pick most DPS bots were given
    std::unordered_map<uint64_t, size_t> votes;
    size_t dps = 0;
    for (Member const& m : plan.members)
    {
        if (!m.bot)
            continue;
        auto it = orders.find(m.p->GetGUID().GetCounter());
        if (it == orders.end())
            continue;
        RaidOrder& o = it->second;

        // A crowd-control job on the fight's own enemies: nobody sheeps a Protector (no source says it works), and the mark orders are the
        // owner's. Everything else the election chose stays.
        if (o.cast_kind == RaidOrder::CastKind::CrowdControl)
            for (Unit* enemy : mine)
                if (enemy->GetGUID() == o.cast_target)
                {
                    o.cast_kind = RaidOrder::CastKind::None;
                    o.cast_spell = 0;
                    o.cast_target = ObjectGuid::Empty;
                    break;
                }

        if (m.tank || m.healer)
            continue;
        ObjectGuid const threatHold = o.hold_target;   // PublishThreatHolds ran before the script
        // Fight::freeAura: this bot has its own enemy the others cannot see (a Shade of Jin'do); the generic rules keep it.
        if (fight->freeAura && m.p->HasAura(fight->freeAura))
            continue;
        // An area job the encounter hands this bot (a Priority row for Who::AoeShare) while its enemy is up: the script names that enemy, so
        // the row's target and the area spells around it win over the script's single focus. bwl_nefarian 1111e: at 20% Nefarian raised
        // 29-41 Bone Constructs, the script kept every caster on him (122 attack starts on Nefarian to 28 on the constructs after the rise),
        // the constructs walked into the healers and casters and killed 20-25 raiders in 40-55 s, and both runs wiped with him at 3% and 9%.
        // Handing the bot back to the generic rules was not enough (1111f run 1: the job's mages 22 attack starts on Nefarian to 4 on the
        // constructs, warlocks 23 to 4, all 12 healers dead at 550-557 s): their pick is only a fallback, and it steers a bot with no target,
        // never one already on Nefarian. A named target is attacked whatever the bot was on.
        if (Unit* job = AreaJobTarget(plan, m, o))
        {
            o.script_owns = true;
            o.script_focus = job->GetGUID();
            o.fallback_target = job->GetGUID();
            o.hold_target = ObjectGuid::Empty;
            continue;
        }
        if (forced)
        {
            o.script_owns = true;
            o.script_focus = forced->GetGUID();
            o.fallback_target = forced->GetGUID();
            o.hold_target = ObjectGuid::Empty;
            ++votes[forced->GetGUID().GetCounter()];
            ++dps;
            continue;
        }

        // Skull only: everything else of the fight, marked or sheeped or not, is off limits for this bot.
        if (fight->skullOnly)
            for (Unit* enemy : mine)
                if (enemy->GetGUID() != ownerMark)
                    o.forbidden_targets.push_back(enemy->GetGUID());

        // A caster counts the schools of its main nuke (MemberMainSchools: a frost mage leaves the Blue Drakonids to the others); when
        // every enemy it can reach resists those, all its class's schools count again.
        uint8_t const allSchools = fight->schoolAware ? MemberDamageSchools(m, plan.mainTank) : uint8_t(0);
        uint8_t const mainSchools = fight->schoolAware ? MemberMainSchools(m, plan.mainTank) : uint8_t(0);
        std::vector<Script::Enemy> view;
        view.reserve(mine.size());
        for (size_t i = 0; i < mine.size(); ++i)
        {
            Unit* enemy = mine[i];
            Script::Enemy e;
            e.guid = enemy->GetGUID().GetCounter();
            e.entry = enemy->GetEntry();
            e.hpPct = enemy->GetHealthPct();
            bool const forbidden = std::find(o.forbidden_targets.begin(), o.forbidden_targets.end(), enemy->GetGUID()) != o.forbidden_targets.end();
            Creature const* c = enemy->ToCreature();
            // Not one that cannot be hit now (Arlokk vanished: invisible, uninteractible, not attackable).
            e.eligible = enemy->IsAlive() && !forbidden && !(c && c->IsInEvadeMode()) && enemy->IsVisible() && !enemy->IsUninteractible() &&
                         !enemy->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            e.held = std::find(held.begin(), held.end(), enemy) != held.end();
            view.push_back(e);
        }
        // Before the camp filter, as before: a bot whose schools the camp's enemies all resist still finds one in its reach.
        if (fight->schoolAware && allSchools)
        {
            bool mainFits = false;
            for (size_t i = 0; i < view.size(); ++i)
                if (view[i].eligible && (mainSchools & ~avoid[i]) != 0 &&
                    (fight->campReach <= 0.f || m.p->IsWithinDistInMap(mine[i], kCampSpellReach)))
                    mainFits = true;
            uint8_t const schools = mainFits ? mainSchools : allSchools;
            for (size_t i = 0; i < view.size(); ++i)
                if ((schools & ~avoid[i]) == 0)
                    view[i].eligible = false;
        }
        // Fight::campReach: only the enemies at the camp this bot can see, while there is one (see RaidFightScript.h, Nefarian).
        // Else the ones in its sight and spell reach (it is not sent walking: its post would walk it back), else every one.
        uint64_t const raidFocus = st.focus.IsEmpty() ? 0 : st.focus.GetCounter();
        if (fight->campReach > 0.f)
        {
            std::vector<bool> atCamp(view.size(), false), seen(view.size(), false);
            bool anyAtCamp = false, anySeen = false;
            for (size_t i = 0; i < mine.size(); ++i)
            {
                if (!view[i].eligible || !m.p->IsWithinDistInMap(mine[i], kCampSpellReach) || !m.p->IsWithinLOSInMap(mine[i]))
                    continue;
                seen[i] = anySeen = true;
                float const keep = view[i].guid == raidFocus ? fight->campReach + kCampFocusKeep : fight->campReach;
                if (Dist2D(mine[i]->GetPositionX(), mine[i]->GetPositionY(), fight->campX, fight->campY) <= keep)
                    atCamp[i] = anyAtCamp = true;
            }
            if (anyAtCamp || anySeen)
                for (size_t i = 0; i < view.size(); ++i)
                    view[i].eligible = anyAtCamp ? atCamp[i] : seen[i];
        }
        // The raid's focus is this bot's previous pick while it can take it; a bot that cannot keeps its own pick until that one dies.
        uint64_t previous = raidFocus;
        uint64_t const self = m.p->GetGUID().GetCounter();
        if (fight->schoolAware && previous)
        {
            bool canTake = false;
            for (Script::Enemy const& e : view)
                if (e.guid == previous && e.eligible)
                    canTake = true;
            if (!canTake)
                if (auto own = g_ownPick.find(self); own != g_ownPick.end())
                    previous = own->second;
        }
        uint64_t const pick = Script::PickFocus(*fight, view.data(), view.size(), ownerMark.IsEmpty() ? 0 : ownerMark.GetCounter(), previous);
        if (fight->schoolAware)
        {
            if (pick && st.focus.GetCounter() != pick)
                g_ownPick[self] = pick;
            else
                g_ownPick.erase(self);
        }
        ObjectGuid focus;
        for (Unit* enemy : mine)
            if (pick && enemy->GetGUID().GetCounter() == pick)
                focus = enemy->GetGUID();
        if (focus.IsEmpty())
        {
            // The skull carries a shield this bot must not strike (Damage Shield for melee, Magic Reflection for casters): it stands, the
            // generic pickers would start an attack on the skull each tick and every swing is reflected.
            if (fight->skullOnly && !ownerMark.IsEmpty() &&
                std::find(o.forbidden_targets.begin(), o.forbidden_targets.end(), ownerMark) != o.forbidden_targets.end())
            {
                o.script_owns = true;
                o.script_focus = ownerMark;
                o.hold_target = ownerMark;
            }
            continue;   // nothing the script can name for this bot: the generic rules keep it
        }

        o.script_owns = true;
        o.script_focus = focus;
        o.fallback_target = focus;
        // The one hold of a scripted fight: the start wait. No threat ratio, no over-threat release clocks.
        o.hold_target = wait ? focus : ObjectGuid::Empty;
        if (std::find(lateHold.begin(), lateHold.end(), focus) != lateHold.end())
            o.hold_target = focus;
        // A boss that was on a non-tank less than kBossRetakeMs ago and is back on a tank: a DPS bot at the tank's threat holds (see
        // g_bossOffTank). The hold's own clocks (PublishThreatHolds) still release it.
        if (!wait && threatHold == focus)
            if (auto off = g_bossOffTank.find(focus.GetCounter()); off != g_bossOffTank.end() && now - off->second < kBossRetakeMs)
            {
                o.hold_target = focus;
                ++retakeHeld;
            }
        ++votes[focus.GetCounter()];
        ++dps;
    }
    if (retakeHeld && now - g_retakeLogMs >= 3000)
    {
        g_retakeLogMs = now;
        TC_LOG_INFO("playerbot.v2.raid", "[raid_script] group={} map={} {} DPS bots hold at the tank's threat: the boss was on a non-tank in the last {} s",
                    plan.groupLow, plan.mapId, retakeHeld, kBossRetakeMs / 1000);
    }
    {
        size_t most = 0;
        uint64_t top = 0;
        for (auto const& [g, n] : votes)
            if (n > most || (n == most && g < top))
            {
                most = n;
                top = g;
            }
        for (Unit* enemy : mine)
            if (top && enemy->GetGUID().GetCounter() == top)
                chosen = enemy->GetGUID();
        if (forced && top == forced->GetGUID().GetCounter())
            chosen = forced->GetGUID();
    }

    if (chosen != st.focus)
    {
        st.focus = chosen;
        std::string name = "-";
        for (Unit* enemy : mine)
            if (enemy->GetGUID() == chosen)
                name = enemy->GetName();
        if (forced && forced->GetGUID() == chosen)
            name = forced->GetName() + " (timed kill)";
        TC_LOG_INFO("playerbot.v2", "[fight_script] group={} map={} {}: DPS focus -> {} ({}) owner mark {} dps bots {}", plan.groupLow, plan.mapId,
            fight->name, name, chosen.GetCounter(), ownerMark.IsEmpty() ? "none" : "set", dps);
    }
    if (wait && !st.startLogged)
    {
        st.startLogged = now;
        TC_LOG_INFO("playerbot.v2", "[fight_script] group={} map={} {}: fight starts, DPS wait {} ms", plan.groupLow, plan.mapId, fight->name,
            fight->startWaitMs);
    }
    dump << "  fight script " << fight->name << ": focus " << chosen.GetCounter() << (wait ? " (start wait)" : "") << "\n";
}

bool LateBossHeld(uint64_t enemyGuidLow)
{
    auto it = g_lateBoss.find(enemyGuidLow);
    return it != g_lateBoss.end() && !it->second.released;
}

} // namespace Playerbot::Raid::detail
