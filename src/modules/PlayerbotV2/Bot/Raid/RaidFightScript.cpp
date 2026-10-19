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

constexpr uint32_t kFightGapMs = 8000;   // no engaged enemy for this long: the next pull starts a new fight
constexpr uint8_t kSkullIcon = 7, kCrossIcon = 6;

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

    ObjectGuid chosen;   // what the DPS were given this pass (log)
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

        // Skull only: everything else of the fight, marked or sheeped or not, is off limits for this bot.
        if (fight->skullOnly)
            for (Unit* enemy : mine)
                if (enemy->GetGUID() != ownerMark)
                    o.forbidden_targets.push_back(enemy->GetGUID());

        std::vector<Script::Enemy> view;
        view.reserve(mine.size());
        for (Unit* enemy : mine)
        {
            Script::Enemy e;
            e.guid = enemy->GetGUID().GetCounter();
            e.entry = enemy->GetEntry();
            e.hpPct = enemy->GetHealthPct();
            bool const forbidden = std::find(o.forbidden_targets.begin(), o.forbidden_targets.end(), enemy->GetGUID()) != o.forbidden_targets.end();
            Creature const* c = enemy->ToCreature();
            e.eligible = enemy->IsAlive() && !forbidden && !(c && c->IsInEvadeMode());
            view.push_back(e);
        }
        uint64_t const pick = Script::PickFocus(*fight, view.data(), view.size(), ownerMark.IsEmpty() ? 0 : ownerMark.GetCounter(),
            st.focus.IsEmpty() ? 0 : st.focus.GetCounter());
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
        chosen = focus;
        ++dps;
    }

    if (chosen != st.focus)
    {
        st.focus = chosen;
        std::string name = "-";
        for (Unit* enemy : mine)
            if (enemy->GetGUID() == chosen)
                name = enemy->GetName();
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

} // namespace Playerbot::Raid::detail
