// Scripted Naxxramas mechanics that the rule tables cannot express. See RaidBrainInternal.h.
//
//  * Heigan the Unclean: the eruption dance. The room's four sections erupt in turn
//    (naxx/classic_boss_heigan.cpp UpdateEruption): eruption number k spares section k % 6 and section 6 - k % 6, so the
//    safe section runs 0, 1, 2, 3, 2, 1, 0, ... The counter restarts at the pull, when the dance starts and when it ends.
//    Each eruption summons a short-lived plague wave creature at (2773, -3684, 292); the brain counts those, so the bots
//    always stand in the section the NEXT eruption spares. Sections come from the trap game objects, classified exactly
//    as the instance script does (classic_instance_naxxramas.cpp OnObjectCreate).
//  * Sapphiron: in the air phase the raid spreads (Icebolt splashes and freezes its target into an ice block); when the
//    Frost Breath starts, everybody hides behind the nearest ice block, on the far side from Sapphiron.
//  * Instructor Razuvious: a priest bot mind-controls a Deathknight Understudy, which taunts Razuvious and tanks him
//    (Shield Wall when low). A second priest takes over before the first control runs out. The raid's own tanks leave
//    Razuvious alone while an understudy holds him and keep the free understudies instead; DPS never kill understudies.
//  * Grand Widow Faerlina: after each Enrage a priest mind-controls a Worshipper and sacrifices it (Widow's Embrace).
//  * Gothik the Harvester: he resets unless 10+ living players stand on each side of the combat gate during his speech
//    (naxx/classic_boss_gothik.cpp HasLessPlayersPerSide), and the gate closes at the pull. So before the pull, while a
//    raid member is in his room and the gate is open, the raid splits: tanks, healers and DPS alternate between the live
//    side (y <= gate y, where his waves spawn) and the dead side; humans count on whatever side they stand. In the fight
//    the gate stays closed until he opens it, so every bot only fights enemies on its own side.

#include "RaidBrainInternal.h"
#include "SpellFamily.h"

#include "Creature.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Log.h"
#include "Map.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace Playerbot::Raid::detail {

namespace {

// ---------------------------------------------------------------------------
// Heigan the Unclean
// ---------------------------------------------------------------------------
constexpr uint32_t kHeigan          = 15936;
constexpr uint32_t kHeiganWave      = 17293;    // NPC_HEIGAN_PLAGUE_WAVE
constexpr float    kEruptMarkerX    = 2773.0f;  // UpdateEruption's per-eruption summon
constexpr float    kEruptMarkerY    = -3684.0f;
constexpr float    kInField         = 9.0f;     // closer than this to a trap = standing in the eruption field
constexpr float    kSafeMargin      = 1.5f;     // a spot is safe when the nearest erupting trap is this much farther than the nearest safe one
constexpr uint32_t kNaxxGoSpawnBase = 30000000; // ClassicNaxxGameObjectSpawnId (classic_instance_naxxramas.cpp)
constexpr float    kManaBurnRange   = 28.0f;    // Mana Burn 29310, cast every 3 s while a mana user is this close (fight phase)

struct Trap { float x, y, z; uint8_t section; };

struct HeiganState
{
    bool engaged = false;
    bool dance = false;
    uint32_t eruptions = 0;
    std::vector<ObjectGuid> markers;   // eruption markers already counted
    uint32_t updatedAt = 0;
};

std::unordered_map<uint32_t, std::vector<Trap>> g_heiganTraps;   // instance id -> traps
std::unordered_map<uint32_t, HeiganState> g_heigan;              // instance id -> dance state

// Section of a trap by entry, then the spawn ids the instance script adds explicitly (a trap can be in two sections).
void ClassifyTrap(GameObject* go, std::vector<Trap>& out)
{
    uint32_t const e = go->GetEntry();
    auto add = [&](uint8_t section) { out.push_back({ go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), section }); };
    if ((e >= 181517 && e <= 181524) || e == 181678)
        add(0);
    else if ((e >= 181510 && e <= 181516) || (e >= 181525 && e <= 181531) || e == 181533 || e == 181676)
        add(1);
    else if ((e >= 181534 && e <= 181544) || e == 181532 || e == 181677)
        add(2);
    else if (e >= 181545 && e <= 181552 && go->GetSpawnId() != kNaxxGoSpawnBase + 533119 && go->GetSpawnId() != kNaxxGoSpawnBase + 533123)
        add(3);
    if (go->GetSpawnId() >= kNaxxGoSpawnBase)
        switch (go->GetSpawnId() - kNaxxGoSpawnBase)
        {
            case 533181: case 533182: case 533183: case 533184: case 533187: case 533188: case 533189: case 533190:
            case 533191: case 533192: case 533193: case 533194: case 533195: case 533197: case 533199: case 533200:
                add(3);
                break;
            case 533185: case 533196: case 533198:
                add(2);
                break;
            default:
                break;
        }
}

std::vector<Trap> const* HeiganTraps(Unit* heigan)
{
    uint32_t const instance = heigan->GetMap()->GetInstanceId();
    auto it = g_heiganTraps.find(instance);
    if (it != g_heiganTraps.end())
        return &it->second;
    std::vector<Trap> traps;
    std::list<GameObject*> objects;
    for (uint32_t entry = 181510; entry <= 181552; ++entry)
        heigan->GetGameObjectListWithEntryInGrid(objects, entry, 120.0f);
    for (uint32_t entry = 181676; entry <= 181678; ++entry)
        heigan->GetGameObjectListWithEntryInGrid(objects, entry, 120.0f);
    for (GameObject* go : objects)
        if (go->GetGoType() == GAMEOBJECT_TYPE_TRAP)
            ClassifyTrap(go, traps);
    // Only cache a complete room (grids around the platform may not be loaded yet); 76 traps in VMaNGOS' data.
    if (traps.size() < 40)
        return nullptr;
    TC_LOG_INFO("playerbot.v2.raid", "[raid] Heigan: {} eruption traps found", traps.size());
    return &(g_heiganTraps[instance] = std::move(traps));
}

void UpdateHeigan(Unit* heigan, uint32_t now)
{
    HeiganState& st = g_heigan[heigan->GetMap()->GetInstanceId()];
    if (st.updatedAt == now)
        return;
    st.updatedAt = now;
    bool const dance = heigan->ToCreature() && heigan->ToCreature()->HasReactState(REACT_PASSIVE);
    if (!st.engaged || dance != st.dance)
    {
        st.engaged = true;
        st.dance = dance;
        st.eruptions = 0;   // the script restarts the eruption counter at the pull and at both phase changes
    }
    std::list<Creature*> waves;
    heigan->GetCreatureListWithEntryInGrid(waves, kHeiganWave, 150.0f);
    for (Creature* wave : waves)
    {
        if (Dist2D(wave->GetPositionX(), wave->GetPositionY(), kEruptMarkerX, kEruptMarkerY) > 2.0f)
            continue;
        if (std::find(st.markers.begin(), st.markers.end(), wave->GetGUID()) != st.markers.end())
            continue;
        st.markers.push_back(wave->GetGUID());
        if (st.markers.size() > 16)
            st.markers.erase(st.markers.begin());
        ++st.eruptions;
    }
}

bool SafeSection(uint32_t eruption, uint8_t section)
{
    uint32_t const k = eruption % 6;
    return section == k || section == 6 - k;
}

// Distance to the nearest trap that erupts next / that does not.
void TrapDistances(std::vector<Trap> const& traps, uint32_t eruption, float x, float y, float& unsafe, float& safe)
{
    unsafe = safe = 9999.0f;
    for (Trap const& t : traps)
    {
        float const d = Dist2D(x, y, t.x, t.y);
        if (SafeSection(eruption, t.section))
            safe = std::min(safe, d);
        else
            unsafe = std::min(unsafe, d);
    }
}

