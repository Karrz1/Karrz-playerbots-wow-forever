// Scripted Temple of Ahn'Qiraj mechanics that the rule tables cannot express. See RaidBrainInternal.h.
//
//  * Viscidus (aq40/classic_boss_viscidus.cpp SpellHit): 200 frost spell hits freeze him (slowed at 100 and 150), then
//    physical hits shatter him into globs. Mages are elected to Frostbolt him and shamans to Frost Shock him while he
//    is not frozen; once frozen (Freeze 25937) the melee and hunters do the rest with their normal rotation.
//  * C'Thun's Eye, Dark Glare (aq40/classic_boss_cthun.cpp EnterDarkGlarePhase): the eye channels a beam straight
//    ahead and turns a full circle (Rotate 26009 one way, 26136 the other) for 38 s. A bot in the beam or just ahead
//    of it steps to the side the beam has already swept.
//  * C'Thun's stomach (classic_instance_temple_of_ahnqiraj.cpp): an eaten player stacks Digestive Acid 26476 and fights
//    the two Flesh Tentacles (15802). Bots inside attack the tentacles; once they are dead or the acid stacks up, the bot
//    walks onto the exit spot (area trigger 4033: a punt knocks it up into trigger 4034, which ports it out). A client
//    sends both triggers on entering them; a bot fires them through the raid order. Bots outside never target the
//    stomach tentacles.

#include "RaidBrainInternal.h"

#include "Creature.h"
#include "DB2Stores.h"
#include "Player.h"
#include "SpellAuras.h"

#include <cmath>
#include <list>

namespace Playerbot::Raid::detail {

namespace {

// ---------------------------------------------------------------------------
// Viscidus
// ---------------------------------------------------------------------------
constexpr uint32_t kViscidus = 15299;
constexpr uint32_t kViscidusFreeze = 25937;
Ranks const kFrostbolt  = { 116, 205, 837, 7322, 8406, 8407, 8408, 10179, 10180, 10181, 25304 };
Ranks const kFrostShock = { 8056, 8058, 10472, 10473 };

void ElectViscidusFrost(Election& e)
{
    Plan& plan = e.plan;
    Unit* viscidus = nullptr;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kViscidus && enemy->IsVisible())
            viscidus = enemy;
    if (!viscidus || viscidus->HasAura(kViscidusFreeze))
        return;
    uint32_t frosters = 0;
    for (Member& m : plan.members)
    {
        if (!m.bot || m.tank || e.busy.count(m.p->GetGUID().GetCounter()))
            continue;
        if (m.healer && plan.emergency)
            continue;
        uint32_t spell = 0;
        if (m.p->GetClass() == CLASS_MAGE)
            spell = KnownRank(m.p, kFrostbolt);
        else if (m.p->GetClass() == CLASS_SHAMAN && !m.healer)
            spell = KnownRank(m.p, kFrostShock);
        if (!spell || !Ready(m.p, spell, viscidus, true))
            continue;
        RaidOrder* o = e.OrderOf(m.p);
        if (!o)
            continue;
        o->cast_kind = RaidOrder::CastKind::Nuke;
        o->cast_spell = spell;
        o->cast_target = viscidus->GetGUID();
        e.busy.insert(m.p->GetGUID().GetCounter());
        ++frosters;
    }
    if (frosters)
        e.dump << "  Viscidus: " << frosters << " bots on frost damage\n";
}

// ---------------------------------------------------------------------------
// C'Thun
// ---------------------------------------------------------------------------
constexpr uint32_t kEyeOfCthun     = 15589;
constexpr uint32_t kRotatePositive = 26009;
constexpr uint32_t kRotateNegative = 26136;
constexpr float    kBeamHalfAngle  = 0.35f;   // ~20 degrees either side of the eye's facing
constexpr float    kBeamLead       = 0.9f;    // ~50 degrees ahead: about 5 s of warning at 360 degrees per 38 s
constexpr float    kBeamClear      = 0.6f;    // stand this far behind the beam's trailing edge
constexpr uint32_t kDigestiveAcid  = 26476;
constexpr uint32_t kFleshTentacle  = 15802;
constexpr uint32_t kStomachGround  = 4033;    // AREATRIGGER_STOMACH_GROUND: starts the punt
constexpr uint32_t kStomachAir     = 4034;    // AREATRIGGER_STOMACH_AIR: ports out
constexpr uint32_t kAcidLeave      = 8;       // leave the stomach at this many acid stacks

float Normalize(float a)
{
    while (a > float(M_PI)) a -= 2.0f * float(M_PI);
    while (a <= -float(M_PI)) a += 2.0f * float(M_PI);
    return a;
}

bool InStomach(Player* p) { return p->HasAura(kDigestiveAcid) && p->GetPositionZ() < 0.0f; }

bool GlareMove(Plan const& plan, Member const& self, RaidOrder& out)
{
    Unit* eye = nullptr;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kEyeOfCthun)
            eye = enemy;
    if (!eye)
        return false;
    float dir = 0.0f;
    if (eye->HasAura(kRotatePositive))
        dir = 1.0f;
    else if (eye->HasAura(kRotateNegative))
        dir = -1.0f;
    else
        return false;
    Player* bot = self.p;
    float const facing = eye->GetOrientation();
    // Angle of the bot from the beam, positive in the direction the beam turns.
    float const rel = dir * Normalize(eye->GetAbsoluteAngle(bot) - facing);
    if (rel < -kBeamHalfAngle || rel > kBeamLead)
        return false;
    float const dist = std::max(eye->GetExactDist2d(bot), 8.0f);
    for (float extra : { 0.0f, 0.3f, 0.6f })
    {
        float const angle = facing - dir * (kBeamHalfAngle + kBeamClear + extra);
        float const x = eye->GetPositionX() + dist * std::cos(angle);
        float const y = eye->GetPositionY() + dist * std::sin(angle);
        float z = 0.0f;
        if (Usable(bot, x, y, bot->GetPositionZ(), z, /*urgent*/ true))
        {
            out.hold_melee = !self.ranged && !self.healer;
            return SetMove(out, x, y, z, "c'thun: out of dark glare");
        }
    }
    return false;
}