bool HeiganMove(Plan const& plan, Member const& self, uint32_t now, RaidOrder& out)
{
    Unit* heigan = nullptr;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kHeigan)
            heigan = enemy;
    if (!heigan)
    {
        if (!plan.members.empty())
            g_heigan.erase(plan.members.front().p->GetMap()->GetInstanceId());
        return false;
    }
    std::vector<Trap> const* traps = HeiganTraps(heigan);
    if (!traps)
        return false;
    UpdateHeigan(heigan, now);
    HeiganState const& st = g_heigan[heigan->GetMap()->GetInstanceId()];

    Player* bot = self.p;
    float unsafe, safe;
    TrapDistances(*traps, st.eruptions, bot->GetPositionX(), bot->GetPositionY(), unsafe, safe);
    bool const inField = std::min(unsafe, safe) < kInField;
    bool const burnable = self.mana && !self.tank && (self.ranged || self.healer);
    // Fight phase: only whoever stands in the field dances (the ranged on the platform keep casting). Dance phase: everybody.
    if (!st.dance && !inField)
    {
        // Mana users on the platform stay out of Mana Burn range, on a spot outside the eruption field.
        if (!burnable || bot->GetExactDist2d(heigan) >= kManaBurnRange)
            return false;
        float const away = heigan->GetAbsoluteAngle(bot);
        for (float offset : { 0.0f, 0.35f, -0.35f, 0.7f, -0.7f, 1.05f, -1.05f })
        {
            float const x = heigan->GetPositionX() + (kManaBurnRange + 3.0f) * std::cos(away + offset);
            float const y = heigan->GetPositionY() + (kManaBurnRange + 3.0f) * std::sin(away + offset);
            float u, s;
            TrapDistances(*traps, st.eruptions, x, y, u, s);
            float z = 0.0f;
            if (std::min(u, s) >= kInField && Usable(bot, x, y, bot->GetPositionZ(), z, /*urgent*/ true))
                return SetMove(out, x, y, z, "heigan: out of mana burn");
        }
        return false;
    }
    bool const safeHere = st.dance ? (inField && safe + kSafeMargin <= unsafe) : (unsafe >= kInField || safe + kSafeMargin <= unsafe);
    if (safeHere)
    {
        // Melee whose target stands where the next eruption hits wait instead of following it in.
        if (!self.tank && !self.healer && !self.ranged)
            if (Unit* victim = bot->GetVictim())
            {
                float vu, vs;
                TrapDistances(*traps, st.eruptions, victim->GetPositionX(), victim->GetPositionY(), vu, vs);
                if (std::min(vu, vs) < kInField && vs + kSafeMargin > vu)
                {
                    out.hold_melee = true;
                    return true;
                }
            }
        return false;
    }

    // The best spot in the safe section: deep inside it (far from erupting traps), not too far to walk.
    struct Candidate { float score; Trap const* trap; };
    std::vector<Candidate> candidates;
    for (Trap const& t : *traps)
    {
        if (!SafeSection(st.eruptions, t.section))
            continue;
        float u, s;
        TrapDistances(*traps, st.eruptions, t.x, t.y, u, s);
        if (u < kSafeMargin + 1.0f)
            continue;   // a trap on the border of two sections
        float const walk = Dist2D(bot->GetPositionX(), bot->GetPositionY(), t.x, t.y);
        // Mana users prefer spots out of Mana Burn range (fight phase only: in the dance he channels Plague Cloud instead).
        float const burn = (!st.dance && burnable && Dist2D(heigan->GetPositionX(), heigan->GetPositionY(), t.x, t.y) < kManaBurnRange) ? 8.0f : 0.0f;
        candidates.push_back({ std::min(u, 12.0f) - 0.25f * walk - burn, &t });
    }
    std::sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b) { return a.score > b.score; });
    int pathChecks = 0;
    for (Candidate const& c : candidates)
    {
        if (++pathChecks > 4)
            break;
        float z = 0.0f;
        if (Usable(bot, c.trap->x, c.trap->y, c.trap->z, z, /*urgent*/ true))
        {
            SetMove(out, c.trap->x, c.trap->y, z, st.dance ? "heigan dance" : "heigan eruption", /*urgent*/ true);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Sapphiron
// ---------------------------------------------------------------------------
constexpr uint32_t kSapphiron        = 15989;
constexpr uint32_t kIceBlock         = 181247;  // GO_SAPPH_ICEBLOCK
constexpr uint32_t kIcebolt          = 28522;   // the frozen player inside a block carries it
constexpr uint32_t kFrostBreath      = 28524;   // 7 s cast from the air
constexpr uint32_t kFrostBreathDummy = 30101;   // the falling ball, 0.5 s before the cast
constexpr float    kIceboltSpread    = 10.0f;
// The blocks standing when the raid starts to hide: his five Icebolts come 3.5 s apart, the breath's falling ball 1.5 s after the fifth and
// the 7 s cast 0.5 s after that (classic_boss_sapphiron.cpp RescheduleIcebolt, EVENT_SAPPH_FROST_BREATH_*). Hiding from the fourth block
// on gives 12.5 s instead of 7.5. naxx_sapphiron r23: 14 died to the first breath at 95 s, most of them still walking ("raid:move").
constexpr std::size_t kHideFromBlocks = 4;

std::unordered_map<uint64_t, uint32_t> g_airSpread;   // bot -> last air phase spread move

bool Casting(Unit* u, uint32_t spell)
{
    if (u->HasAura(spell))
        return true;
    for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
        if (Spell* cast = u->GetCurrentSpell(type))
            if (cast->GetSpellInfo() && cast->GetSpellInfo()->Id == spell)
                return true;
    return false;
}

bool SapphironMove(Plan const& plan, Member const& self, uint32_t now, RaidOrder& out)
{
    Unit* sapphiron = nullptr;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kSapphiron)
            sapphiron = enemy;
    if (!sapphiron || !sapphiron->IsHovering())
        return false;
    Player* bot = self.p;
    if (bot->HasAura(kIcebolt))
        return true;   // frozen inside a block

    std::list<GameObject*> blocks;
    sapphiron->GetGameObjectListWithEntryInGrid(blocks, kIceBlock, 150.0f);
    blocks.remove_if([](GameObject* b) { return !b->isSpawned(); });
    // From the breath's falling ball on, a cast under way is stopped for the walk; before it (the fourth block) it is let finish first.
    bool const breathing = Casting(sapphiron, kFrostBreath) || Casting(sapphiron, kFrostBreathDummy);
    if (breathing || blocks.size() >= kHideFromBlocks)
    {
        // Hide: stand right behind the nearest ice block, on the line from Sapphiron through the block.
        GameObject* nearest = nullptr;
        float nearestDist = 0.0f;
        for (GameObject* block : blocks)
        {
            if (!block->isSpawned())
                continue;
            float const d = Dist2D(bot->GetPositionX(), bot->GetPositionY(), block->GetPositionX(), block->GetPositionY());
            if (!nearest || d < nearestDist) { nearest = block; nearestDist = d; }
        }
        if (!nearest)
            return false;
        // Melee and tanks stay behind the block instead of chasing Sapphiron (casters may keep casting from cover).
        out.hold_melee = !self.ranged && !self.healer;
        // The spot behind the block is this bot's post against him: no step out for a line of sight to its target or its tank
        // ([raid_los] skips a bot with a post enemy), no formation walk and no clump spacing (both skip a bot at its post).
        // naxx_sapphiron r25 (combined 1118): Mclocka hid, stepped 16 y toward its tank for sight 7 s before the breath and died with six
        // others at (3491,-5272); Mchealsd walked back to its formation slot between hide orders and died on its way.
        out.post_enemy = sapphiron->GetGUID();
        // The breath reaches whoever he sees (an area spell checks the line from him, high in the air, to each target; the block is a
        // game object with a model in that check). A spot counts as hidden when the same check from him is blocked: a fixed 2.5-4 y
        // behind the block left the line over its top. naxx22 1116: 14 died to the first breath, the melee among them holding behind
        // blocks 20 y from him.
        // The test is the one the spell makes (Spell::CheckEffectTarget for an area spell: the target's IsWithinLOS to the cast's source
        // point): from the player's eye to his position raised by the player's own height, M2 models left out. naxx_sapphiron r27: five
        // bots stood on one spot this check called hidden, and four hunters on another, and the breath killed all nine there; the old
        // line ran from the top of him and counted M2 models.
        Map* const map = bot->GetMap();
        float const eye = bot->GetCollisionHeight();
        float const sx = sapphiron->GetPositionX(), sy = sapphiron->GetPositionY(), sz = sapphiron->GetPositionZ() + eye;
        auto hidden = [&](float px, float py, float pz)
        {
            return !map->isInLineOfSight(bot->GetPhaseShift(), px, py, pz + eye, sx, sy, sz, LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::M2);
        };
        if (hidden(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
        {
            out.at_post = true;
            return true;   // already hidden: stays
        }
        float const away = std::atan2(nearest->GetPositionY() - sy, nearest->GetPositionX() - sx);
        int pathChecks = 0;
        for (float distance : { 1.5f, 2.0f, 2.5f, 3.0f, 4.0f })
            for (float side : { 0.0f, 0.15f, -0.15f })
            {
                float const px = nearest->GetPositionX() + distance * std::cos(away + side);
                float const py = nearest->GetPositionY() + distance * std::sin(away + side);
                float const gz = map->GetHeight(bot->GetPhaseShift(), px, py, nearest->GetPositionZ() + 2.0f, true, 6.0f);
                if (gz <= INVALID_HEIGHT + 1.0f || !hidden(px, py, gz))
                    continue;
                if (++pathChecks > 4)
                    return true;
                float pz = 0.0f;
                if (!Usable(bot, px, py, gz, pz, /*urgent*/ true))
                    continue;
                SetMove(out, px, py, pz, "hide behind ice block", breathing);
                return true;
            }
        // No spot behind the nearest block is out of his sight: said once per bot and breath.
        uint32_t& logged = g_airSpread[bot->GetGUID().GetCounter() | (uint64_t(1) << 63)];
        if (now - logged >= 10000)
        {
            logged = now;
            TC_LOG_INFO("playerbot.v2.raid", "[raid] Sapphiron: {} finds no spot behind the ice block at {:.1f},{:.1f},{:.1f} out of his sight "
                        "(he is at {:.1f},{:.1f},{:.1f}, {:.0f} y off)", bot->GetName(), nearest->GetPositionX(), nearest->GetPositionY(),
                        nearest->GetPositionZ(), sx, sy, sz, Dist2D(sx, sy, nearest->GetPositionX(), nearest->GetPositionY()));
        }
        // Then right behind it all the same, on the line from him through the block.
        float const px = nearest->GetPositionX() + 2.0f * std::cos(away), py = nearest->GetPositionY() + 2.0f * std::sin(away);
        float pz = 0.0f;
        if (Dist2D(bot->GetPositionX(), bot->GetPositionY(), px, py) >= 1.2f && Usable(bot, px, py, nearest->GetPositionZ(), pz, /*urgent*/ true))
            SetMove(out, px, py, pz, "hide behind ice block", breathing);
        return true;
    }

    // Icebolts: keep apart so a splash or a block does not take two players.
    std::vector<Player*> others;
    float nearestRaider = 9999.0f;
    for (Member const& m : plan.members)
        if (m.p != bot)
        {
            others.push_back(m.p);
            nearestRaider = std::min(nearestRaider, Dist2D(bot->GetPositionX(), bot->GetPositionY(), m.p->GetPositionX(), m.p->GetPositionY()));
        }
    if (others.empty() || nearestRaider >= kIceboltSpread * 0.85f)
        return false;
    uint32_t& at = g_airSpread[bot->GetGUID().GetCounter()];
    if (now - at < 3000)
        return false;
    if (Clearest(bot, others, kIceboltSpread, out, "air phase spread", {}, /*urgent*/ true))
    {
        at = now;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Instructor Razuvious
// ---------------------------------------------------------------------------
constexpr uint32_t kRazuvious   = 16061;
constexpr uint32_t kUnderstudy  = 16803;
constexpr uint32_t kUsTaunt     = 29060;   // Deathknight Understudy: Taunt
constexpr uint32_t kUsShieldWall= 29061;   // Deathknight Understudy: Shield Wall (same id the Horsemen use)
Ranks const kMindControl = { 605, 10911, 10912 };

Creature* Possessed(Player* p, uint32_t entry = kUnderstudy)
{
    Unit* charmed = p->GetCharmed();
    if (!charmed || !charmed->IsAlive() || charmed->GetEntry() != entry)
        return nullptr;
    return charmed->ToCreature();
}

bool CreatureSpellReady(Creature* c, uint32_t spell)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spell, DIFFICULTY_NONE);
    return info && c->GetSpellHistory()->IsReady(info);
}

// The Mind Control a priest is casting right now (0 = none).
uint32_t CastingMindControl(Player* p, ObjectGuid& target)
{
    if (Spell* cast = p->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        if (cast->GetSpellInfo() && std::find(kMindControl.begin(), kMindControl.end(), cast->GetSpellInfo()->Id) != kMindControl.end())
        {
            if (ObjectGuid const guid = cast->m_targets.GetUnitTargetGUID(); !guid.IsEmpty())
                target = guid;   // by GUID: the spell's object pointer can dangle between map updates
            return cast->GetSpellInfo()->Id;
        }
    return 0;
}

void ElectRazuvious(Election& e)
{
    Plan& plan = e.plan;
    Unit* raz = nullptr;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kRazuvious)
            raz = enemy;
    if (!raz)
        return;

    std::list<Creature*> understudies;
    raz->GetCreatureListWithEntryInGrid(understudies, kUnderstudy, 80.0f);

    // 1. Controllers command their understudy.
    int holding = 0;            // controllers with more than 12 s of control left
    bool pending = false;       // a Mind Control is being cast
    for (Member& m : plan.members)
    {
        if (!m.bot)
            continue;
        RaidOrder* o = e.OrderOf(m.p);
        if (!o)
            continue;
        ObjectGuid mcTarget;
        if (uint32_t const casting = CastingMindControl(m.p, mcTarget))
        {
            // Keep the cast job so the consumer waits for it instead of letting the rotation cast over it.
            o->move = false;
            o->cast_kind = RaidOrder::CastKind::CrowdControl;
            o->cast_spell = casting;
            o->cast_target = mcTarget;
            e.busy.insert(m.p->GetGUID().GetCounter());
            pending = true;
            continue;
        }
        Creature* us = Possessed(m.p);
        if (!us)
            continue;
        e.busy.insert(m.p->GetGUID().GetCounter());
        o->move = false;   // moving breaks the channel
        o->cast_kind = RaidOrder::CastKind::None;
        o->cast_spell = 0;
        int32_t left = 0;
        for (uint32_t id : kMindControl)
            if (Aura* aura = us->GetAura(id, m.p->GetGUID()))
                left = std::max(left, aura->GetDuration());
        if (left < 0 || left > 12000)   // a negative duration is permanent
            ++holding;
        bool const tanking = raz->GetVictim() == us;
        if (!tanking && us->GetDistance(raz) <= 8.0f && CreatureSpellReady(us, kUsTaunt))
        {
            o->charm_spell = kUsTaunt;
            o->charm_target = raz->GetGUID();
        }
        else if (tanking && us->GetHealthPct() < 60.0f && !us->HasAura(kUsShieldWall) && CreatureSpellReady(us, kUsShieldWall))
            o->charm_spell = kUsShieldWall;
        else
            o->charm_attack = raz->GetGUID();
        e.dump << "  " << m.p->GetName() << ": controls an understudy (" << left / 1000 << " s left)"
               << (tanking ? ", tanking" : "") << "\n";
    }

    // 2. While an understudy holds Razuvious the raid's tanks leave him alone and keep the free understudies.
    bool understudyTanks = false;
    for (Member const& m : plan.members)
        if (Creature* us = Possessed(m.p))
            if (raz->GetVictim() == us)
                understudyTanks = true;
    if (understudyTanks)
        for (Member const& m : plan.members)
        {
            if (!m.bot || !m.tank)
                continue;
            RaidOrder* o = e.OrderOf(m.p);
            if (!o)
                continue;
            o->forbidden_targets.push_back(raz->GetGUID());
            Creature* best = nullptr;
            for (Creature* us : understudies)
                if (us->IsAlive() && !us->IsCharmed() && (!best || m.p->GetDistance(us) < m.p->GetDistance(best)))
                    best = us;
            if (best)
                o->fallback_target = best->GetGUID();
        }

    // 3. A new Mind Control when nobody holds one with time to spare (the next priest takes over before the first runs out).
    if (holding > 0 || pending)
        return;
    Creature* target = nullptr;
    for (Creature* us : understudies)
        if (us->IsAlive() && !us->IsCharmed() && (!target || us->GetDistance(raz) < target->GetDistance(raz)))
            target = us;
    if (target)
        e.Assign(target, { &kMindControl }, true, RaidOrder::CastKind::CrowdControl, "mind control", false);
}

// ---------------------------------------------------------------------------
// Grand Widow Faerlina
// ---------------------------------------------------------------------------
constexpr uint32_t kFaerlina      = 15953;
constexpr uint32_t kWorshipper    = 16506;
constexpr uint32_t kFaerlinaRage  = 28798;   // Enrage, every 60 s
constexpr uint32_t kWidowsEmbrace = 28732;   // removes the enrage and blocks the next one and her volley for 30 s

// Faerlina: once she enrages, a priest mind-controls a Naxxramas Worshipper and makes it cast Widow's Embrace on her
// (naxx/classic_boss_faerlina.cpp SpellHit; the worshipper has no bot logic of its own, the spell is its template spell).
// Sacrificing after the enrage, not before, buys the full minute.
void ElectFaerlina(Election& e)
{
    Plan& plan = e.plan;
    Unit* faerlina = nullptr;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kFaerlina)
            faerlina = enemy;
    if (!faerlina)
        return;
    bool const enraged = faerlina->HasAura(kFaerlinaRage) && !faerlina->HasAura(kWidowsEmbrace);

    bool controlling = false;
    for (Member& m : plan.members)
    {
        if (!m.bot)
            continue;
        RaidOrder* o = e.OrderOf(m.p);
        if (!o)
            continue;
        ObjectGuid mcTarget;
        if (uint32_t const casting = CastingMindControl(m.p, mcTarget))
        {
            o->move = false;
            o->cast_kind = RaidOrder::CastKind::CrowdControl;
            o->cast_spell = casting;
            o->cast_target = mcTarget;
            e.busy.insert(m.p->GetGUID().GetCounter());
            controlling = true;
            continue;
        }
        Creature* worshipper = Possessed(m.p, kWorshipper);
        if (!worshipper)
            continue;
        controlling = true;
        e.busy.insert(m.p->GetGUID().GetCounter());
        o->move = false;   // moving breaks the channel
        o->cast_kind = RaidOrder::CastKind::None;
        o->cast_spell = 0;
        if (!enraged)
            o->charm_hold = true;
        else if (worshipper->GetDistance(faerlina) > 8.0f)
            o->charm_attack = faerlina->GetGUID();   // walk it over
        else
        {
            o->charm_spell = kWidowsEmbrace;
            o->charm_target = faerlina->GetGUID();
        }
        e.dump << "  " << m.p->GetName() << ": controls a worshipper" << (enraged ? ", sacrificing it" : "") << "\n";
    }
    if (controlling || !enraged)
        return;

    std::list<Creature*> worshippers;
    faerlina->GetCreatureListWithEntryInGrid(worshippers, kWorshipper, 60.0f);
    Creature* target = nullptr;
    for (Creature* w : worshippers)
        if (w->IsAlive() && !w->IsCharmed() && (!target || w->GetDistance(faerlina) < target->GetDistance(faerlina)))
            target = w;
    if (target)
        e.Assign(target, { &kMindControl }, true, RaidOrder::CastKind::CrowdControl, "mind control", false);
}

// ---------------------------------------------------------------------------
// Gothik the Harvester
// ---------------------------------------------------------------------------
constexpr uint32_t kGothik      = 16060;
constexpr uint32_t kCombatGate  = 181170;   // GO_MILI_GOTH_COMBAT_GATE, spawned at (2692.07, -3360.78, 267.5)
constexpr float    kGothRoom    = 60.0f;    // a member this close to the gate is in his room
constexpr uint32_t kSideMinimum = 10;       // HasLessPlayersPerSide(10)
constexpr uint32_t kGothImmune  = 29230;    // on the balcony (speech and waves) he takes no damage

GameObject* GothikGate(Player* anyone)
{
    std::list<GameObject*> gates;
    anyone->GetGameObjectListWithEntryInGrid(gates, kCombatGate, 120.0f);
    return gates.empty() ? nullptr : gates.front();
}

bool LiveSide(GameObject* gate, float y) { return y <= gate->GetPositionY(); }   // IsInRightSideGothArea

struct GothSpot { uint32_t instance = 0; bool live = false; float x = 0, y = 0, z = 0; };
std::unordered_map<uint64_t, GothSpot> g_gothSpot;                  // bot -> its pre-pull spot
std::unordered_map<uint64_t, std::pair<bool, Position>> g_gothAnchor; // (instance << 1 | side) -> anchor found?
std::unordered_set<uint32_t> g_gothOffFloorLogged;                    // instances whose off-the-floor raid was logged
std::unordered_map<uint32_t, std::pair<bool, Position>> g_gothDraw;   // instance -> the spot under his balcony that draws him, found?
constexpr float kGothDrawMargin = 3.0f;   // the draw spot stands this far inside his aggro reach (or the opener's range)
// When no floor spot lies inside his aggro reach, a ranged bot on the live side opens on him with one shot from the floor. His reach is
// 15 y less his combat reach for a creature above the expansion's top level (Creature::GetAttackDistance), and he stands 18 y above the
// floor: naxx_gothik r25 (combined 1118): "his aggro reach 9.0 y ... nobody can draw him", the raid stood split until the run failed.
struct GothShot { bool searched = false, found = false; ObjectGuid shooter; Position spot; };
std::unordered_map<uint32_t, GothShot> g_gothShot;   // instance -> its ranged opener
// The draw waits for the split, but not for a straggler: once this long has passed since the split was first ordered, a bot that has not
// reached its spot no longer holds it back. naxx_gothik r24h (0045): the raid stood split for 120 s and nobody drew him, two healers 43-46 y
// from the tanks; one bot short of its spot held the whole pull.
constexpr uint32_t kGothDrawWaitMs = 20000;
std::unordered_map<uint32_t, uint32_t> g_gothSplitSince;   // instance -> when its split was first ordered

// A floor point on one side, reachable on foot (the gate is open before the pull, and the navmesh ignores it anyway).
bool GothAnchor(Player* bot, GameObject* gate, bool live, Position& out)
{
    uint64_t const key = (uint64_t(bot->GetInstanceId()) << 1) | (live ? 1 : 0);
    auto it = g_gothAnchor.find(key);
    if (it != g_gothAnchor.end())
    {
        out = it->second.second;
        return it->second.first;
    }
    float const sign = live ? -1.0f : 1.0f;
    for (float depth : { 18.0f, 14.0f, 24.0f, 10.0f })
        for (float along : { 0.0f, 8.0f, -8.0f, 16.0f, -16.0f })
        {
            float const x = gate->GetPositionX() + along;
            float const y = gate->GetPositionY() + sign * depth;
            float const z = bot->GetMap()->GetHeight(bot->GetPhaseShift(), x, y, gate->GetPositionZ() + 2.0f, true, 8.0f);
            if (z <= INVALID_HEIGHT + 1.0f || std::fabs(z - gate->GetPositionZ()) > 4.0f)
                continue;
            PathGenerator path(bot);
            if (!path.CalculatePath(x, y, z, false) || (path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE)))
                continue;
            out.Relocate(x, y, z);
            g_gothAnchor[key] = { true, out };
            return true;
        }
    g_gothAnchor[key] = { false, Position() };
    TC_LOG_WARN("playerbot.v2.raid", "[raid] Gothik: no reachable {} side spot near the combat gate", live ? "live" : "dead");
    return false;
}

// A spot on the live side's floor within `want` yards of him, in his sight, that `bot` walks to on a complete path (the nearest bearing to
// the live side's anchor first, the farthest distance first).
bool GothFloorSpot(Creature* gothik, GameObject* gate, Player* bot, float want, Position& out)
{
    float const dz = gothik->GetPositionZ() - gate->GetPositionZ();
    if (want <= std::fabs(dz) + 1.0f)
        return false;
    float const h = std::sqrt(want * want - dz * dz);
    Position anchor;
    float const toward = GothAnchor(bot, gate, true, anchor) ? gothik->GetAbsoluteAngle(anchor.GetPositionX(), anchor.GetPositionY())
                                                             : gothik->GetAbsoluteAngle(bot);
    for (float r = h; r >= 2.0f; r -= 2.0f)
        for (int step = 0; step <= 8; ++step)
            for (int sign : { 1, -1 })
            {
                if (step == 0 && sign < 0)
                    continue;
                float const a = toward + float(sign * step) * float(M_PI) / 16.0f;
                float const x = gothik->GetPositionX() + r * std::cos(a);
                float const y = gothik->GetPositionY() + r * std::sin(a);
                if (!LiveSide(gate, y))
                    continue;
                float const z = bot->GetMap()->GetHeight(bot->GetPhaseShift(), x, y, gate->GetPositionZ() + 2.0f, true, 8.0f);
                if (z <= INVALID_HEIGHT + 1.0f || std::fabs(z - gate->GetPositionZ()) > 4.0f)
                    continue;
                if (gothik->GetExactDist(x, y, z) > want || !gothik->IsWithinLOS(x, y, z + 2.0f))
                    continue;
                PathGenerator path(bot);
                if (!path.CalculatePath(x, y, z, false) || (path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE)))
                    continue;
                out.Relocate(x, y, z);
                return true;
            }
    return false;
}

// One shot from the live side's floor starts the fight (JustEngagedWith: the zone joins in, he takes his immunity and the gate closes):
// a hunter, else a mage, else a warlock, on the live side. The shooter is kept while it can still shoot (a swap mid-walk sends a second
// bot to the spot and the first one back); its spot is inside its opener's range, in his sight. Its other order (the side spot) gives way.
void NaxxGothikRangedOpener(std::unordered_map<uint64_t, RaidOrder>& orders, std::vector<Member const*> const& ordered,
                            Creature* gothik, GameObject* gate, uint32_t instance, std::ostringstream& dump)
{
    GothShot& shot = g_gothShot[instance];
    auto const canShoot = [&](Member const* m, uint32_t& spell, int& order)
    {
        auto it = g_gothSpot.find(m->p->GetGUID().GetCounter());
        spell = RangedPullSpell(m->p, order);
        return m->bot && !m->tank && !m->healer && m->p->IsAlive() && spell && it != g_gothSpot.end() && it->second.instance == instance &&
               it->second.live;
    };
    Member const* shooter = nullptr;
    uint32_t spell = 0;
    int order = 3;
    for (Member const* m : ordered)
        if (m->p->GetGUID() == shot.shooter && canShoot(m, spell, order))
            shooter = m;
    if (!shooter)
    {
        int bestOrder = 3;
        float bestYards = 0.0f;
        for (Member const* m : ordered)
        {
            uint32_t s = 0;
            int o = 3;
            if (!canShoot(m, s, o))
                continue;
            float const yards = m->p->GetExactDist2d(gothik);
            if (!shooter || o < bestOrder || (o == bestOrder && yards < bestYards))
            {
                shooter = m;
                bestOrder = o;
                bestYards = yards;
            }
        }
        if (!shooter)
        {
            if (!shot.searched)
                TC_LOG_WARN("playerbot.v2.raid", "[raid] Gothik: no hunter, mage or warlock bot on the live side to open on him");
            shot.searched = true;
            return;
        }
        shot.shooter = shooter->p->GetGUID();
        shot.searched = false;   // a new shooter looks for its own spot
        canShoot(shooter, spell, order);
    }
    Player* const p = shooter->p;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spell, DIFFICULTY_NONE);
    if (!info)
        return;
    if (!shot.searched)
    {
        shot.searched = true;
        float const range = info->GetMaxRange(false, p);
        shot.found = GothFloorSpot(gothik, gate, p, range - kGothDrawMargin, shot.spot);
        if (shot.found)
            TC_LOG_INFO("playerbot.v2.raid", "[raid] Gothik: {} opens on him with {} from ({:.1f},{:.1f},{:.1f}), {:.1f} y from him (range {:.0f} y)",
                        p->GetName(), spell, shot.spot.GetPositionX(), shot.spot.GetPositionY(), shot.spot.GetPositionZ(),
                        gothik->GetExactDist(shot.spot), range);
        else
            TC_LOG_WARN("playerbot.v2.raid", "[raid] Gothik: no floor spot on the live side within {}'s range {:.0f} y of him: nobody can open",
                        p->GetName(), range);
    }
    if (!shot.found)
        return;
    RaidOrder& o = orders[p->GetGUID().GetCounter()];
    o.active = true;
    o.prepull = true;
    if (p->GetExactDist2d(shot.spot.GetPositionX(), shot.spot.GetPositionY()) > 2.0f || !Ready(p, spell, gothik, true))
    {
        SetMove(o, shot.spot.GetPositionX(), shot.spot.GetPositionY(), shot.spot.GetPositionZ(), "gothik: walks to the opener spot");
        dump << "  Gothik: " << p->GetName() << " walks to open on him\n";
        return;
    }
    o.move = false;
    o.cast_kind = RaidOrder::CastKind::Nuke;
    o.cast_spell = spell;
    o.cast_target = gothik->GetGUID();
    dump << "  Gothik: " << p->GetName() << " opens on him\n";
}