bool StomachMove(Plan const& /*plan*/, Member const& self, RaidOrder& out)
{
    Player* bot = self.p;
    if (!bot->HasAura(kDigestiveAcid))
        return false;
    // Knocked up into the exit trigger: fire it.
    if (AreaTriggerEntry const* air = sAreaTriggerStore.LookupEntry(kStomachAir))
        if (bot->IsInAreaTrigger(air))
        {
            out.area_trigger = kStomachAir;
            return true;
        }
    if (!InStomach(bot))
        return false;
    std::list<Creature*> tentacles;
    bot->GetCreatureListWithEntryInGrid(tentacles, kFleshTentacle, 60.0f);
    bool tentacleAlive = false;
    for (Creature* t : tentacles)
        if (t->IsAlive())
            tentacleAlive = true;
    if (tentacleAlive && Stacks(bot, kDigestiveAcid) < kAcidLeave)
        return false;   // keep fighting the tentacles (ElectStomach points the bot at them)
    AreaTriggerEntry const* ground = sAreaTriggerStore.LookupEntry(kStomachGround);
    if (!ground)
        return false;
    if (bot->IsInAreaTrigger(ground))
    {
        out.area_trigger = kStomachGround;
        return true;   // hold here until the punt
    }
    float z = 0.0f;
    if (!Usable(bot, ground->Pos.X, ground->Pos.Y, ground->Pos.Z, z, /*urgent*/ true))
        z = ground->Pos.Z;
    return SetMove(out, ground->Pos.X, ground->Pos.Y, z, "c'thun: leave the stomach");
}

// Inside: attack the nearest flesh tentacle. Outside: never the stomach tentacles.
void ElectStomach(Election& e)
{
    Plan& plan = e.plan;
    std::vector<Unit*> tentacles;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kFleshTentacle)
            tentacles.push_back(enemy);
    if (tentacles.empty())
        return;
    for (Member const& m : plan.members)
    {
        if (!m.bot)
            continue;
        RaidOrder* o = e.OrderOf(m.p);
        if (!o)
            continue;
        if (!InStomach(m.p))
        {
            for (Unit* t : tentacles)
                o->forbidden_targets.push_back(t->GetGUID());
            continue;
        }
        Unit* nearest = nullptr;
        for (Unit* t : tentacles)
            if (!nearest || m.p->GetDistance(t) < m.p->GetDistance(nearest))
                nearest = t;
        if (nearest && !o->move)
            o->attack_target = nearest->GetGUID();
    }
}

} // namespace

bool Aq40MechanicsMove(Plan const& plan, Member const& self, uint32_t /*now*/, RaidOrder& out)
{
    return StomachMove(plan, self, out) || GlareMove(plan, self, out);
}

void Aq40MechanicsElect(Election& e, uint32_t /*now*/)
{
    ElectViscidusFrost(e);
    ElectStomach(e);
}

} // namespace Playerbot::Raid::detail