void NaxxPrePullGothik(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders, std::ostringstream& dump)
{
    Player* first = plan.members.front().p;
    GameObject* gate = GothikGate(first);
    if (!gate)
        return;
    Creature* gothik = gate->FindNearestCreature(kGothik, 120.0f, true);
    if (!gothik || gothik->IsInCombat() || gate->GetGoState() != GO_STATE_ACTIVE)
    {
        g_gothSplitSince.erase(first->GetInstanceId());   // the next split's wait starts afresh
        g_gothShot.erase(first->GetInstanceId());          // and the next opener is chosen afresh
        return;
    }
    bool inRoom = false;
    Player* above = nullptr;   // near the gate on the map but off its floor (his balcony's height): logged, the split waits for the floor
    for (Member const& m : plan.members)
    {
        if (m.p->GetExactDist2d(gate) >= kGothRoom)
            continue;
        if (std::fabs(m.p->GetPositionZ() - gate->GetPositionZ()) < 8.0f)
            inRoom = true;
        else if (!above)
            above = m.p;
    }
    if (!inRoom && above && g_gothOffFloorLogged.insert(first->GetInstanceId()).second)
        TC_LOG_WARN("playerbot.v2.raid", "[raid] Gothik: {} stands {:.0f} y from the combat gate at z {:.1f}, the gate's floor is z {:.1f}: no side split "
                    "off the floor", above->GetName(), above->GetExactDist2d(gate), above->GetPositionZ(), gate->GetPositionZ());
    if (!inRoom || plan.members.size() < 2 * kSideMinimum)
    {
        g_gothSplitSince.erase(first->GetInstanceId());
        return;
    }

    // Humans stay where they are and count for that side; bots alternate within each role so both sides get tanks,
    // healers and damage, starting with the side that has fewer humans.
    uint32_t live = 0, dead = 0;
    for (Member const& m : plan.members)
        if (!m.bot)
            ++(LiveSide(gate, m.p->GetPositionY()) ? live : dead);
    bool nextLive = live <= dead;
    std::vector<Member const*> ordered;
    for (int pass = 0; pass < 3; ++pass)
        for (Member const& m : plan.members)
            if (m.bot && (pass == 0 ? m.tank : pass == 1 ? m.healer : (!m.tank && !m.healer)))
                ordered.push_back(&m);
    uint32_t const instance = first->GetInstanceId();
    uint32_t index = 0;
    for (Member const* m : ordered)
    {
        bool const side = nextLive;
        nextLive = !nextLive;
        ++index;
        GothSpot& spot = g_gothSpot[m->p->GetGUID().GetCounter()];
        if (spot.instance != instance || spot.live != side)
        {
            Position anchor;
            if (!GothAnchor(m->p, gate, side, anchor))
                continue;
            // A small ring around the anchor, a different place per bot, on the floor.
            float const angle = float(index) * 2.4f;
            float const radius = 2.0f + float(index % 4) * 1.5f;
            float x = anchor.GetPositionX() + radius * std::cos(angle);
            float y = anchor.GetPositionY() + radius * std::sin(angle);
            float z = m->p->GetMap()->GetHeight(m->p->GetPhaseShift(), x, y, anchor.GetPositionZ() + 2.0f, true, 6.0f);
            if (z <= INVALID_HEIGHT + 1.0f || std::fabs(z - anchor.GetPositionZ()) > 3.0f || LiveSide(gate, y) != side)
            {
                x = anchor.GetPositionX();
                y = anchor.GetPositionY();
                z = anchor.GetPositionZ();
            }
            spot = { instance, side, x, y, z };
        }
        ++(side ? live : dead);
        RaidOrder& o = orders[m->p->GetGUID().GetCounter()];
        o.active = true;
        o.prepull = true;
        SetMove(o, spot.x, spot.y, spot.z, side ? "gothik: live side" : "gothik: dead side");
    }
    dump << "  Gothik split: " << live << " live side, " << dead << " dead side\n";

    // Once every bot stands at its spot, a tank on the live side draws him: he stands on his balcony 18 y above the floor and starts the
    // fight only when somebody comes into his aggro reach in his sight (no area trigger in the classic script), and nobody walks up to him.
    // naxx_gothik r23: the split worked, the attack order sent the main tank at him (no path up, his reach 49 y off), and the raid stood
    // in its pre-pull spots for 121 s until the run failed. The draw spot is on the live side's floor, inside his reach by kGothDrawMargin.
    uint32_t const nowMs = GameTime::GetGameTimeMS();
    uint32_t const since = g_gothSplitSince.emplace(instance, nowMs).first->second;
    std::vector<std::string> walking;
    auto const shotIt = g_gothShot.find(instance);
    for (Member const* m : ordered)
    {
        // The ranged opener's spot is its own: counted as walking to its side spot, it held the opener back every other pass and the
        // split order sent it back (naxx_gothik r26: Mchuntc swapped between "walks to the opener spot" and "live side" for 10 s).
        if (shotIt != g_gothShot.end() && shotIt->second.found && m->p->GetGUID() == shotIt->second.shooter)
            continue;
        auto it = g_gothSpot.find(m->p->GetGUID().GetCounter());
        if (it == g_gothSpot.end() || it->second.instance != instance || m->p->GetExactDist2d(it->second.x, it->second.y) > 3.0f)
            walking.push_back(fmt::format("{} {:.0f} y", m->p->GetName(), it == g_gothSpot.end() ? -1.0f : m->p->GetExactDist2d(it->second.x, it->second.y)));
    }
    if (!walking.empty())
    {
        if (nowMs - since < kGothDrawWaitMs)
            return;   // still walking
        static std::unordered_map<uint32_t, uint32_t> logged;
        uint32_t& at = logged[instance];
        if (!at || nowMs - at >= 15000u)
        {
            at = nowMs ? nowMs : 1u;
            std::string list;
            for (std::string const& w : walking)
                list += (list.empty() ? "" : ", ") + w;
            TC_LOG_INFO("playerbot.v2.raid", "[raid] Gothik: the split has stood {} s; the draw goes ahead with {} not at their spots: {}",
                        (nowMs - since) / 1000u, walking.size(), list);
        }
    }
    Member const* puller = nullptr;
    for (Member const* m : ordered)
    {
        if (!m->tank || !g_gothSpot[m->p->GetGUID().GetCounter()].live)
            continue;
        if (!puller || m->p == plan.mainTank)
            puller = m;
    }
    if (!puller)
    {
        NaxxGothikRangedOpener(orders, ordered, gothik, gate, instance, dump);   // no tank on the live side to draw him
        return;
    }
    Player* const tank = puller->p;
    auto drawIt = g_gothDraw.find(instance);
    if (drawIt == g_gothDraw.end())
    {
        float const reach = gothik->GetAttackDistance(tank) + gothik->GetCombatReach() + tank->GetCombatReach();
        float const dz = gothik->GetPositionZ() - gate->GetPositionZ();
        Position draw;
        bool const found = GothFloorSpot(gothik, gate, tank, reach - kGothDrawMargin, draw);
        drawIt = g_gothDraw.emplace(instance, std::make_pair(found, draw)).first;
        if (found)
            TC_LOG_INFO("playerbot.v2.raid", "[raid] Gothik: {} draws him from ({:.1f},{:.1f},{:.1f}), {:.1f} y from him on his balcony at "
                        "({:.1f},{:.1f},{:.1f}), his aggro reach {:.1f} y", tank->GetName(), draw.GetPositionX(), draw.GetPositionY(),
                        draw.GetPositionZ(), gothik->GetExactDist(draw), gothik->GetPositionX(), gothik->GetPositionY(), gothik->GetPositionZ(), reach);
        else
            TC_LOG_WARN("playerbot.v2.raid", "[raid] Gothik: no floor spot on the live side within his aggro reach {:.1f} y (him at "
                        "({:.1f},{:.1f},{:.1f}), {:.1f} y above the gate's floor): a ranged opener pulls him", reach, gothik->GetPositionX(),
                        gothik->GetPositionY(), gothik->GetPositionZ(), dz);
    }
    if (!drawIt->second.first)
    {
        NaxxGothikRangedOpener(orders, ordered, gothik, gate, instance, dump);
        return;
    }
    Position const& draw = drawIt->second.second;
    RaidOrder& o = orders[tank->GetGUID().GetCounter()];
    SetMove(o, draw.GetPositionX(), draw.GetPositionY(), draw.GetPositionZ(), "gothik: draws him from under his balcony");
    dump << "  Gothik: " << tank->GetName() << " draws him\n";
}

// In the fight each side keeps its raiders. He goes back out of combat (EnterEvadeMode) when either side of the combat gate has fewer
// than 10 living players at any of his speech checks, 5 s apart (classic_boss_gothik.cpp HasLessPlayersPerSide), and the gate stays shut
// until late in the fight. naxx_gothik r26: the opener pulled him, the out-of-combat split orders ended with the pull, the raid ran after
// its tanks across the gate line, and he reset 5 s later at 100%. So while he is on his balcony or the gate is shut, a bot on the wrong
// side or out of his room goes back to its spot; one with no enemy on its side near it waits at its spot; one with an add near it fights.
constexpr float kGothAddNear = 40.0f;
bool GothikSideMove(Plan const& plan, Member const& self, RaidOrder& out)
{
    Unit* gothik = nullptr;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kGothik)
            gothik = enemy;
    if (!gothik || !gothik->IsInCombat())
        return false;
    Player* const bot = self.p;
    GameObject* gate = GothikGate(bot);
    if (!gate || (gate->GetGoState() == GO_STATE_ACTIVE && !gothik->HasAura(kGothImmune)))
        return false;   // the gates are open: one room again
    auto it = g_gothSpot.find(bot->GetGUID().GetCounter());
    if (it == g_gothSpot.end() || it->second.instance != bot->GetInstanceId())
        return false;
    GothSpot const& spot = it->second;
    bool const away = LiveSide(gate, bot->GetPositionY()) != spot.live || bot->GetExactDist2d(gate) > kGothRoom;
    if (!away)
        for (Unit* enemy : plan.enemies)
            if ((enemy->GetEntry() != kGothik || !enemy->HasAura(kGothImmune)) && LiveSide(gate, enemy->GetPositionY()) == spot.live &&
                bot->GetExactDist2d(enemy) <= kGothAddNear)
                return false;   // an add, or Gothik down from his balcony, on its side: the fight is here
    out.post_enemy = gothik->GetGUID();
    if (!away && bot->GetExactDist2d(spot.x, spot.y) <= 4.0f)
    {
        out.at_post = true;
        out.hold_melee = !self.ranged && !self.healer;
        return true;
    }
    SetMove(out, spot.x, spot.y, spot.z, spot.live ? "gothik: back to the live side" : "gothik: back to the dead side", away);
    return true;
}

// In the fight the gate is closed: a bot only fights enemies on its own side (the other side is out of reach).
void ElectGothikSides(Election& e)
{
    Plan& plan = e.plan;
    bool gothik = false;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kGothik)
            gothik = true;
    if (!gothik)
        return;
    GameObject* gate = GothikGate(plan.members.front().p);
    if (!gate || gate->GetGoState() == GO_STATE_ACTIVE)   // the gate is open: one room again
        return;
    for (Member const& m : plan.members)
    {
        if (!m.bot)
            continue;
        RaidOrder* o = e.OrderOf(m.p);
        if (!o)
            continue;
        bool const side = LiveSide(gate, m.p->GetPositionY());
        Unit* nearest = nullptr;
        for (Unit* enemy : plan.enemies)
        {
            if (LiveSide(gate, enemy->GetPositionY()) != side || enemy->HasAura(kGothImmune))
                o->forbidden_targets.push_back(enemy->GetGUID());
            else if (enemy->GetEntry() != kGothik && (!nearest || m.p->GetDistance(enemy) < m.p->GetDistance(nearest)))
                nearest = enemy;
        }
        if (nearest && o->fallback_target.IsEmpty())
            o->fallback_target = nearest->GetGUID();
    }
}

// ---------------------------------------------------------------------------
// Thaddius: Stalagg and Feugen on their platforms
// ---------------------------------------------------------------------------
// The two adds stand on platforms at z 312 (classic_boss_thaddius.cpp ClassicThadAddPositions); Thaddius' own walkway (z 303) lies between
// them, cut off by the slime pit and a 15 y wall up to each platform (terrainprofile 2026-11-15). The platforms are reached from the room's
// door (Thaddius Door, 3421.9,-3017.5,295.6: complete paths of 103 y), and from one platform to the other is a 194 y walk round. Each
// add must stay within 60 y of its Tesla Coil or the coil shocks the raid, and Magnetic Pull swaps the two tanks every 20.5 s. So the raid
// splits as a Classic raid does: tanks, healers, ranged and melee alternate between the two platforms before the pull, and in phase one each
// non-tank fights on its own platform only. naxx_thaddius 1115c, staged on his walkway: the tanks had no path up (Incomplete, 0.5 y of
// progress), Stalagg and Feugen killed the casters who got up, and the raid wiped at 0/5.
constexpr uint32_t kStalagg  = 15929;
constexpr uint32_t kFeugen   = 15930;
constexpr float    kThadRoom = 150.0f;      // a member this close to Stalagg is in the room
constexpr float    kThadOtherAddMin = 45.0f;   // a side spot keeps at least this far from the other add (the platforms are about 85 y apart)
constexpr float    kThadSpotTol = 5.0f;     // a ranged or healer within this of its spot holds there
constexpr float    kThadMeleeOffSide = 45.0f;   // a melee further than this from its own add is on the wrong platform

enum : uint8_t { kThadSideStalagg = 1, kThadSideFeugen = 2 };

struct ThadSpot { uint32_t instance = 0; uint8_t side = 0; float x = 0, y = 0, z = 0; };
std::unordered_map<uint64_t, ThadSpot> g_thadSpot;                     // bot -> its side and spot
std::unordered_map<uint64_t, std::pair<bool, Position>> g_thadAnchor;  // (instance << 2 | side) -> anchor found?

uint32_t ThadAddEntry(uint8_t side) { return side == kThadSideStalagg ? kStalagg : kFeugen; }

// The side's anchor: a floor point on the add's platform at the side's distance from it, in its sight, far from the other add and reachable
// on foot from where `bot` stands. Stalagg's side stands 24 y off (outside his reach before the pull, inside every spell's after it);
// Feugen's 33 y, outside his Static Field (28135, 30 y: the mana of everyone nearer burns).
bool ThadAnchor(Player* bot, Creature* add, Creature* other, uint8_t side, Position& out)
{
    uint64_t const key = (uint64_t(bot->GetInstanceId()) << 2) | side;
    auto it = g_thadAnchor.find(key);
    if (it != g_thadAnchor.end())
    {
        out = it->second.second;
        return it->second.first;
    }
    static constexpr float kStalaggRadii[] = { 24.0f, 21.0f, 27.0f, 18.0f };
    static constexpr float kFeugenRadii[]  = { 33.0f, 35.0f, 31.0f };
    std::span<float const> const radii = side == kThadSideStalagg ? std::span<float const>(kStalaggRadii) : std::span<float const>(kFeugenRadii);
    // From the back of the platform (the bearing away from the other add) round to the front.
    float const back = other->GetAbsoluteAngle(add);
    for (float radius : radii)
        for (int step = 0; step <= 12; ++step)
            for (int sign : { 1, -1 })
            {
                if (step == 0 && sign < 0)
                    continue;
                float const a = back + float(sign * step) * float(M_PI) / 12.0f;
                float const x = add->GetPositionX() + radius * std::cos(a);
                float const y = add->GetPositionY() + radius * std::sin(a);
                float const z = bot->GetMap()->GetHeight(bot->GetPhaseShift(), x, y, add->GetPositionZ() + 3.0f, true, 8.0f);
                if (z <= INVALID_HEIGHT + 1.0f || std::fabs(z - add->GetPositionZ()) > 2.5f)
                    continue;   // off the platform
                if (other->GetExactDist2d(x, y) < kThadOtherAddMin || !add->IsWithinLOS(x, y, z + 2.0f))
                    continue;
                PathGenerator path(bot);
                if (!path.CalculatePath(x, y, z, false) || (path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE)))
                    continue;
                G3D::Vector3 const end = path.GetActualEndPosition();
                if ((end.x - x) * (end.x - x) + (end.y - y) * (end.y - y) > 3.0f * 3.0f || std::fabs(end.z - z) > 3.0f)
                    continue;
                out.Relocate(x, y, z);
                g_thadAnchor[key] = { true, out };
                TC_LOG_INFO("playerbot.v2.raid", "[raid] Thaddius: {} side spot ({:.1f},{:.1f},{:.1f}), {:.0f} y from the add, path {:.0f} y from {}",
                            side == kThadSideStalagg ? "Stalagg's" : "Feugen's", x, y, z, radius, path.GetPathLength(), bot->GetName());
                return true;
            }
    g_thadAnchor[key] = { false, Position() };
    TC_LOG_WARN("playerbot.v2.raid", "[raid] Thaddius: no reachable spot on {} platform from {} at ({:.1f},{:.1f},{:.1f})",
                side == kThadSideStalagg ? "Stalagg's" : "Feugen's", bot->GetName(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    return false;
}

// 0 tank, 1 healer, 2 ranged, 3 melee: each kind is split evenly between the platforms.
uint8_t ThadKind(Member const& m) { return m.tank ? 0 : m.healer ? 1 : m.ranged ? 2 : 3; }

void NaxxPrePullThaddius(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders, std::ostringstream& dump)
{
    Player* first = plan.members.front().p;
    Creature* stalagg = first->FindNearestCreature(kStalagg, 250.0f, true);
    Creature* feugen = first->FindNearestCreature(kFeugen, 250.0f, true);
    if (!stalagg || !feugen || stalagg->IsInCombat() || feugen->IsInCombat())
        return;
    bool inRoom = false;
    for (Member const& m : plan.members)
        if (m.p->GetExactDist2d(stalagg) < kThadRoom)
            inRoom = true;
    if (!inRoom)
        return;
    uint32_t const instance = first->GetInstanceId();
    // Count what each side already has (bots given a side before, humans by the add they stand nearer), then give the rest, kind by kind,
    // to the side with fewer of their kind. The main tank goes to Stalagg's: the raid opens on him (ScenarioDefs naxx_thaddius OpensOn).
    uint32_t count[4][3] = {};
    auto const sideOf = [&](Member const& m) -> uint8_t
    {
        if (!m.bot)
            return m.p->GetExactDist2d(stalagg) <= m.p->GetExactDist2d(feugen) ? kThadSideStalagg : kThadSideFeugen;
        auto it = g_thadSpot.find(m.p->GetGUID().GetCounter());
        return it != g_thadSpot.end() && it->second.instance == instance ? it->second.side : 0;
    };
    for (Member const& m : plan.members)
        if (uint8_t const side = sideOf(m))
            ++count[ThadKind(m)][side];
    uint32_t index = 0;
    for (Member const& m : plan.members)
    {
        if (!m.bot)
            continue;
        ++index;
        uint64_t const low = m.p->GetGUID().GetCounter();
        ThadSpot& spot = g_thadSpot[low];
        if (spot.instance != instance || !spot.side)
        {
            uint8_t const kind = ThadKind(m);
            uint8_t side;
            if (m.p == plan.mainTank)
                side = kThadSideStalagg;
            else
                side = count[kind][kThadSideFeugen] < count[kind][kThadSideStalagg] ? kThadSideFeugen : kThadSideStalagg;
            Creature* add = side == kThadSideStalagg ? stalagg : feugen;
            Creature* other = side == kThadSideStalagg ? feugen : stalagg;
            Position anchor;
            if (!ThadAnchor(m.p, add, other, side, anchor))
            {
                g_thadSpot.erase(low);
                continue;
            }
            ++count[kind][side];
            // A small ring around the anchor, a different place per bot, on the platform's floor.
            float const angle = float(index) * 2.4f;
            float const radius = 2.0f + float(index % 4) * 1.5f;
            float x = anchor.GetPositionX() + radius * std::cos(angle);
            float y = anchor.GetPositionY() + radius * std::sin(angle);
            float z = m.p->GetMap()->GetHeight(m.p->GetPhaseShift(), x, y, anchor.GetPositionZ() + 2.0f, true, 6.0f);
            if (z <= INVALID_HEIGHT + 1.0f || std::fabs(z - anchor.GetPositionZ()) > 2.0f || !add->IsWithinLOS(x, y, z + 2.0f))
            {
                x = anchor.GetPositionX();
                y = anchor.GetPositionY();
                z = anchor.GetPositionZ();
            }
            spot = { instance, side, x, y, z };
        }
        RaidOrder& o = orders[low];
        o.active = true;
        o.prepull = true;
        SetMove(o, spot.x, spot.y, spot.z, spot.side == kThadSideStalagg ? "thaddius: Stalagg's platform" : "thaddius: Feugen's platform");
    }
    dump << "  Thaddius split: Stalagg tanks " << count[0][kThadSideStalagg] << " healers " << count[1][kThadSideStalagg] << " ranged "
         << count[2][kThadSideStalagg] << " melee " << count[3][kThadSideStalagg] << " | Feugen tanks " << count[0][kThadSideFeugen] << " healers "
         << count[1][kThadSideFeugen] << " ranged " << count[2][kThadSideFeugen] << " melee " << count[3][kThadSideFeugen] << "\n";
}

// This bot's side in phase one (Stalagg or Feugen up and fighting), with its own add (nullptr when that one is down for good) and the other.
// False outside phase one and for a bot the split never gave a side.
bool ThadPhaseOneSide(Plan const& plan, Player* bot, ThadSpot const*& spot, Unit*& own, Unit*& other)
{
    own = other = nullptr;
    bool any = false;
    auto it = g_thadSpot.find(bot->GetGUID().GetCounter());
    if (it == g_thadSpot.end() || it->second.instance != bot->GetInstanceId() || !it->second.side)
        return false;
    spot = &it->second;
    for (Unit* enemy : plan.enemies)
    {
        if (enemy->GetEntry() != kStalagg && enemy->GetEntry() != kFeugen)
            continue;
        any = true;
        (enemy->GetEntry() == ThadAddEntry(spot->side) ? own : other) = enemy;
    }
    return any;
}

// Phase one: a ranged or healer bot holds its spot on its own platform; a melee on the wrong platform walks back; tanks go where Magnetic
// Pull throws them and are left alone.
bool ThaddiusSideMove(Plan const& plan, Member const& self, RaidOrder& out)
{
    if (!self.bot || self.tank)
        return false;
    ThadSpot const* spot = nullptr;
    Unit* own = nullptr;
    Unit* other = nullptr;
    if (!ThadPhaseOneSide(plan, self.p, spot, own, other))
        return false;
    Player* bot = self.p;
    if (!self.ranged && !self.healer)
    {
        float const ownX = own ? own->GetPositionX() : spot->x, ownY = own ? own->GetPositionY() : spot->y;
        if (bot->GetExactDist2d(ownX, ownY) <= kThadMeleeOffSide)
            return false;
        return SetMove(out, spot->x, spot->y, spot->z, "thaddius: back to own platform");
    }
    if (own)
        out.post_enemy = own->GetGUID();
    if (bot->GetExactDist2d(spot->x, spot->y) <= kThadSpotTol && std::fabs(bot->GetPositionZ() - spot->z) < 3.0f)
    {
        out.at_post = true;
        return true;
    }
    return SetMove(out, spot->x, spot->y, spot->z, "thaddius: own platform");
}

// ---------------------------------------------------------------------------
// Gluth
// ---------------------------------------------------------------------------
// He eats every Zombie Chow within 15 y for 5% of his health (DoSearchZombieChow, classic_boss_gluth.cpp), and his own body is that wide:
// he hits from 17.7 y (naxx_gluth 1115e: Mctankwb died at 3319,-3106 with him at 3311,-3122). A tank that leads him into a corner from in
// front leaves him 17 y short of it, on the raid's side: in 1115e his tank stood on the north-east post and he stood 19 y from the camp,
// ate the chows that came to the raid and killed it once his tank fell. So his tank drags him toward the east wall, then steps back past
// him to the camp side, inside his reach, where he does not follow. One more tank stands beside the holder for the Mortal Wound swap; the
// rest take the chows at two posts by the spawns, away from him, inside the healers' 40 y.
// The step back is anchored on the spot where he stopped, not on his live position: in naxx_gluth 1115f a spot 10 y from his live centre
// moved with every step he took, his tank walked him from the east wall to 6 y from the camp and back four times in 45 s ("drag" and
// "camp side" in turn), and it died there. The drag spot is within 43 y of the camp: 1115f's, 57 y off, left the tank out of heal reach.
constexpr uint32_t kGluth          = 15932;
constexpr uint32_t kZombieChow     = 16360;
constexpr float    kGluthCampX     = 3290.0f, kGluthCampY = -3110.0f;   // RaidEncounterData_NAXX.cpp: the ranged and healer rows
constexpr float    kGluthDragX     = 3330.0f, kGluthDragY = -3125.0f;   // by the east wall, 43 y from the camp, 65-80 y from the chow spawns
constexpr float    kGluthFloorZ    = 297.8f;
constexpr float    kGluthArrived   = 20.0f;   // he stands this close to the drag spot: where he stands now is his spot
// He left the east wall: farther than this from the drag spot, drag him again. Not a distance from the spot where he stopped: he steps
// after his tank by up to its 10 y step, and at 12 y from that spot (0085) the step itself sent the tank back to the wall, every few
// seconds (naxx_gluth 1115h: the holder moved 62 of 90 s between the wall and its step, 15 white hits and no Sunder in 90 s at 86 rage,
// and every DPS stood 26-29 s in the threat hold under it). He settles within 20 y of the drag spot and his tank stands 10 y past him:
// even on top of his tank he is within 30 y of it.
constexpr float    kGluthLeftWall  = 32.0f;
constexpr float    kGluthTankStep  = 10.0f;   // the holder stands this far from his spot toward the camp (his reach is 17.7)
constexpr float    kGluthSwapSide  = 5.0f;    // the swap tank stands this far beside the holder's spot
constexpr float    kGluthSpotTol   = 3.0f;
constexpr float    kGluthChowTol   = 5.0f;
constexpr uint32_t kGluthStaleMs   = 10000;   // an anchor not looked at for this long belongs to an earlier pull
constexpr uint32_t kGluthDragHoldMs = 15000;  // the longest the DPS wait for the drag (the holder runs 50-70 y with him at its back)
struct GluthPost { float x, y; };
constexpr GluthPost kGluthChowPosts[] = {
    { 3288.0f, -3148.0f },   // south: 30 y from spawn 3267.9,-3172.1, 41 y from 3308.3,-3185.8, 38 y from the camp
    { 3265.0f, -3136.0f },   // west: 13 y from spawn 3253.2,-3132.3, 36 y from the camp
};
struct GluthAnchor { uint32_t seen = 0; bool settled = false; float x = 0.f, y = 0.f; uint32_t drag_since = 0; bool drag_timed_out = false; };
std::unordered_map<uint64_t, GluthAnchor> g_gluthAnchor;   // Gluth's guid -> where he stopped by the east wall

Unit* FindGluth(Plan const& plan)
{
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kGluth && enemy->IsAlive())
            return enemy;
    return nullptr;
}

// His spot, once he has come to the east wall: where he stopped, settled while he stays within kGluthLeftWall of the drag spot.
GluthAnchor& GluthSpot(Unit* gluth, uint32_t now)
{
    GluthAnchor& a = g_gluthAnchor[(uint64_t(gluth->GetInstanceId()) << 32) ^ gluth->GetGUID().GetCounter()];
    if (!a.seen || now - a.seen > kGluthStaleMs)
        a = GluthAnchor{};
    a.seen = now ? now : 1u;
    float const gx = gluth->GetPositionX(), gy = gluth->GetPositionY();
    if (a.settled && Dist2D(gx, gy, kGluthDragX, kGluthDragY) > kGluthLeftWall)
        a.settled = false;
    if (!a.settled && Dist2D(gx, gy, kGluthDragX, kGluthDragY) <= kGluthArrived)
    {
        a.settled = true;
        a.x = gx;
        a.y = gy;
    }
    return a;
}

// The tank holding him (nullptr while he is on nobody or on a non-tank: the pick-up is the tank brain's), the tank kept beside it for the
// swap, and this tank's chow post (index into kGluthChowPosts, -1 for the holder and the swap tank).
enum class GluthJob : uint8_t { None, Holder, Swap, Chows };
GluthJob GluthJobOf(Plan const& plan, Unit* gluth, Player* bot, int& post)
{
    post = -1;
    Unit* held = gluth->GetVictim();
    Player* holder = held ? held->ToPlayer() : nullptr;
    bool holderIsTank = false;
    for (Member const& m : plan.members)
        if (m.p == holder && m.tank)
            holderIsTank = true;
    if (!holderIsTank)
        return GluthJob::None;
    if (bot == holder)
        return GluthJob::Holder;
    int index = 0;   // plan.members is sorted by guid
    for (Member const& m : plan.members)
    {
        if (!m.tank || m.p == holder || !m.p->IsAlive())
            continue;
        if (m.p == bot)
        {
            if (index == 0)
                return GluthJob::Swap;
            post = (index - 1) % int(std::size(kGluthChowPosts));
            return GluthJob::Chows;
        }
        ++index;
    }
    return GluthJob::None;
}

bool GluthMoveTo(Player* bot, RaidOrder& out, float x, float y, float tol, char const* why)
{
    if (bot->GetExactDist2d(x, y) <= tol)
    {
        out.at_post = true;
        return true;
    }
    float z = kGluthFloorZ;
    if (!Usable(bot, x, y, kGluthFloorZ, z))
        return false;
    return SetMove(out, x, y, z, why);
}

// A chow on this bot, alive and still a chow the tanks hold (not one Decimate sent walking to him).
bool ChowOnMe(Player* bot)
{
    Unit* victim = bot->GetVictim();
    return victim && victim->GetEntry() == kZombieChow && victim->IsAlive() && victim->GetVictim() == bot;
}

int NearestChowPost(Player* bot)
{
    int best = 0;
    for (int i = 1; i < int(std::size(kGluthChowPosts)); ++i)
        if (bot->GetExactDist2d(kGluthChowPosts[i].x, kGluthChowPosts[i].y) < bot->GetExactDist2d(kGluthChowPosts[best].x, kGluthChowPosts[best].y))
            best = i;
    return best;
}

bool GluthMove(Plan const& plan, Member const& self, uint32_t now, RaidOrder& out)
{
    if (!self.bot || !self.tank)
        return false;
    Unit* gluth = FindGluth(plan);
    if (!gluth)
        return false;
    int post = -1;
    GluthJob job = GluthJobOf(plan, gluth, self.p, post);
    Player* bot = self.p;
    // The swap tank with a chow on it takes the chow to a post: next to him, he eats it (1115f: Mctankwb held chows 3-14 y from him and he
    // went from 91% back to 94-96% three times).
    if (job == GluthJob::Swap && ChowOnMe(bot))
    {
        job = GluthJob::Chows;
        post = NearestChowPost(bot);
    }
    if (job == GluthJob::Holder || job == GluthJob::Swap)
    {
        GluthAnchor const& spot = GluthSpot(gluth, now);
        out.post_enemy = gluth->GetGUID();
        if (!spot.settled)
        {
            if (job == GluthJob::Swap)
                return false;   // he is still on the way: it follows and stays in taunt reach on its own
            return GluthMoveTo(bot, out, kGluthDragX, kGluthDragY, kGluthSpotTol, "gluth: drag to the east wall");
        }
        float const cx = kGluthCampX - spot.x, cy = kGluthCampY - spot.y;
        float const len = std::max(0.1f, std::sqrt(cx * cx + cy * cy));
        float const ux = cx / len, uy = cy / len;
        float x = spot.x + ux * kGluthTankStep, y = spot.y + uy * kGluthTankStep;
        if (job == GluthJob::Swap)
        {
            x -= uy * kGluthSwapSide;
            y += ux * kGluthSwapSide;
        }
        return GluthMoveTo(bot, out, x, y, kGluthSpotTol, job == GluthJob::Holder ? "gluth: camp side of him" : "gluth: beside his tank");
    }
    if (job != GluthJob::Chows)
        return false;
    // A chow tank going for a loose chow runs to it (the tank brain's pick-up); one with a chow on it, or with nothing, holds its post.
    if (Unit* victim = bot->GetVictim())
        if (victim->GetEntry() == kZombieChow && victim->IsAlive() && victim->GetVictim() != bot)
            return false;
    GluthPost const& p = kGluthChowPosts[post];
    return GluthMoveTo(bot, out, p.x, p.y, kGluthChowTol, "gluth: chow post");
}

} // namespace

void NaxxGluthTargets(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders);

// After PublishTargets: in Thaddius' phase one a non-tank bot only attacks the add on its own platform (the other is 85 y away and a 194 y
// walk round). The kill-together rule's spread still holds: an own add it forbids (more than 10% ahead of the other) leaves the bot with
// nothing to hit until the other side catches up.
void NaxxPostTargets(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders)
{
    for (Member const& m : plan.members)
    {
        if (!m.bot || m.tank)
            continue;
        ThadSpot const* spot = nullptr;
        Unit* own = nullptr;
        Unit* other = nullptr;
        if (!ThadPhaseOneSide(plan, m.p, spot, own, other))
            continue;
        auto it = orders.find(m.p->GetGUID().GetCounter());
        if (it == orders.end())
            continue;
        RaidOrder& o = it->second;
        if (other)
            o.forbidden_targets.push_back(other->GetGUID());
        bool const ownForbidden = !own ||
            std::find(o.forbidden_targets.begin(), o.forbidden_targets.end(), own->GetGUID()) != o.forbidden_targets.end();
        o.fallback_target = ownForbidden ? ObjectGuid::Empty : own->GetGUID();
    }
    NaxxGluthTargets(plan, orders);
}

// Gluth's chow tanks never attack him: their post is 35-55 y from him and the attack would walk them over (and bring their chows into his
// reach).
// While his tank drags him to the east wall the DPS do not hit him: a tank on the run builds no threat, and the timed threat releases let
// the raid's damage take him off it (naxx_gluth 1115g: on the main tank at 3 s, on a fury warrior at 6 s with the drag half done, then on
// one DPS after another; 3 tanks and 10 raiders dead by 36 s). Once he stands at his spot the normal threat holds take over. Ordered casts
// (Tranquilizing Shot) still go; the wait is bounded by kGluthDragHoldMs from the start of the drag.
void NaxxGluthTargets(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders)
{
    Unit* gluth = FindGluth(plan);
    if (!gluth)
        return;
    uint32_t const now = GameTime::GetGameTimeMS();
    GluthAnchor& spot = GluthSpot(gluth, now);
    int unused = -1;
    bool dragging = false;
    for (Member const& m : plan.members)
        if (m.tank && GluthJobOf(plan, gluth, m.p, unused) == GluthJob::Holder)
            dragging = !spot.settled;
    if (!dragging)
        spot.drag_since = 0;
    else if (!spot.drag_since)
        spot.drag_since = now ? now : 1u;
    if (dragging && now - spot.drag_since >= kGluthDragHoldMs)
    {
        if (!spot.drag_timed_out)
            TC_LOG_INFO("playerbot.v2.raid", "[raid_gluth] the drag has not brought him within {:.0f} y of {:.0f},{:.0f} in {} s (he is at {:.1f},{:.1f}): "
                        "the DPS open where he is", kGluthArrived, kGluthDragX, kGluthDragY, kGluthDragHoldMs / 1000u,
                        gluth->GetPositionX(), gluth->GetPositionY());
        spot.drag_timed_out = true;
        dragging = false;
    }
    else if (!dragging)
        spot.drag_timed_out = false;
    for (Member const& m : plan.members)
    {
        if (!dragging || !m.bot || m.tank || m.healer)
            continue;
        auto it = orders.find(m.p->GetGUID().GetCounter());
        if (it != orders.end())
            it->second.hold_target = gluth->GetGUID();
    }
    for (Member const& m : plan.members)
    {
        int post = -1;
        if (!m.bot || !m.tank || GluthJobOf(plan, gluth, m.p, post) != GluthJob::Chows)
            continue;
        auto it = orders.find(m.p->GetGUID().GetCounter());
        if (it == orders.end() || it->second.cast_kind == RaidOrder::CastKind::Taunt)
            continue;   // a swap taunt elected for this tank still goes
        it->second.forbidden_targets.push_back(gluth->GetGUID());
    }
}

// Patchwerk
// ----------------------------------------------------------------------------------------------------------------------------------
// The healers wait at their posts at the east end of his hall before he is pulled (RaidEncounterData_NAXX.cpp: 3326,-3223 and
// 3326,-3241, 20 y from his spawn, east of it, where his walk never goes). From the room's lower floor where the raid waits they had a
// 75 y walk after the pull: naxx_patchwerk 1115i fought him at his spawn, each soaker took one Hateful Strike (7k of 10.7k) in the first
// 3 s, and the healers spent 29 of 61 s walking, cast nothing for 18 s and every tank died before the first heal landed.
// They go only while he is far down his walk (kPatchFarYards from his spawn: 18 s or more at his pace, their walk takes about 8 s), and
// once sent they stay sent for the instance, so the post is held as he walks back. In combat the fight's stand rows take over.
constexpr uint32_t kPatchwerk      = 16028;
constexpr float    kPatchSpawnX    = 3308.5f, kPatchSpawnY = -3232.1f;
constexpr float    kPatchFarYards  = 90.0f;
constexpr float    kPatchNearRaid  = 150.0f;
constexpr float    kPatchHallZ     = 294.1f;
struct PatchPost { float x, y; };
constexpr PatchPost kPatchHealerPosts[] = { { 3326.0f, -3223.0f }, { 3326.0f, -3241.0f } };   // share 1 (even), share 2 (odd)
// The ranged DPS stand at the same east end, 4 y further east, between the healers' corners (RaidEncounterData_NAXX.cpp, Patchwerk).
constexpr PatchPost kPatchRangedPosts[] = { { 3330.0f, -3227.0f }, { 3330.0f, -3237.0f } };   // share 1 (even), share 2 (odd)
std::unordered_set<uint32_t> g_patchPrePull;   // instances whose healers were sent

void NaxxPrePullPatchwerk(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders, std::ostringstream& dump)
{
    Player* first = plan.members.front().p;
    if (first->GetExactDist2d(kPatchSpawnX, kPatchSpawnY) > kPatchNearRaid)
        return;
    Creature* patch = first->FindNearestCreature(kPatchwerk, 300.0f, true);
    if (!patch || patch->IsInCombat())
        return;
    uint32_t const instance = first->GetInstanceId();
    float const away = patch->GetExactDist2d(kPatchSpawnX, kPatchSpawnY);
    if (!g_patchPrePull.count(instance))
    {
        if (away < kPatchFarYards)
            return;
        g_patchPrePull.insert(instance);
        TC_LOG_INFO("playerbot.v2.raid", "[raid_patchwerk] he is {:.0f} y down his walk (at {:.1f},{:.1f}): the healers and the ranged go to their posts at the hall's east end",
                    away, patch->GetPositionX(), patch->GetPositionY());
    }
    // Healers and ranged DPS alike: the ranged had their post west of him in the hall, and from the lower floor the way up to it runs past
    // him, so they walked into his 18 y circle, out again and back for the whole fight (naxx_patchwerk 1115 threat run: all 16 ranged DPS
    // on "leave hazard" / "role position" turn about, 0 damage in 90 s). Waiting at the east end before the pull, they never cross it.
    uint32_t healerIndex = 0, rangedIndex = 0, healers = 0, ranged = 0;
    for (Member const& m : plan.members)
    {
        bool const isRanged = !m.tank && !m.healer && m.ranged;   // Who::Ranged
        if (!m.healer && !isRanged)
            continue;
        PatchPost const& post = m.healer ? kPatchHealerPosts[healerIndex++ & 1u] : kPatchRangedPosts[rangedIndex++ & 1u];
        if (!m.bot)
            continue;
        float z = kPatchHallZ;
        if (!Usable(m.p, post.x, post.y, kPatchHallZ, z))
            z = kPatchHallZ;
        RaidOrder& o = orders[m.p->GetGUID().GetCounter()];
        o.active = true;
        o.prepull = true;
        SetMove(o, post.x, post.y, z, m.healer ? "patchwerk: healer post" : "patchwerk: ranged post");
        ++(m.healer ? healers : ranged);
    }
    dump << "  Patchwerk: " << healers << " healers and " << ranged << " ranged to the east end, he is " << int(away) << " y from his spawn\n";
}

void NaxxPrePull(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders, std::ostringstream& dump)
{
    NaxxPrePullPatchwerk(plan, orders, dump);
    if (g_patchPrePull.size() > 64)
        g_patchPrePull.clear();
    NaxxPrePullGothik(plan, orders, dump);
    if (g_gothDraw.size() > 64)
        g_gothDraw.clear();
    if (g_gothSplitSince.size() > 64)
        g_gothSplitSince.clear();
    NaxxPrePullThaddius(plan, orders, dump);
    if (g_thadSpot.size() > 512)
        g_thadSpot.clear();
    if (g_thadAnchor.size() > 64)
        g_thadAnchor.clear();
    if (g_gothSpot.size() > 512)
        g_gothSpot.clear();
    if (g_gothOffFloorLogged.size() > 64)
        g_gothOffFloorLogged.clear();
    if (g_gothAnchor.size() > 64)
        g_gothAnchor.clear();
    if (g_gluthAnchor.size() > 64)
        g_gluthAnchor.clear();
}

// ---------------------------------------------------------------------------
// Maexxna: Web Spray
// ---------------------------------------------------------------------------
// Web Spray (29484) stuns the whole raid in her room for 10 s, her tank too, every 40 s from the pull (MaexWebSprayCooldown in
// classic_boss_maexxna.cpp): nobody heals and the tank cannot block, parry or dodge while she keeps hitting it. At 30% she enrages
// (28747: +250% damage done, +50% attack speed). naxx21: the tank fell to 33-39% in each of the first two stuns and died right after the
// second to a Poison Shock; once she was enraged every stun killed the tank she was on, and the last tank went at 133 s. In the seconds
// before the spray the healers put what keeps healing through a stun on her tank (Power Word: Shield, Renew, Rejuvenation, Regrowth),
// each by one healer, and once she is enraged her warrior tank opens Shield Wall, or Last Stand when the wall is spent.
constexpr uint32_t kMaexxna        = 15952;
constexpr uint32_t kWebSpray       = 29484;
constexpr uint32_t kMaexEnrage     = 28747;
constexpr uint32_t kSprayCycleMs   = 40000;
constexpr uint32_t kSprayPrepMs    = 7000;    // the jobs start this long before the spray is due (a Regrowth's cast and a GCD each)
constexpr uint32_t kSprayLateMs    = 5000;    // and stop this long after it was due, if the clock was off and no spray came
constexpr uint32_t kWeakenedSoul   = 6788;
constexpr uint32_t kShieldWall     = 871;
constexpr uint32_t kLastStand      = 12975;

// `withinMs`: the job waits until the spray is due within this long (0: from kSprayPrepMs on).
struct SprayJob { uint32_t base; uint32_t blockedBy; uint32_t withinMs; char const* what; };
// Abolish Poison: her Necrotic Poison (28776, healing taken -90 %) comes back on her target every 5-10 s, so one lands while every
// healer stands stunned in the web and the heals over time on her tank do a tenth. naxx_maexxna r25 (combined 1118): it was cleansed
// 4 s before the first spray and on him again 5 s into it; her tank went from 94 % to dead in 9 s under the HoTs. Abolish Poison
// takes a poison off every 2 s for 8 s, stunned healer or not, so it goes on as late as it can: within 2.5 s of the spray it lasts
// some 6 s into it.
SprayJob const kSprayJobs[] = {
    { 2893, 0,             2500, "maexxna: abolish poison on her tank before Web Spray" },   // Abolish Poison
    { 17,   kWeakenedSoul, 0,    "maexxna: shield her tank before Web Spray" },     // Power Word: Shield
    { 139,  0,             0,    "maexxna: renew her tank before Web Spray" },      // Renew
    { 774,  0,             0,    "maexxna: rejuvenation on her tank before Web Spray" },
    { 8936, 0,             0,    "maexxna: regrowth on her tank before Web Spray" },
};

struct MaexSpray
{
    uint32_t engaged = 0;   // first plan that saw her in the fight
    uint32_t seen = 0;      // last plan that saw her (a gap means a new pull: her guid stays the same after a wipe)
    uint32_t last = 0;      // when the last spray went on (from its aura)
    bool     up = false;    // a spray is on the raid now
    std::unordered_map<uint32_t, uint64_t> jobOf;   // job family -> the healer on it for the coming spray
    uint32_t jobsFor = 0;   // the spray time those jobs are for
};
std::unordered_map<uint64_t, MaexSpray> g_maexSpray;   // her guid -> the spray clock

bool AnyRankOn(Unit* u, uint32_t base)
{
    for (uint32_t const id : SpellFamily::Ranks(base))
        if (u->HasAura(id))
            return true;
    return false;
}

bool Stunned(Player* p) { return p->HasUnitState(UNIT_STATE_STUNNED) || p->HasAura(kWebSpray); }

int ManaPctOf(Player* p)
{
    uint32_t const max = p->GetMaxPower(POWER_MANA);
    return max ? int(uint64_t(p->GetPower(POWER_MANA)) * 100 / max) : 100;
}

// One healer for one job: the one that had it while it is still able, else the first idle one that can cast it now.
Player* SprayHealer(Election& e, MaexSpray& st, SprayJob const& job, Unit* tank, uint32_t& spell)
{
    Ranks const ranks = SpellFamily::Ranks(job.base);
    auto able = [&](Member const& m)
    {
        if (!m.bot || !m.healer || m.p == tank || e.busy.count(m.p->GetGUID().GetCounter()) || Stunned(m.p) || ManaPctOf(m.p) < 10)
            return false;
        spell = KnownRank(m.p, ranks);
        return spell && Ready(m.p, spell, tank, false);
    };
    if (auto it = st.jobOf.find(job.base); it != st.jobOf.end())
        for (Member const& m : e.plan.members)
            if (m.p->GetGUID().GetCounter() == it->second && able(m))
                return m.p;
    for (bool allowCasting : { false, true })
        for (Member const& m : e.plan.members)
            if (Busy(m.p) == allowCasting && able(m))
                return m.p;
    return nullptr;
}

void ElectMaexxnaSpray(Election& e, uint32_t now)
{
    Plan& plan = e.plan;
    Unit* maex = nullptr;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kMaexxna)
            maex = enemy;
    if (!maex)
        return;
    MaexSpray& st = g_maexSpray[maex->GetGUID().GetCounter()];
    if (st.engaged && now - st.seen > 10000)
        st = MaexSpray();
    st.seen = now;
    if (!st.engaged)
        st.engaged = now;
    // The clock follows the spray itself once one was seen: it went on when its aura did.
    bool up = false;
    for (Member const& m : plan.members)
        if (Aura const* a = m.p->GetAura(kWebSpray))
        {
            up = true;
            if (!st.up)
                st.last = now - std::min<uint32_t>(now, uint32_t(std::max(0, a->GetMaxDuration() - a->GetDuration())));
            break;
        }
    st.up = up;
    uint32_t const due = (st.last ? st.last : st.engaged) + kSprayCycleMs;
    if (up || now + kSprayPrepMs < due || now > due + kSprayLateMs)
        return;
    if (st.jobsFor != due)
    {
        st.jobsFor = due;
        st.jobOf.clear();
    }
    // Every heal on her tank lands from here to the spray (CancelOverheal keeps it).
    for (Member const& m : plan.members)
        if (m.bot && m.healer)
            if (RaidOrder* ho = e.OrderOf(m.p))
                ho->keep_heals = true;
    Unit* tank = maex->GetVictim();
    Player* holder = tank ? tank->ToPlayer() : nullptr;
    if (!holder || !FindMember(plan, holder))
        return;
    int const inMs = int(due) - int(now);
    // Her tank is set for the spray: from here on no other tank takes her (its taunt would leave the shield, the heals over time and
    // Abolish Poison on the tank she just left). naxx_maexxna r26: the jobs went on Mctankwb 7-2 s before the spray, the main tank
    // took her back from it 1.5 s before, and died in the web with one shield on him.
    if (Member const* hm = FindMember(plan, holder); hm && hm->tank)
        for (Member const& m : plan.members)
            if (m.tank && m.p != holder)
                if (RaidOrder* to = e.OrderOf(m.p))
                    to->forbidden_targets.push_back(maex->GetGUID());

    for (SprayJob const& job : kSprayJobs)
    {
        if (AnyRankOn(holder, job.base) || (job.blockedBy && holder->HasAura(job.blockedBy)) || (job.withinMs && inMs > int(job.withinMs)))
            continue;
        uint32_t spell = 0;
        Player* healer = SprayHealer(e, st, job, holder, spell);
        if (!healer)
            continue;
        RaidOrder* o = e.OrderOf(healer);
        if (!o)
            continue;
        o->cast_kind = RaidOrder::CastKind::Prepare;
        o->cast_spell = spell;
        o->cast_target = holder->GetGUID();
        e.busy.insert(healer->GetGUID().GetCounter());
        e.dump << "  " << healer->GetName() << ": " << job.what << " " << spell << " -> " << holder->GetName() << "\n";
        auto [it, fresh] = st.jobOf.try_emplace(job.base, healer->GetGUID().GetCounter());
        if (fresh || it->second != healer->GetGUID().GetCounter())
        {
            it->second = healer->GetGUID().GetCounter();
            TC_LOG_INFO("playerbot.v2.raid", "[raid_maexxna] {} casts {} on {} ({:.0f}%) {:.1f} s before Web Spray", healer->GetName(), spell,
                        holder->GetName(), holder->GetHealthPct(), inMs / 1000.0f);
        }
    }

    // Enraged, a stun is a tank's death: the wall first, Last Stand once the wall is spent.
    Member* hm = FindMember(plan, holder);
    if (!hm->bot || !hm->tank || holder->GetClass() != CLASS_WARRIOR || e.busy.count(holder->GetGUID().GetCounter()))
        return;
    if (!maex->HasAura(kMaexEnrage) || holder->HasAura(kShieldWall) || holder->HasAura(kLastStand))
        return;
    for (uint32_t const spell : { kShieldWall, kLastStand })
    {
        if (!holder->HasSpell(spell) || !Ready(holder, spell, holder, false))
            continue;
        RaidOrder* o = e.OrderOf(holder);
        if (!o)
            return;
        o->cast_kind = RaidOrder::CastKind::Prepare;
        o->cast_spell = spell;
        o->cast_target = holder->GetGUID();
        e.busy.insert(holder->GetGUID().GetCounter());
        e.dump << "  " << holder->GetName() << ": maexxna: " << (spell == kShieldWall ? "Shield Wall" : "Last Stand") << " before Web Spray\n";
        if (inMs > int(kSprayPrepMs) - 1000)
            TC_LOG_INFO("playerbot.v2.raid", "[raid_maexxna] {} opens {} at {:.0f}%, she is enraged, {:.1f} s before Web Spray", holder->GetName(),
                        spell == kShieldWall ? "Shield Wall" : "Last Stand", holder->GetHealthPct(), inMs / 1000.0f);
        return;
    }
}

bool NaxxMechanicsMove(Plan const& plan, Member const& self, uint32_t now, RaidOrder& out)
{
    return HeiganMove(plan, self, now, out) || SapphironMove(plan, self, now, out) || GothikSideMove(plan, self, out) || ThaddiusSideMove(plan, self, out) ||
           GluthMove(plan, self, now, out);
}

void NaxxMechanicsElect(Election& e, uint32_t now)
{
    ElectRazuvious(e);
    ElectFaerlina(e);
    ElectGothikSides(e);
    ElectMaexxnaSpray(e, now);
    if (g_maexSpray.size() > 64)
        g_maexSpray.clear();
    if (g_airSpread.size() > 256)
        g_airSpread.clear();
}

} // namespace Playerbot::Raid::detail
