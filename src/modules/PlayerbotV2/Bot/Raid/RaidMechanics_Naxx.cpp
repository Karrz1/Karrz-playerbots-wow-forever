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
#include "RaidBrain.h"
#include "PlayerbotMovement.h"
#include "SpellFamily.h"

#include "Creature.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Log.h"
#include "Map.h"
#include "MoveSpline.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WaypointManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <list>
#include <map>
#include <mutex>
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
// Plague Cloud 30122 (dance phase, his channel on the platform): radius 20 around him. naxx21: 8 deaths to it, 12-20 y from him, raid:move
// among them (a dance spot inside the cloud). Kept clear by 4 y for both hit boxes and his turning.
constexpr float    kPlagueCloudClear = 24.0f;
// His room's doors: the entrance (GO_PLAG_HEIG_ENTRY_DOOR) closes 15 s into the fight (EVENT_HEIGAN_DOOR_CLOSE) and the exit to the tunnel
// (GO_PLAG_HEIG_EXIT_DOOR) stays shut until he dies. A raider beyond either when it shuts is out of the fight. naxx_heigan af52 (1120): the
// mana users' way out of his Mana Burn reach led east through the open entrance (2830,-3680, 7 y past it); it shut at 15 s with 25 of
// them outside, the 12 healers among them, and the tanks died unhealed (healers casting 0 of 12 all fight).
constexpr uint32_t kHeiganEntryDoor = 181202;
constexpr uint32_t kHeiganExitDoor  = 181203;
constexpr float    kDoorClear       = 3.0f;      // a spot off the field stays this far inside both doors

// His teleport (EventPortPlayer, at 40 s and every phase after) puts three raiders, never his tank, at the far end of the tunnel east of his
// room, where the Eye Stalks (16236, rooted, aggro at 19 y) Mind Flay them (29407, 750 a tick). The ported raider kills the stalks round it
// (15% health) and walks back. naxx22 1116: two hunters and a warlock held their raid post at the landing ("raid_formation_hold") and died
// to Mind Flay at 66-74 s.
constexpr uint32_t kEyeStalk        = 16236;
constexpr float    kPortX           = 2917.43f;  // classic_boss_heigan.cpp EventPortPlayer
constexpr float    kPortY           = -3769.18f;
constexpr float    kPortedNear      = 60.0f;     // a raider this close to the landing
constexpr float    kPortedFar       = 75.0f;     // and this far from him was ported
constexpr float    kStalkReach      = 32.0f;     // the stalks a ported raider kills: within its spell reach and a margin
constexpr float    kBackNear        = 45.0f;     // the walk back ends this close to him; the fight's own moves take over

std::unordered_set<uint64_t> g_heiganPorted;   // raiders on their way back from the tunnel

bool HeiganPorted(Player const* bot, Unit const* heigan)
{
    return Dist2D(bot->GetPositionX(), bot->GetPositionY(), kPortX, kPortY) < kPortedNear && bot->GetExactDist2d(heigan) > kPortedFar;
}

// The live Eye Stalk nearest the bot within kStalkReach that it can see, or null.
Creature* NearestStalk(Player* bot)
{
    std::list<Creature*> stalks;
    bot->GetCreatureListWithEntryInGrid(stalks, kEyeStalk, kStalkReach);
    Creature* best = nullptr;
    for (Creature* c : stalks)
        if (c->IsAlive() && bot->IsValidAttackTarget(c) && bot->IsWithinLOSInMap(c) &&
            (!best || bot->GetExactDist2d(c) < bot->GetExactDist2d(best)))
            best = c;
    return best;
}

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

// A door's plane: its position, its facing (the axis through the doorway) and the sign of that axis on his side.
struct DoorPlane { float x, y, nx, ny, side; };
std::unordered_map<uint32_t, std::vector<DoorPlane>> g_heiganDoors;   // instance id -> both doors, once both are found
std::mutex g_heiganDoorsMu;                                            // plans for different instances run on different map threads

std::vector<DoorPlane> HeiganDoors(Unit* heigan)
{
    uint32_t const instance = heigan->GetMap()->GetInstanceId();
    {
        std::lock_guard<std::mutex> lk(g_heiganDoorsMu);
        auto it = g_heiganDoors.find(instance);
        if (it != g_heiganDoors.end())
            return it->second;
    }
    Creature const* boss = heigan->ToCreature();
    Position const home = boss ? boss->GetHomePosition() : heigan->GetPosition();
    std::list<GameObject*> objects;
    heigan->GetGameObjectListWithEntryInGrid(objects, kHeiganEntryDoor, 150.0f);
    heigan->GetGameObjectListWithEntryInGrid(objects, kHeiganExitDoor, 150.0f);
    std::vector<DoorPlane> doors;
    for (GameObject* go : objects)
    {
        float const nx = std::cos(go->GetOrientation()), ny = std::sin(go->GetOrientation());
        float const toHome = (home.GetPositionX() - go->GetPositionX()) * nx + (home.GetPositionY() - go->GetPositionY()) * ny;
        doors.push_back({ go->GetPositionX(), go->GetPositionY(), nx, ny, toHome >= 0.0f ? 1.0f : -1.0f });
    }
    if (doors.size() >= 2)   // only a complete pair is kept: a grid round a door may not be loaded yet
    {
        std::lock_guard<std::mutex> lk(g_heiganDoorsMu);
        g_heiganDoors[instance] = doors;
    }
    return doors;
}

// True when (x, y) lies on his side of both doors, kDoorClear inside them.
bool InHeiganRoom(std::vector<DoorPlane> const& doors, float x, float y)
{
    for (DoorPlane const& d : doors)
        if (((x - d.x) * d.nx + (y - d.y) * d.ny) * d.side < kDoorClear)
            return false;
    return true;
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
        g_heiganPorted.erase(self.p->GetGUID().GetCounter());
        return false;
    }
    // Ported: the stalks round it first (ElectHeiganTunnel), then the walk back, until it is within kBackNear of him. The walk is pathed by
    // the move itself (the tunnel bends), with no Usable probe: it starts far outside the fight area round the main tank that Usable keeps to.
    {
        uint64_t const id = self.p->GetGUID().GetCounter();
        if (HeiganPorted(self.p, heigan))
            g_heiganPorted.insert(id);
        if (g_heiganPorted.count(id))
        {
            if (self.p->GetExactDist2d(heigan) <= kBackNear)
                g_heiganPorted.erase(id);
            else
            {
                if (!self.healer && HeiganPorted(self.p, heigan) && NearestStalk(self.p))
                    return true;   // the stalk is its target this plan; no move
                return SetMove(out, heigan->GetPositionX(), heigan->GetPositionY(), heigan->GetPositionZ(), "heigan: back from the tunnel");
            }
        }
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
    // Mana Burn takes whoever's power is mana (CheckManausersAndRepeat: GetPowerType, so a bear or cat druid is safe), tanks included.
    bool const burnable = bot->GetPowerType() == POWER_MANA && !(self.tank && bot == heigan->GetVictim());
    // Fight phase: mana users leave his Mana Burn reach for a spot outside the eruption field (the platform), wherever they stand: one
    // in the field used to stay on its safe section within his reach. naxx21: 20 deaths to Mana Burn, the healers and casters 9-22 y
    // from him in the field, 2.2-2.7k a burn every 3 s.
    // The way out never passes nearer to him than the bot already is: naxx r25 (1118) "out of mana burn" picked spots across him.
    bool const burnFight = !st.dance && burnable;
    float const hx = heigan->GetPositionX(), hy = heigan->GetPositionY();
    float const fromHim = Dist2D(bot->GetPositionX(), bot->GetPositionY(), hx, hy);
    float const passClear = std::min(fromHim, kManaBurnRange + 1.0f) - 1.0f;   // the nearest to him a mana user's walk may come
    auto leaveField = [&](char const* why) -> bool
    {
        std::vector<DoorPlane> const doors = HeiganDoors(heigan);
        float const away = heigan->GetAbsoluteAngle(bot);
        int pathChecks = 0;
        for (float radius : { kManaBurnRange + 4.0f, kManaBurnRange + 10.0f })
            for (float offset : { 0.0f, 0.35f, -0.35f, 0.7f, -0.7f, 1.05f, -1.05f, 1.6f, -1.6f, 2.2f, -2.2f, 3.14f })
            {
                float const x = hx + radius * std::cos(away + offset);
                float const y = hy + radius * std::sin(away + offset);
                float u, s;
                TrapDistances(*traps, st.eruptions, x, y, u, s);
                if (std::min(u, s) < kInField || pathChecks >= 6 || !InHeiganRoom(doors, x, y))
                    continue;
                if (SegmentDist2D(bot->GetPositionX(), bot->GetPositionY(), x, y, hx, hy) < passClear)
                    continue;
                ++pathChecks;
                float z = 0.0f;
                if (Usable(bot, x, y, bot->GetPositionZ(), z, /*urgent*/ true))
                    return SetMove(out, x, y, z, why, /*urgent*/ true);   // two burns kill a cloth wearer
            }
        return false;
    };
    if (burnFight && bot->GetExactDist(heigan) < kManaBurnRange + 1.0f && leaveField("heigan: out of mana burn"))
        return true;
    // No floor out of the field in reach: the dance below, on spots beyond his reach only.
    // Fight phase: only whoever stands in the field dances (the ranged on the platform keep casting). Dance phase: everybody.
    if (!st.dance && !inField)
        return false;
    bool const inCloud = st.dance && bot->GetExactDist2d(heigan) < kPlagueCloudClear;
    bool const safeHere = !inCloud && (st.dance ? (inField && safe + kSafeMargin <= unsafe) : (unsafe >= kInField || safe + kSafeMargin <= unsafe));
    if (safeHere)
    {
        // Melee whose target stands where the next eruption hits wait instead of following it in.
        if (!self.tank && !self.healer && !self.ranged)
            if (Unit* victim = bot->GetVictim())
            {
                // In the dance he stands in his own cloud: no melee walks in after him.
                if (st.dance && victim == heigan)
                {
                    out.hold_melee = true;
                    return true;
                }
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

    // A mana user in the field outside his reach, where the next eruption hits: off the field to the platform beyond his reach first. A
    // section spot is the second choice: naxx r25 (1118) sent ten casters and healers from 35 y to safe-section spots 5-8 y from him
    // (Mana Burn 2.3-2.7k, dead at 48-57 s).
    if (burnFight && leaveField("heigan: off the field, beyond mana burn"))
        return true;

    // The best spot in the safe section: deep inside it (far from erupting traps), not too far to walk. A mana user in the fight phase
    // takes only spots beyond his reach, on a walk that comes no nearer to him than it already is.
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
        if (st.dance && Dist2D(heigan->GetPositionX(), heigan->GetPositionY(), t.x, t.y) < kPlagueCloudClear)
            continue;   // inside his Plague Cloud
        // Fight phase only: in the dance he channels Plague Cloud instead of burning mana.
        if (burnFight && (Dist2D(hx, hy, t.x, t.y) < kManaBurnRange + 3.0f ||
                          SegmentDist2D(bot->GetPositionX(), bot->GetPositionY(), t.x, t.y, hx, hy) < passClear))
            continue;
        float const walk = Dist2D(bot->GetPositionX(), bot->GetPositionY(), t.x, t.y);
        candidates.push_back({ std::min(u, 12.0f) - 0.25f * walk, &t });
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
// bot -> the hide spot it was last sent to behind an ice block, and when. sapphiron_0138 run2 (1118): Mchealpd got its hide order at
// 19:58:33, the next passes found no new spot (the four path checks spent) and gave no move, so the heal rule had the tick: four Flash of
// Light casts stopped the walk and it died to the breath at :40, 24 y from him, in the open.
struct HideSpot { float x = 0.0f, y = 0.0f, z = 0.0f; uint32_t at = 0; bool round = false; };   // round: the point beside a block on the way
constexpr float kSapphRoundBlock = 4.5f;   // the point a walk round an ice block passes, beside the spot behind it
std::unordered_map<uint64_t, HideSpot> g_hideSpot;
// After the breath the raid stays behind the blocks a little longer: the cast ends and he drops his hover in the same moment
// (classic_boss_sapphiron.cpp EVENT_SAPPH_LAND waits for the cast to finish, then setHover(false)), so a bot that walked out on the hover
// was in the open while the breath's hits went out. He lands for 4 s before he fights again, so the wait costs nothing.
constexpr uint32_t kBreathGraceMs = 3000;
std::mutex g_breathMu;                                    // plans for different instances run on different map threads
std::unordered_map<uint64_t, uint32_t> g_breathSeen;      // Sapphiron's guid -> the last plan that saw him breathing (0 = none)

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
    if (!sapphiron)
        return false;
    bool const breathing = Casting(sapphiron, kFrostBreath) || Casting(sapphiron, kFrostBreathDummy);
    bool afterBreath = false;
    {
        std::lock_guard<std::mutex> lk(g_breathMu);
        uint32_t& seen = g_breathSeen[sapphiron->GetGUID().GetCounter()];
        if (breathing)
            seen = now ? now : 1;
        afterBreath = !breathing && seen && now - seen < kBreathGraceMs;
    }
    if (!sapphiron->IsHovering() && !afterBreath)
        return false;
    Player* bot = self.p;
    if (bot->HasAura(kIcebolt))
        return true;   // frozen inside a block

    std::list<GameObject*> blocks;
    sapphiron->GetGameObjectListWithEntryInGrid(blocks, kIceBlock, 150.0f);
    blocks.remove_if([](GameObject* b) { return !b->isSpawned(); });
    // From the breath's falling ball on, a cast under way is stopped for the walk; before it (the fourth block) it is let finish first.
    // The few seconds after the breath count as the breath (kBreathGraceMs).
    bool const urgentHide = breathing || afterBreath;
    if (urgentHide || blocks.size() >= kHideFromBlocks)
    {
        // Hide: stand right behind an ice block, on the line from Sapphiron through the block; the nearest block first.
        std::vector<GameObject*> near(blocks.begin(), blocks.end());
        std::sort(near.begin(), near.end(), [bot](GameObject* l, GameObject* r)
        {
            return Dist2D(bot->GetPositionX(), bot->GetPositionY(), l->GetPositionX(), l->GetPositionY()) <
                   Dist2D(bot->GetPositionX(), bot->GetPositionY(), r->GetPositionX(), r->GetPositionY());
        });
        if (near.empty())
            return false;
        GameObject* const nearest = near.front();
        float const nearestDist = Dist2D(bot->GetPositionX(), bot->GetPositionY(), nearest->GetPositionX(), nearest->GetPositionY());
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
            // Where a bot counts itself hidden, once per bot and breath, with the block it hides by: naxx_sapphiron r28 lost three
            // hunters and later a mage on one spot 45 y off that this test called hidden. The breath (28524) tests from him (targets
            // 22/15, 200 y), as this does.
            uint32_t& said = g_airSpread[bot->GetGUID().GetCounter() | (uint64_t(1) << 62)];
            if (now - said >= 10000)
            {
                said = now;
                TC_LOG_INFO("playerbot.v2.raid", "[raid] Sapphiron: {} hidden at {:.1f},{:.1f},{:.1f}, nearest ice block at {:.1f},{:.1f},{:.1f} "
                            "({:.1f} y), he is at {:.1f},{:.1f},{:.1f}", bot->GetName(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                            nearest->GetPositionX(), nearest->GetPositionY(), nearest->GetPositionZ(), nearestDist, sapphiron->GetPositionX(),
                            sapphiron->GetPositionY(), sapphiron->GetPositionZ());
            }
            return true;   // already hidden: stays
        }
        // A spot counts when the bot standing on it is hidden: the test is made again at the height the walk ends on, and a spot on top of
        // the block is no spot. sapphiron_0138 run2: six bots died stacked at 3501.4,-5265.0,140.0, 2 y above the floor (138.1) by the
        // block: the spot was tested on the floor, and Usable looks for the ground from 2 y above the point it is given, which found the
        // top of the block.
        // A block with no usable spot behind it (its path checks spent, the ground refused) passes to the next one: the search used to stop
        // after four path checks round the nearest block with no move (sapphiron_0138 run2: Mcwarrc held as melee under him at 19:58:29 and
        // died to the breath). Three blocks, three path checks each.
        // The spot is fixed by the block, not by the fight: Usable is asked as for a fixed place, so it does not ask for the bot's own sight
        // of the spot (the block it hides behind stands between them whenever the bot comes from his side) nor for the fight area. The
        // walk there is still checked. sapphiron af38 (1119): 46 of 49 Frost Breath deaths had "finds no spot" as their last word; the
        // melee under him and every raider on his side of a block were refused every spot behind it.
        // An ice block is a door-type game object (181247, type 0), and a path that runs through a closed door is refused: the walk
        // from his side to a spot behind a block crosses the block itself, on the spot's own polygon, so every such spot was "no path".
        // The navmesh does not know the block is there; the walk goes round it, by a point beside the spot, kSapphRoundBlock off the line
        // from the block, from which the spot is a straight step. sapphiron af39-1 (1119): all 81 "finds no spot" lines of both runs
        // were refused "no path" (9 of 9 path checks), and 21 died to the first breath.
        auto doorBetween = [&](float ax, float ay, float az, float bx, float by, float bz)
        {
            G3D::Vector3 hit;
            return map->FindClosedDoorOnSegment(bot->GetPhaseShift(), G3D::Vector3(ax, ay, az + 1.0f), G3D::Vector3(bx, by, bz + 1.0f), hit);
        };
        HideSpot& last = g_hideSpot[bot->GetGUID().GetCounter()];
        uint32_t blocksTried = 0;
        int noGround = 0, notHidden = 0, raised = 0, roundBlock = 0;
        int refused[16] = {};
        for (GameObject* block : near)
        {
            if (++blocksTried > 3)
                break;
            float const away = std::atan2(block->GetPositionY() - sy, block->GetPositionX() - sx);
            float const floorZ = block->GetPositionZ();
            int pathChecks = 0;
            for (float distance : { 2.0f, 2.5f, 3.0f, 4.0f, 5.0f })
            {
                for (float side : { 0.0f, 0.15f, -0.15f })
                {
                    float const px = block->GetPositionX() + distance * std::cos(away + side);
                    float const py = block->GetPositionY() + distance * std::sin(away + side);
                    float const gz = map->GetHeight(bot->GetPhaseShift(), px, py, floorZ + 1.0f, true, 6.0f);
                    if (gz <= INVALID_HEIGHT + 1.0f || gz > floorZ + 1.0f)
                    {
                        ++noGround;
                        continue;
                    }
                    if (!hidden(px, py, gz))
                    {
                        ++notHidden;
                        continue;
                    }
                    if (++pathChecks > 3)
                        break;
                    float pz = 0.0f;
                    if (!Usable(bot, px, py, gz, pz, /*urgent*/ true, /*hazard*/ false, /*fixedPlace*/ true))
                    {
                        int const why = g_usableWhy;
                        if (why == 7 && doorBetween(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), px, py, gz))
                        {
                            // round the block: the point beside the spot on the bot's side first
                            float const ax = std::cos(away + side + float(M_PI) / 2.0f), ay = std::sin(away + side + float(M_PI) / 2.0f);
                            float best[2][2] = { { px + kSapphRoundBlock * ax, py + kSapphRoundBlock * ay },
                                                 { px - kSapphRoundBlock * ax, py - kSapphRoundBlock * ay } };
                            if (Dist2D(bot->GetPositionX(), bot->GetPositionY(), best[1][0], best[1][1]) <
                                Dist2D(bot->GetPositionX(), bot->GetPositionY(), best[0][0], best[0][1]))
                                std::swap(best[0], best[1]);
                            for (auto const& v : best)
                            {
                                float const vgz = map->GetHeight(bot->GetPhaseShift(), v[0], v[1], floorZ + 1.0f, true, 6.0f);
                                if (vgz <= INVALID_HEIGHT + 1.0f || vgz > floorZ + 1.0f ||
                                    doorBetween(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), v[0], v[1], vgz) ||
                                    doorBetween(v[0], v[1], vgz, px, py, gz))
                                    continue;
                                float vz = 0.0f;
                                if (!Usable(bot, v[0], v[1], vgz, vz, /*urgent*/ true, /*hazard*/ false, /*fixedPlace*/ true) || vz > floorZ + 1.0f)
                                    continue;
                                SetMove(out, v[0], v[1], vz, "hide behind ice block: round the block", urgentHide);
                                last = { v[0], v[1], vz, now, true };   // the walk goes on to the point beside it; from there the spot is a straight step
                                return true;
                            }
                            ++roundBlock;
                        }
                        ++refused[std::clamp(why, 0, 15)];
                        continue;
                    }
                    if (pz > floorZ + 1.0f || !hidden(px, py, pz))
                    {
                        ++raised;
                        continue;
                    }
                    SetMove(out, px, py, pz, "hide behind ice block", urgentHide);
                    last = { px, py, pz, now, false };
                    return true;
                }
                if (pathChecks > 3)
                    break;
            }
        }
        // No new spot this pass: the one it was sent to a moment ago still stands while it is hidden from him, and the walk there goes on (an
        // order with no move gives the tick to the rotation, and a cast stops the walk).
        if (now - last.at < 5000 && Dist2D(bot->GetPositionX(), bot->GetPositionY(), last.x, last.y) >= 1.2f &&
            (last.round || hidden(last.x, last.y, last.z)))
        {
            SetMove(out, last.x, last.y, last.z, "hide behind ice block", urgentHide);
            return true;
        }
        // No spot behind the three nearest blocks is out of his sight: said once per bot and breath.
        uint32_t& logged = g_airSpread[bot->GetGUID().GetCounter() | (uint64_t(1) << 63)];
        if (now - logged >= 10000)
        {
            logged = now;
            std::string why;
            for (int i = 1; i < 16; ++i)
                if (refused[i])
                    why += fmt::format(" {} {}", UsableWhyName(i), refused[i]);
            TC_LOG_INFO("playerbot.v2.raid", "[raid] Sapphiron: {} finds no spot behind the ice block at {:.1f},{:.1f},{:.1f} out of his sight "
                        "(he is at {:.1f},{:.1f},{:.1f}, {:.0f} y off; {} blocks tried: {} without ground, {} in his sight, {} raised or seen at "
                        "the walk's height, {} with no way round the block, refused:{})", bot->GetName(), nearest->GetPositionX(), nearest->GetPositionY(),
                        nearest->GetPositionZ(), sx, sy, sz, Dist2D(sx, sy, nearest->GetPositionX(), nearest->GetPositionY()),
                        std::min<uint32_t>(blocksTried, 3), noGround, notHidden, raised, roundBlock, why.empty() ? " none" : why);
        }
        // Then right behind the nearest all the same, on the line from him through the block, on the floor (not on top of it).
        float const away = std::atan2(nearest->GetPositionY() - sy, nearest->GetPositionX() - sx);
        float const px = nearest->GetPositionX() + 2.5f * std::cos(away), py = nearest->GetPositionY() + 2.5f * std::sin(away);
        float pz = 0.0f;
        if (Dist2D(bot->GetPositionX(), bot->GetPositionY(), px, py) >= 1.2f &&
            Usable(bot, px, py, nearest->GetPositionZ(), pz, /*urgent*/ true, /*hazard*/ false, /*fixedPlace*/ true) &&
            pz <= nearest->GetPositionZ() + 1.0f)
            SetMove(out, px, py, pz, "hide behind ice block", urgentHide);
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
// From the pull, how long the raid leaves Razuvious to a Mind Control still to come before its tanks take him themselves (no priest in reach
// to cast one). A Mind Control cast or held always keeps him to the understudy.
constexpr uint32_t kRazMcGraceMs = 15000;
constexpr int      kRazControllers = 2;   // understudies kept under Mind Control at once
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

// Disrupting Shout (29107): every 25 s from 15 s after the pull, it burns up to 4,500 mana from everybody within 45 y whose power is mana and
// deals twice the mana burned as physical damage (classic_boss_razuvious.cpp; SpellEffect 62 POWER_BURN, radius index 11, amplitude 2;
// Spell::EffectPowerBurn skips a target whose power type is not mana). naxx21: the first shout killed 15 healers, casters and priests at
// 17-29 y from him, 5.6-8.0k each. Every mana user but the priests who hold or are about to take an understudy keeps 50 y from him
// (RaidEncounterData_NAXX.cpp, avoidEnemy); the priests at the understudies cannot, the Mind Control channel breaks past 22 y
// (Spell::UpdateChanneledTargetList). Just before each shout a paladin puts Blessing of Protection on each of them (the damage is
// physical; Forbearance allows it once a minute), and otherwise a priest shields them.
constexpr uint32_t kRazShoutFirstMs = 15000;
constexpr uint32_t kRazShoutEveryMs = 25000;
constexpr uint32_t kRazShoutPrepMs  = 4000;
constexpr uint32_t kForbearance     = 25771;
constexpr uint32_t kRazWeakenedSoul = 6788;
constexpr float    kMcReach         = 15.0f;   // a priest walking in to cast stops this close to its understudy (Mind Control: 20 y)
Ranks const kBlessingOfProtection = { 1022, 5599, 10278 };
Ranks const kPowerWordShield = { 17, 592, 600, 3747, 6065, 6066, 10898, 10899, 10900, 10901 };

struct RazFight { uint32_t engaged = 0; uint32_t seen = 0; uint64_t walker = 0; };
std::unordered_map<uint64_t, RazFight> g_razFight;   // his guid -> this pull

// A mind-control priest gets Blessing of Protection, or Power Word: Shield, before the shout: one job per priest per plan.
void ShieldController(Election& e, Player* priest, int inMs)
{
    for (Ranks const* family : { &kBlessingOfProtection, &kPowerWordShield })
    {
        bool const bop = family == &kBlessingOfProtection;
        if (bop ? priest->HasAura(kForbearance) : priest->HasAura(kRazWeakenedSoul))
            continue;
        bool has = false;
        for (uint32_t id : *family)
            has = has || priest->HasAura(id);
        if (has)
            return;
        for (Member const& m : e.plan.members)
        {
            if (!m.bot || m.p == priest || e.busy.count(m.p->GetGUID().GetCounter()) || m.p->HasUnitState(UNIT_STATE_STUNNED))
                continue;
            uint32_t const id = KnownRank(m.p, *family);
            if (!id || !Ready(m.p, id, priest, false))
                continue;
            RaidOrder* o = e.OrderOf(m.p);
            if (!o)
                return;
            o->cast_kind = RaidOrder::CastKind::Prepare;
            o->cast_spell = id;
            o->cast_target = priest->GetGUID();
            e.busy.insert(m.p->GetGUID().GetCounter());
            e.dump << "  " << m.p->GetName() << ": razuvious: " << (bop ? "Blessing of Protection" : "Power Word: Shield") << " on "
                   << priest->GetName() << " before Disrupting Shout\n";
            if (inMs > int(kRazShoutPrepMs) - 400)
                TC_LOG_INFO("playerbot.v2.raid", "[raid_razuvious] {} casts {} on {} ({} mana) {:.1f} s before Disrupting Shout", m.p->GetName(),
                            bop ? "Blessing of Protection" : "Power Word: Shield", priest->GetName(), priest->GetPower(POWER_MANA), inMs / 1000.0f);
            return;
        }
    }
}

void ElectRazuvious(Election& e, uint32_t now)
{
    Plan& plan = e.plan;
    Unit* raz = nullptr;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kRazuvious)
            raz = enemy;
    if (!raz)
        return;
    RazFight& fight = g_razFight[raz->GetGUID().GetCounter()];
    if (fight.engaged && now - fight.seen > 10000)   // a new pull (his guid stays the same after a wipe)
        fight = RazFight();
    fight.seen = now;
    if (!fight.engaged)
        fight.engaged = now;
    uint32_t const since = now - fight.engaged;
    uint32_t const nextShout = since < kRazShoutFirstMs ? kRazShoutFirstMs
                             : kRazShoutFirstMs + ((since - kRazShoutFirstMs) / kRazShoutEveryMs + 1) * kRazShoutEveryMs;
    int const shoutInMs = int(nextShout) - int(since);
    std::vector<Player*> controllers;

    std::list<Creature*> understudies;
    raz->GetCreatureListWithEntryInGrid(understudies, kUnderstudy, 80.0f);

    // 1. Controllers command their understudy.
    int holding = 0;            // controllers with more than 12 s of control left
    bool pending = false;       // a Mind Control is being cast
    int mcCasts = 0;            // how many are being cast
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
            controllers.push_back(m.p);
            if (fight.walker == m.p->GetGUID().GetCounter())
                fight.walker = 0;
            // Keep the cast job so the consumer waits for it instead of letting the rotation cast over it.
            o->move = false;
            o->cast_kind = RaidOrder::CastKind::CrowdControl;
            o->cast_spell = casting;
            o->cast_target = mcTarget;
            e.busy.insert(m.p->GetGUID().GetCounter());
            pending = true;
            ++mcCasts;
            continue;
        }
        Creature* us = Possessed(m.p);
        if (!us)
            continue;
        controllers.push_back(m.p);
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
        // Taunt whenever it is ready, also while the understudy has him: it lifts the understudy's threat to the top of his list, so the
        // DPS, who only keep under the understudy's threat, have room to keep hitting (naxx_razuvious 0152 run 1: one taunt at :13, the
        // understudy's threat stayed at 750 while the rogues opened at :37). The barrier still comes first when the understudy is low.
        bool const barrier = tanking && us->GetHealthPct() < 60.0f && !us->HasAura(kUsShieldWall) && CreatureSpellReady(us, kUsShieldWall);
        if (!barrier && us->GetDistance(raz) <= 8.0f && CreatureSpellReady(us, kUsTaunt))
        {
            o->charm_spell = kUsTaunt;
            o->charm_target = raz->GetGUID();
        }
        else if (barrier)
            o->charm_spell = kUsShieldWall;
        else
            o->charm_attack = raz->GetGUID();
        e.dump << "  " << m.p->GetName() << ": controls an understudy (" << left / 1000 << " s left)"
               << (tanking ? ", tanking" : "") << "\n";
    }

    // 2. While an understudy holds Razuvious the raid's tanks leave him alone and keep the free understudies. Until one holds him, while a
    // Mind Control is under way or still to come early in the pull, nobody touches him: no raid tank takes him and no DPS opens on him.
    // He hits a raid tank for 7-26k. naxx_razuvious 0152 run 1: the fallback pull at 21:49:09 put the whole raid on him, a raid tank took
    // him (loose, nearest), both raid tanks on him died at :12 and :13, and the understudy's taunt only came at :13; the warriors who had
    // opened on him died under him next.
    bool understudyTanks = false;
    for (Member const& m : plan.members)
        if (Creature* us = Possessed(m.p))
            if (raz->GetVictim() == us)
                understudyTanks = true;
    bool mcComing = !controllers.empty() || pending;
    if (!mcComing && since < kRazMcGraceMs)
        for (Member const& m : plan.members)
            if (m.bot && m.p->IsAlive() && KnownRank(m.p, kMindControl))
                for (Creature* us : understudies)
                    if (us->IsAlive() && !us->IsCharmed())
                    {
                        mcComing = true;
                        break;
                    }
    if (!understudyTanks && mcComing)
        for (Member const& m : plan.members)
        {
            if (!m.bot || m.healer || Possessed(m.p))
                continue;
            RaidOrder* o = e.OrderOf(m.p);
            if (!o)
                continue;
            o->forbidden_targets.push_back(raz->GetGUID());
            if (!m.tank)
                continue;   // a DPS stops (DPS never kill an understudy)
            Creature* best = nullptr;
            for (Creature* us : understudies)
                if (us->IsAlive() && !us->IsCharmed() && (!best || m.p->GetDistance(us) < m.p->GetDistance(best)))
                    best = us;
            if (best)
                o->fallback_target = best->GetGUID();
        }
    if (!understudyTanks && mcComing)
        e.dump << "  Razuvious: nobody touches him until an understudy holds him (" << since / 1000 << " s into the pull)\n";
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

    // 3. The priests at the understudies before the shout.
    if (shoutInMs <= int(kRazShoutPrepMs))
        for (Player* priest : controllers)
            ShieldController(e, priest, shoutInMs);

    // 4. Two understudies under Mind Control, as the raid plays him: the second stands at him and taunts the moment the first loses him
    // (its channel broken, its control run out, the understudy dead), and a new Mind Control starts whenever fewer than two hold one with
    // time to spare. naxx_razuvious 0152 run 1: one understudy held him; at :40 its priest, beaten to 15% by the loose understudies, lost
    // it with nobody else holding one, and he killed three warriors in three seconds.
    if (holding + mcCasts >= kRazControllers)
        return;
    Creature* target = nullptr;
    for (Creature* us : understudies)
        if (us->IsAlive() && !us->IsCharmed() && (!target || us->GetDistance(raz) < target->GetDistance(raz)))
            target = us;
    if (!target || e.Assign(target, { &kMindControl }, true, RaidOrder::CastKind::CrowdControl, "mind control", false))
        return;
    // Nobody in reach (the mana users keep out of the shout): one priest walks in, the same one until it casts. The walk in crosses the
    // mana users' 50 y circle round him (RaidEncounterData_NAXX.cpp avoidEnemy), whose "leave hazard" move makes the walker busy before
    // this election runs; the walker keeps its job over that move, and casts as soon as it stands in reach. naxx_razuvious r25 and
    // gt-1118 r1: each pass the hazard pulled the walker back out, the next pass chose another priest, seven priests took turns 45-57 y
    // out for 30 s and no Mind Control was cast.
    auto onlyLeavesHazard = [&e](Member const& m)
    {
        RaidOrder* o = e.OrderOf(m.p);
        return o && o->move && o->move_reason && std::strcmp(o->move_reason, "leave hazard") == 0;
    };
    Member const* walker = nullptr;
    for (Member const& m : plan.members)
    {
        if (!m.bot || !KnownRank(m.p, kMindControl))
            continue;
        bool const isWalker = m.p->GetGUID().GetCounter() == fight.walker;
        if (e.busy.count(m.p->GetGUID().GetCounter()) && !(isWalker && onlyLeavesHazard(m)))
            continue;
        if (isWalker)
        {
            walker = &m;
            break;
        }
        if (!walker || m.p->GetDistance(target) < walker->p->GetDistance(target))
            walker = &m;
    }
    if (!walker)
        return;
    RaidOrder* o = e.OrderOf(walker->p);
    if (!o)
        return;
    // In reach: the cast replaces the walk (and the hazard move), and the walker stays the walker until CastingMindControl takes over.
    if (uint32_t const rank = KnownRank(walker->p, kMindControl); rank && Ready(walker->p, rank, target, true) && !Busy(walker->p))
    {
        o->move = false;
        o->move_reason = "";
        o->cast_kind = RaidOrder::CastKind::CrowdControl;
        o->cast_spell = rank;
        o->cast_target = target->GetGUID();
        e.busy.insert(walker->p->GetGUID().GetCounter());
        fight.walker = walker->p->GetGUID().GetCounter();
        TC_LOG_INFO("playerbot.v2.raid", "[raid_razuvious] {} casts Mind Control on an understudy ({:.0f} y away, the shout in {:.1f} s)",
                    walker->p->GetName(), walker->p->GetExactDist(target), shoutInMs / 1000.0f);
        e.dump << "  " << walker->p->GetName() << ": razuvious: Mind Control " << rank << " -> " << target->GetName() << "\n";
        return;
    }
    float const d = std::max(walker->p->GetExactDist2d(target), 0.1f);
    // Already at the stop and still not ready is the line of sight (Ready tests it as the spell does): the walker closes to 8 y, round
    // whatever blocks it, instead of standing where it is.
    float const stopAt = d <= kMcReach + 1.0f ? 8.0f : kMcReach;
    float const t = std::max(0.0f, (d - stopAt) / d);
    float x = walker->p->GetPositionX() + (target->GetPositionX() - walker->p->GetPositionX()) * t;
    float y = walker->p->GetPositionY() + (target->GetPositionY() - walker->p->GetPositionY()) * t;
    float z = target->GetPositionZ();
    if (!Usable(walker->p, x, y, target->GetPositionZ(), z, /*urgent*/ true))
    {
        x = target->GetPositionX();
        y = target->GetPositionY();
        z = target->GetPositionZ();
    }
    if (fight.walker != walker->p->GetGUID().GetCounter())
        TC_LOG_INFO("playerbot.v2.raid", "[raid_razuvious] {} walks in to mind control an understudy ({:.0f} y away, the shout in {:.1f} s)",
                    walker->p->GetName(), d, shoutInMs / 1000.0f);
    fight.walker = walker->p->GetGUID().GetCounter();
    SetMove(*o, x, y, z, "razuvious: walk in to mind control");
    e.busy.insert(walker->p->GetGUID().GetCounter());
}

// The fight opens on a Mind Control, before anybody else is in his room's fight: a priest takes an understudy from out of his aggro reach,
// and the understudy (ElectRazuvious, step 1) taunts him; the raid's tanks pick up the free understudies. He hits a raid tank for 7-26k
// (Unbalancing Strike 26613: 25.8k on a T3 warrior), so a tank that opens on him dies before any understudy is controlled. naxx_razuvious
// r22: the main tank pulled him and four understudies at 0 s, died at 3 s, the next two tanks by 6 s, and no Mind Control was ever cast.
// He sees a raider from 33 y (IdleAggroReach) while he walks his loop round the understudies (waypoint path 810160610, 2757-2784 x,
// -3088..-3111 y), and Mind Control reaches 20 y, so no cast spot is out of his sight all the time: the priest waits out of the sight of his
// whole walk and goes in when his walk has taken him far enough off that he cannot come into sight before the cast lands.
// naxx_razuvious r24h: the spot was picked against the core's 10 y, he engaged the priest from 29 y before its cast went off, and 26 died.
// A member this close to him is in his room. The landing keeps clear of his whole walk by his 33 y sight and the squad's spread (about 51 y,
// PickArrival), so the raid can stand 80 y from him while he is at the far end of his loop.
constexpr float kRazRoom = 100.0f;
constexpr float kMcRangeMargin = 2.0f;      // the cast spot stands this far inside Mind Control's reach (range and both combat reaches)
constexpr float kRazSightMargin = 3.0f;     // the priest keeps this far outside his sight
constexpr float kRazUsReachMargin = 3.0f;   // ... and outside an understudy's aggro reach
constexpr float kRazWaitStep = 2.0f;        // the wait spot is looked for along the line from the understudy out through the cast spot
constexpr int   kRazWaitSteps = 25;
constexpr float kRazSlackS = 1.0f;          // the brain's cadence and the cast's start, counted as this many seconds more of his walk
constexpr float kRazWindowSpareS = 1.0f;    // a spot's time out of his sight each lap beats its way in by this much (the brain catches it)
constexpr float kRazOnLeg = 3.0f;           // a priest this close to the line from its wait spot to its cast spot is on its way in
constexpr int   kRazPathChecks = 24;        // complete walks checked per search, nearest candidates first
constexpr uint32_t kRazSearchRetryMs = 5000;   // a search that found nothing is run again after this long
std::unordered_map<uint32_t, uint64_t> g_razOpener;   // instance -> the priest that opens the fight
std::unordered_set<uint32_t> g_razOpenLogged;         // instances whose missing spot was logged
struct RazOpenSpot
{
    uint64_t priest = 0;
    uint64_t understudy = 0;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;   // the cast spot
    float wx = 0.0f, wy = 0.0f, wz = 0.0f;   // the wait spot, out of the sight of his whole walk
    uint32_t failedMs = 0;                   // when the last search found nothing (0 = it found the spot above)
    bool goingIn = false;                    // the priest was last sent to the cast spot (for the log)
};
std::unordered_map<uint32_t, RazOpenSpot> g_razOpenSpot;   // instance -> the opener's spots, kept while the priest and target stay

// The nearest his walk comes to (x, y): the legs of his waypoint path, closed into a loop, and where he stands (he is off the path while
// he walks back to it). With no path, where he stands.
float RazWalkDist(Creature const* raz, WaypointPath const* path, float x, float y)
{
    float best = raz->GetExactDist2d(x, y);
    if (path && !path->Nodes.empty())
        for (std::size_t i = 0; i < path->Nodes.size(); ++i)
        {
            WaypointNode const& a = path->Nodes[i];
            WaypointNode const& b = path->Nodes[(i + 1) % path->Nodes.size()];
            best = std::min(best, SegmentDist2D(a.X, a.Y, b.X, b.Y, x, y));
        }
    return best;
}

// The ground at (x, y) near zHint, for a point the path check then walks to.
bool RazFloor(Player* p, float x, float y, float zHint, float& z)
{
    float const h = p->GetMap()->GetHeight(p->GetPhaseShift(), x, y, zHint + 2.0f, true, 12.0f);
    if (h <= INVALID_HEIGHT + 1.0f || std::fabs(h - zHint) > 12.0f)
        return false;
    z = h;
    return true;
}

// The nearest he comes to the priest on its way in, the two of them moving at once: the priest runs from (px, py) to its cast spot
// (sx, sy) at `run` and then stands there `holdS` more seconds (the cast and the slack); he walks on from the point of his path nearest to
// him at `speed` (onward round the loop; both ways for a path walked back from its end), and, since a pause at a node can end at any
// moment or last the whole time, standing where he is is checked as well. Off his path (more than 4 y from it: walking home), or with no
// path, as if he walked straight at the cast spot. Sampled every 0.2 s; `atS` is when the nearest point comes.
// Each moment is compared with where the priest is at that moment: naxx_razuvious r25 and raz-1 compared his whole walk with the whole way
// in, cast spot included, and the cast spot is 18-21 y from his loop's near side, so the priest waited at its wait spot until the run's
// fallback pull at 180 s.
float RazClosestOnWayIn(Creature const* raz, WaypointPath const* path, float speed, float px, float py, float sx, float sy, float run,
                        float holdS, float& atS)
{
    constexpr float kStepS = 0.2f;
    float const rx = raz->GetPositionX(), ry = raz->GetPositionY();
    float const leg = Dist2D(px, py, sx, sy);
    float const totalS = leg / std::max(run, 1.0f) + holdS;
    int const steps = int(std::ceil(totalS / kStepS));
    auto priestAt = [&](float t, float& x, float& y)
    {
        float const f = leg > 0.0f ? std::min(1.0f, run * t / leg) : 1.0f;
        x = px + (sx - px) * f;
        y = py + (sy - py) * f;
    };
    float best = std::numeric_limits<float>::max();
    atS = 0.0f;
    auto note = [&](float t, float hx, float hy)
    {
        float x = 0.0f, y = 0.0f;
        priestAt(t, x, y);
        if (float const d = Dist2D(x, y, hx, hy); d < best)
        {
            best = d;
            atS = t;
        }
    };
    // Standing where he is (a pause at a node).
    for (int s = 0; s <= steps; ++s)
        note(float(s) * kStepS, rx, ry);

    std::size_t leg0 = 0;
    float legDist = -1.0f;
    std::vector<WaypointNode> const* nodes = (path && path->Nodes.size() >= 2) ? &path->Nodes : nullptr;
    std::size_t const n = nodes ? nodes->size() : 0;
    bool const backwards = path && path->Flags.HasFlag(WaypointPathFlags::FollowPathBackwardsFromEndToStart);
    if (nodes)
        for (std::size_t i = 0; i < (backwards ? n - 1 : n); ++i)
        {
            WaypointNode const& a = (*nodes)[i];
            WaypointNode const& b = (*nodes)[(i + 1) % n];
            float const d = SegmentDist2D(a.X, a.Y, b.X, b.Y, rx, ry);
            if (legDist < 0.0f || d < legDist)
            {
                leg0 = i;
                legDist = d;
            }
        }
    if (!nodes || legDist > 4.0f)
    {
        // Walking straight at the cast spot.
        float const toSpot = Dist2D(rx, ry, sx, sy);
        for (int s = 0; s <= steps; ++s)
        {
            float const t = float(s) * kStepS;
            float const f = toSpot > 0.0f ? std::min(1.0f, speed * t / toSpot) : 1.0f;
            note(t, rx + (sx - rx) * f, ry + (sy - ry) * f);
        }
        return best;
    }
    // Walks the nodes in `order` from where he stands, one sample per step of `speed * kStepS` yards.
    auto walk = [&](std::vector<std::size_t> const& order)
    {
        float x = rx, y = ry;
        std::size_t k = 0;
        float const stepYd = speed * kStepS;
        for (int s = 1; s <= steps; ++s)
        {
            float go = stepYd;
            while (go > 0.0f && k < order.size())
            {
                WaypointNode const& node = (*nodes)[order[k]];
                float const len = Dist2D(x, y, node.X, node.Y);
                if (len <= go)
                {
                    x = node.X;
                    y = node.Y;
                    go -= len;
                    ++k;
                    continue;
                }
                x += (node.X - x) * go / len;
                y += (node.Y - y) * go / len;
                go = 0.0f;
            }
            note(float(s) * kStepS, x, y);
        }
    };
    // Enough nodes ahead for any way in (two laps of the loop, or out and back twice).
    std::vector<std::size_t> onward, back;
    if (!backwards)
    {
        for (std::size_t k = 1; k <= 2 * n; ++k)
            onward.push_back((leg0 + k) % n);
        walk(onward);
    }
    else
    {
        // Out and back: onward to the end, then back to the start, and the same starting the other way.
        for (std::size_t i = leg0 + 1; i < n; ++i) onward.push_back(i);
        for (std::size_t i = n - 1; i-- > 0;) onward.push_back(i);
        for (std::size_t i = leg0 + 1; i-- > 0;) back.push_back(i);
        for (std::size_t i = 1; i < n; ++i) back.push_back(i);
        walk(onward);
        walk(back);
    }
    return best;
}

// His pace on his path: the spline's own while he walks a leg, else the path's move type. Not his movement flags: a waypoint walk picks the
// walking pace without setting the walking flag (MoveSplineInit::Launch), so IsWalking() read false all along, his run pace was counted,
// and no way in ever looked long enough (r25, raz-1: the priest waited until the 180 s fallback pull).
float RazPace(Creature const* raz, WaypointPath const* path)
{
    float speed = raz->GetSpeed(path && path->MoveType == WaypointMoveType::Run ? MOVE_RUN : MOVE_WALK);
    if (path && path->Velocity)
        speed = *path->Velocity;
    if (!raz->movespline->Finalized() && raz->movespline->Velocity() > 0.0f)
        speed = raz->movespline->Velocity();
    return std::max(speed, 0.5f);
}

// The longest he walks his loop, at `speed`, without coming within `sight` of the way in from (wx, wy) to (sx, sy): the time a priest has
// for its run and cast each lap. His pauses at nodes are not counted (a scripted one can end at any time), so this is the least he gives.
// With no path, all the time when he stands out of sight, else none.
float RazWindowS(Creature const* raz, WaypointPath const* path, float speed, float wx, float wy, float sx, float sy, float sight)
{
    constexpr float kForever = 1000.0f;
    if (!path || path->Nodes.size() < 2)
        return SegmentDist2D(wx, wy, sx, sy, raz->GetPositionX(), raz->GetPositionY()) >= sight ? kForever : 0.0f;
    // His walk as one lap of points: round the loop, or out to the end and back.
    std::vector<std::pair<float, float>> lap;
    std::size_t const n = path->Nodes.size();
    if (path->Flags.HasFlag(WaypointPathFlags::FollowPathBackwardsFromEndToStart))
    {
        for (std::size_t i = 0; i < n; ++i) lap.push_back({ path->Nodes[i].X, path->Nodes[i].Y });
        for (std::size_t i = n - 1; i-- > 1;) lap.push_back({ path->Nodes[i].X, path->Nodes[i].Y });
    }
    else
        for (std::size_t i = 0; i < n; ++i) lap.push_back({ path->Nodes[i].X, path->Nodes[i].Y });
    constexpr float kStepYd = 0.5f;
    std::vector<bool> clear;
    for (std::size_t i = 0; i < lap.size(); ++i)
    {
        auto const& [ax, ay] = lap[i];
        auto const& [bx, by] = lap[(i + 1) % lap.size()];
        int const steps = std::max(1, int(std::ceil(Dist2D(ax, ay, bx, by) / kStepYd)));
        for (int k = 0; k < steps; ++k)
        {
            float const f = float(k) / float(steps);
            clear.push_back(SegmentDist2D(wx, wy, sx, sy, ax + (bx - ax) * f, ay + (by - ay) * f) >= sight);
        }
    }
    if (std::all_of(clear.begin(), clear.end(), [](bool c) { return c; }))
        return kForever;
    // The longest run of clear samples, round the lap.
    std::size_t best = 0, cur = 0;
    for (std::size_t i = 0; i < 2 * clear.size(); ++i)
    {
        cur = clear[i % clear.size()] ? cur + 1 : 0;
        best = std::max(best, std::min(cur, clear.size()));
    }
    return float(best) * kStepYd / speed;
}

void NaxxPrePullRazuvious(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders, std::ostringstream& dump)
{
    if (plan.members.empty())
        return;
    Player* first = plan.members.front().p;
    Creature* raz = first->FindNearestCreature(kRazuvious, 200.0f, true);
    if (!raz || raz->IsInCombat())
        return;
    bool inRoom = false;
    for (Member const& m : plan.members)
        inRoom = inRoom || m.p->GetExactDist2d(raz) < kRazRoom;
    if (!inRoom)
        return;
    uint32_t const instance = first->GetInstanceId();
    // A priest already casting or holding one: its order stays a still cast, so nothing walks it out of the channel.
    for (Member const& m : plan.members)
    {
        ObjectGuid target;
        if (uint32_t const casting = CastingMindControl(m.p, target))
        {
            if (!m.bot)
                return;
            RaidOrder& o = orders[m.p->GetGUID().GetCounter()];
            o.active = true;
            o.prepull = true;
            o.move = false;
            o.cast_kind = RaidOrder::CastKind::CrowdControl;
            o.cast_spell = casting;
            o.cast_target = target;
            return;
        }
        if (Possessed(m.p))
            return;
    }
    std::list<Creature*> understudies;
    raz->GetCreatureListWithEntryInGrid(understudies, kUnderstudy, 60.0f);
    understudies.remove_if([](Creature* c) { return !c->IsAlive() || c->IsCharmed() || c->IsInCombat(); });
    if (understudies.empty())
        return;
    // The opener: the same priest until it casts, else the bot priest with Mind Control nearest to an understudy.
    Member const* opener = nullptr;
    uint64_t& chosen = g_razOpener[instance];
    for (Member const& m : plan.members)
        if (m.bot && m.p->GetGUID().GetCounter() == chosen && KnownRank(m.p, kMindControl))
            opener = &m;
    if (!opener)
    {
        float best = 0.0f;
        for (Member const& m : plan.members)
        {
            if (!m.bot || !KnownRank(m.p, kMindControl))
                continue;
            for (Creature* us : understudies)
                if (float const d = m.p->GetExactDist2d(us); !opener || d < best)
                {
                    opener = &m;
                    best = d;
                }
        }
    }
    if (!opener)
        return;
    chosen = opener->p->GetGUID().GetCounter();
    Player* priest = opener->p;
    uint32_t const rank = KnownRank(priest, kMindControl);
    SpellInfo const* info = sSpellMgr->GetSpellInfo(rank, DIFFICULTY_NONE);
    // Mind Control's reach on an understudy, centre to centre, as the core's range check holds a cast from a standing priest
    // (Spell::CalcMinMaxRange): the spell's range with the priest's range modifiers, plus both combat reaches. naxx_razuvious af52-1:
    // the cast spots stood 18 y out (the bare 20 y less the margin) where the cast reaches about 23 y; at his 4 y/s walk the best spot's
    // way in was out of his sight 6.0 s a lap against the 7.9 s it needed, the priest waited for its moment for 3 min, the fallback pull
    // sent the main tank in alone, and the first three tanks died in 3 s before any Mind Control.
    float const range = info ? info->GetMaxRange(false, priest) : 20.0f;
    auto mcReach = [&](Unit const* target) { return range + priest->GetCombatReach() + target->GetCombatReach(); };
    float const castS = (info ? float(info->CalcCastTime()) : 3000.0f) / 1000.0f;
    // His sight against the priest, centre to centre (IdleAggroReach is edge to edge, IsWithinDistInMap adds both combat reaches).
    float const sight = IdleAggroReach(raz, priest) + raz->GetCombatReach() + priest->GetCombatReach() + kRazSightMargin;
    WaypointPath const* path = raz->GetWaypointPathId() ? sWaypointMgr->GetPath(raz->GetWaypointPathId()) : nullptr;
    // The understudies' own aggro reaches, all of them (one in a fight or charmed still stands where it stands).
    std::vector<Circle> usReach;
    {
        std::list<Creature*> all;
        raz->GetCreatureListWithEntryInGrid(all, kUnderstudy, 60.0f);
        for (Creature* c : all)
            if (c->IsAlive() && !c->IsCharmed())
                usReach.push_back({ c->GetPositionX(), c->GetPositionY(),
                                    IdleAggroReach(c, priest) + c->GetCombatReach() + priest->GetCombatReach() + kRazUsReachMargin });
    }
    auto legClear = [&](float ax, float ay, float bx, float by, bool walk)
    {
        for (Circle const& c : usReach)
            if (SegmentDist2D(ax, ay, bx, by, c.x, c.y) < c.r)
                return false;
        if (!walk)
            return true;
        // Out of the sight of his whole walk too (the priest walks this leg whenever it is sent): sampled every yard.
        float const len = Dist2D(ax, ay, bx, by);
        int const steps = std::max(1, int(std::ceil(len)));
        for (int i = 0; i <= steps; ++i)
        {
            float const f = float(i) / float(steps);
            if (RazWalkDist(raz, path, ax + (bx - ax) * f, ay + (by - ay) * f) < sight)
                return false;
        }
        return true;
    };
    auto pathClear = [&](PathGenerator const& walk, bool outOfSight)
    {
        if (walk.GetPathType() & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE))
            return false;
        Movement::PointsArray const& points = walk.GetPath();
        for (std::size_t i = 1; i < points.size(); ++i)
            if (!legClear(points[i - 1].x, points[i - 1].y, points[i].x, points[i].y, outOfSight))
                return false;
        return true;
    };

    // The spots: kept while the same priest goes for the same understudy and that one is still free.
    RazOpenSpot& spot = g_razOpenSpot[instance];
    Creature* us = nullptr;
    if (spot.priest == priest->GetGUID().GetCounter() && !spot.failedMs)
        for (Creature* c : understudies)
            if (c->GetGUID().GetCounter() == spot.understudy)
                us = c;
    if (!us)
    {
        uint32_t const now = GameTime::GetGameTimeMS();
        if (spot.priest == priest->GetGUID().GetCounter() && spot.failedMs && now - spot.failedMs < kRazSearchRetryMs)
            return;
        // Every cast spot round every free understudy, with its wait spot out along the same line; the cheap checks first, then the
        // walks, nearest first: the priest's walk to the wait spot out of the sight of his whole walk and every understudy's reach,
        // the wait spot's walk to the cast spot clear of the understudies' reaches. The shortest way in wins (the time in his sight).
        // Each spot also by the time his walk gives it each lap (RazWindowS): one his walk never leaves long enough for the run in, the
        // cast and the slack is never gone in to. naxx_razuvious af48-1: the spot west of the south-west understudy came first (the raid
        // lands west of the room); at his 4 y/s walk he stays out of sight of it about 6 s a lap, its way in takes 7 s, the priest waited
        // 3 min for its moment ("he would come 21-37 y from it, 40 y needed"), the run's fallback pull sent the main tank in, and he
        // killed it in 5 s before the first Mind Control.
        float const pace = RazPace(raz, path);
        float const run = std::max(priest->GetSpeed(MOVE_RUN), 1.0f);
        struct Candidate { Creature* us; float sx, sy, sz, wx, wy, score, window, need; bool fits; };
        std::vector<Candidate> candidates;
        int inReach = 0, noGround = 0, noWait = 0, walkFailed = 0;
        for (Creature* c : understudies)
            for (int k = 0; k < 24; ++k)
            {
                float const reach = std::max(mcReach(c) - kMcRangeMargin, 5.0f);
                float const a = float(k) * float(M_PI) / 12.0f;
                float const dx = std::cos(a), dy = std::sin(a);
                float const sx = c->GetPositionX() + reach * dx;
                float const sy = c->GetPositionY() + reach * dy;
                if (InsideAny(usReach, sx, sy, 0.0f))
                {
                    ++inReach;
                    continue;
                }
                float sz = 0.0f;
                if (!RazFloor(priest, sx, sy, c->GetPositionZ(), sz) || !c->IsWithinLOS(sx, sy, sz + 2.0f))
                {
                    ++noGround;
                    continue;
                }
                int j = 1;
                for (; j <= kRazWaitSteps; ++j)
                {
                    float const wx = sx + dx * kRazWaitStep * float(j), wy = sy + dy * kRazWaitStep * float(j);
                    if (RazWalkDist(raz, path, wx, wy) >= sight && !InsideAny(usReach, wx, wy, 0.0f))
                        break;
                }
                if (j > kRazWaitSteps)
                {
                    ++noWait;
                    continue;
                }
                float const wx = sx + dx * kRazWaitStep * float(j), wy = sy + dy * kRazWaitStep * float(j);
                float const in = kRazWaitStep * float(j);
                float const window = RazWindowS(raz, path, pace, wx, wy, sx, sy, sight);
                float const need = in / run + castS + kRazSlackS + kRazWindowSpareS;
                candidates.push_back({ c, sx, sy, sz, wx, wy, in * 4.0f + priest->GetExactDist2d(wx, wy), window, need, window >= need });
            }
        // The spots his walk gives time enough first (shortest way in first), then the rest by the time they get.
        std::sort(candidates.begin(), candidates.end(), [](Candidate const& l, Candidate const& r)
        {
            if (l.fits != r.fits)
                return l.fits;
            if (!l.fits && l.window - l.need != r.window - r.need)
                return l.window - l.need > r.window - r.need;
            return l.score < r.score;
        });
        int const fitting = int(std::count_if(candidates.begin(), candidates.end(), [](Candidate const& c) { return c.fits; }));
        Candidate const* best = nullptr;
        float bestWz = 0.0f;
        int checks = 0;
        for (Candidate const& cand : candidates)
        {
            if (checks >= kRazPathChecks)
                break;
            float wz = 0.0f;
            if (!RazFloor(priest, cand.wx, cand.wy, cand.sz, wz))
            {
                ++walkFailed;
                continue;
            }
            ++checks;
            PathGenerator toWait(priest);
            if (!BotMovement::SehSafeCalculatePath(toWait, cand.wx, cand.wy, wz) || !pathClear(toWait, true))
            {
                ++walkFailed;
                continue;
            }
            PathGenerator toCast(priest);
            if (!BotMovement::SehSafeCalculatePathFrom(toCast, cand.wx, cand.wy, wz, cand.sx, cand.sy, cand.sz) || !pathClear(toCast, false))
            {
                ++walkFailed;
                continue;
            }
            best = &cand;
            bestWz = wz;
            break;
        }
        spot = RazOpenSpot{};
        spot.priest = priest->GetGUID().GetCounter();
        if (!best)
        {
            spot.failedMs = std::max(now, 1u);
            if (g_razOpenLogged.insert(instance).second)
                TC_LOG_WARN("playerbot.v2.raid", "[raid_razuvious] {} finds no Mind Control spot with a wait spot out of his sight ({:.0f} y) "
                            "and a clear walk: {} spots inside an understudy's reach, {} without ground or sight, {} with no wait spot, "
                            "{} walks refused", priest->GetName(), sight, inReach, noGround, noWait, walkFailed);
            return;
        }
        us = best->us;
        spot.understudy = us->GetGUID().GetCounter();
        spot.sx = best->sx; spot.sy = best->sy; spot.sz = best->sz;
        spot.wx = best->wx; spot.wy = best->wy; spot.wz = bestWz;
        g_razOpenLogged.erase(instance);
        TC_LOG_INFO("playerbot.v2.raid", "[raid_razuvious] {} opens: Mind Control on an understudy from ({:.1f},{:.1f},{:.1f}), {:.1f} y from it "
                    "(the cast reaches {:.1f} y), waits at ({:.1f},{:.1f}) {:.0f} y further out, out of his sight ({:.0f} y) on his whole walk ({} path "
                    "nodes); his {:.1f} y/s walk leaves the way in out of his sight {:.1f} s a lap, {:.1f} s needed ({} of {} spots give enough)",
                    priest->GetName(), spot.sx, spot.sy, spot.sz, us->GetExactDist(spot.sx, spot.sy, spot.sz), mcReach(us), spot.wx, spot.wy,
                    Dist2D(spot.wx, spot.wy, spot.sx, spot.sy), sight,
                    path ? path->Nodes.size() : 0, pace, best->window, best->need, fitting, candidates.size());
    }

    RaidOrder& o = orders[priest->GetGUID().GetCounter()];
    o.active = true;
    o.prepull = true;
    // The way in: from the wait spot to the cast spot and the cast, while his walk does not bring him into sight of the rest of the way
    // before the cast lands (RazClosestOnWayIn). A priest not yet on that line goes to the wait spot first.
    float const px = priest->GetPositionX(), py = priest->GetPositionY();
    if (SegmentDist2D(spot.wx, spot.wy, spot.sx, spot.sy, px, py) > kRazOnLeg)
    {
        spot.goingIn = false;
        SetMove(o, spot.wx, spot.wy, spot.wz, "razuvious: wait out of his sight");
        return;
    }
    float const speed = RazPace(raz, path);
    float const run = std::max(priest->GetSpeed(MOVE_RUN), 1.0f);
    float const left = Dist2D(px, py, spot.sx, spot.sy);
    // A Mind Control already on its way has only what is left of it to run. naxx_razuvious af53-1: the priest began the cast at 09:58:38,
    // the pass after weighed a whole 3.0 s cast again from there, found him 29 y from it at the end of that, and walked it back out (the
    // cast broke); six goes in three minutes, the same each time, and the fallback pull sent the tank in alone.
    float castLeftS = castS;
    if (Spell const* cur = priest->GetCurrentSpell(CURRENT_GENERIC_SPELL); cur &&
        std::find(kMindControl.begin(), kMindControl.end(), cur->GetSpellInfo()->Id) != kMindControl.end() &&
        cur->m_targets.GetUnitTargetGUID() == us->GetGUID())
        castLeftS = std::max(0, cur->GetRemainingCastTime()) / 1000.0f;
    float const seconds = left / run + castLeftS + kRazSlackS;
    float atS = 0.0f;
    float const closest = RazClosestOnWayIn(raz, path, speed, px, py, spot.sx, spot.sy, run, castLeftS + kRazSlackS, atS);
    if (closest < sight)
    {
        if (spot.goingIn)
            TC_LOG_INFO("playerbot.v2.raid", "[raid_razuvious] {} steps back to wait: he would come {:.0f} y from it {:.1f} s into its {:.1f} s "
                        "way in, {:.0f} y needed", priest->GetName(), closest, atS, seconds, sight);
        spot.goingIn = false;
        SetMove(o, spot.wx, spot.wy, spot.wz, "razuvious: wait out of his sight");
        o.prepull_wait = true;
        dump << fmt::format("  Razuvious: {} waits out of his sight (he would come {:.0f} y from it {:.1f} s in, {:.0f} y needed, him at {:.0f},{:.0f})\n",
                            priest->GetName(), closest, atS, sight, raz->GetPositionX(), raz->GetPositionY());
        // The same in the log every 10 s: naxx_razuvious 0152 waited the whole 180 s with nothing in the log saying how near he came.
        static std::unordered_map<uint64_t, uint32_t> s_waitLogged;   // world thread only (the plan's update)
        uint32_t& at = s_waitLogged[priest->GetGUID().GetCounter()];
        uint32_t const nowMs = GameTime::GetGameTimeMS();
        if (nowMs - at >= 10000)
        {
            at = nowMs;
            TC_LOG_INFO("playerbot.v2.raid", "[raid_razuvious] {} waits for its moment: he would come {:.0f} y from it {:.1f} s into its {:.1f} s "
                        "way in, {:.0f} y needed ({:.0f} y to walk, a {:.1f} s cast), him at ({:.1f},{:.1f}) walking {:.1f} y/s", priest->GetName(),
                        closest, atS, seconds, sight, left, castS, raz->GetPositionX(), raz->GetPositionY(), speed);
        }
        return;
    }
    if (!spot.goingIn)
        TC_LOG_INFO("playerbot.v2.raid", "[raid_razuvious] {} goes in to mind control: he stays {:.0f} y or more from it on its {:.1f} s way in, "
                    "{:.0f} y needed ({:.0f} y to walk, a {:.1f} s cast), him at ({:.1f},{:.1f}) walking {:.1f} y/s", priest->GetName(), closest,
                    seconds, sight, left, castS, raz->GetPositionX(), raz->GetPositionY(), speed);
    spot.goingIn = true;
    if (left <= 1.5f && priest->GetExactDist(us) <= mcReach(us) - 0.5f && priest->IsWithinLOSInMap(us) && !priest->isMoving())
    {
        o.move = false;
        o.cast_kind = RaidOrder::CastKind::CrowdControl;
        o.cast_spell = rank;
        o.cast_target = us->GetGUID();
        dump << "  Razuvious: " << priest->GetName() << " opens with Mind Control on an understudy\n";
        return;
    }
    SetMove(o, spot.sx, spot.sy, spot.sz, "razuvious: the opening Mind Control");
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
std::unordered_set<uint32_t> g_thadDraw;                               // instances whose platform tanks are drawing the adds
constexpr float kThadDrawMargin = 3.0f;    // a draw spot stands this far inside its add's aggro reach

uint32_t ThadAddEntry(uint8_t side) { return side == kThadSideStalagg ? kStalagg : kFeugen; }

// How far from an add a raider waits for the pull: the add's aggro reach against it (the core's distance counts both combat reaches) and a
// margin for a pet at heel. naxx_thaddius 1116: both adds were in combat on a pet 5 s before the attack was ordered, Feugen's tank 23 y from
// him; Stalagg, sent after the same raider, crossed the room.
constexpr float kThadPrePullMargin = 5.0f;
float ThadPrePullClear(Player* bot, Creature* add)
{
    return add->GetAttackDistance(bot) + add->GetCombatReach() + bot->GetCombatReach() + kThadPrePullMargin;
}

// The nearest the walk along `path` comes to `unit`, in 2D (each leg measured to its closest point, not only at its corners).
float ThadPathNearest(Movement::PointsArray const& path, Unit const* unit)
{
    float const ux = unit->GetPositionX(), uy = unit->GetPositionY();
    float nearest = std::numeric_limits<float>::max();
    for (size_t i = 0; i < path.size(); ++i)
    {
        G3D::Vector3 const& a = path[i];
        G3D::Vector3 const& b = i + 1 < path.size() ? path[i + 1] : path[i];
        float const dx = b.x - a.x, dy = b.y - a.y;
        float const len2 = dx * dx + dy * dy;
        float const t = len2 > 0.0f ? std::clamp(((ux - a.x) * dx + (uy - a.y) * dy) / len2, 0.0f, 1.0f) : 0.0f;
        nearest = std::min(nearest, std::hypot(a.x + t * dx - ux, a.y + t * dy - uy));
    }
    return nearest;
}

// The side's anchor: a floor point on the add's platform at the side's distance from it, in its sight, far from the other add and reachable
// on foot from where `bot` stands. Stalagg's side stands 24 y off (outside his reach before the pull, inside every spell's after it);
// Feugen's 33 y, outside his Static Field (28135, 30 y: the mana of everyone nearer burns).
// The walk there keeps out of both adds' reach too. Each platform's ramp comes up at its front, the side facing the door (the route from one
// platform to the other runs 3412,-2977 - 3423,-2996 - 3468,-3023 - 3485,-3011), and the spot search starts at the back: naxx_thaddius r24
// sent Feugen's side to 3538,-2995 round the back of him, Feugen's tank was 13 y from him on the way and a pet at heel pulled both adds 14 s
// after the raid came in, with half the raid still walking. A spot whose walk passes nearer is only taken when no other spot exists.
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
    std::span<float const> const base = side == kThadSideStalagg ? std::span<float const>(kStalaggRadii) : std::span<float const>(kFeugenRadii);
    // Out of the add's aggro reach too, before the pull (ThadPrePullClear): the nearer radii are dropped and wider ones tried.
    float const clear = ThadPrePullClear(bot, add);
    std::vector<float> radii;
    for (float r : base)
        if (r >= clear)
            radii.push_back(r);
    for (float extra : { 0.0f, 3.0f, 6.0f, 9.0f })
        radii.push_back(std::max(clear, base.front()) + extra);
    // From the back of the platform (the bearing away from the other add) round to the front. The first pass takes only a spot whose walk keeps
    // out of both adds' reach; the second, any reachable spot.
    float const back = other->GetAbsoluteAngle(add);
    float const otherClear = ThadPrePullClear(bot, other);
    for (bool const walkClear : { true, false })
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
                float const passAdd = ThadPathNearest(path.GetPath(), add);
                float const passOther = ThadPathNearest(path.GetPath(), other);
                if (walkClear && (passAdd < clear || passOther < otherClear))
                    continue;
                out.Relocate(x, y, z);
                g_thadAnchor[key] = { true, out };
                if (walkClear)
                    TC_LOG_INFO("playerbot.v2.raid", "[raid] Thaddius: {} side spot ({:.1f},{:.1f},{:.1f}), {:.0f} y from the add (its reach and a margin {:.0f} y), "
                                "path {:.0f} y from {}, passing the add at {:.0f} y and the other at {:.0f} y",
                                side == kThadSideStalagg ? "Stalagg's" : "Feugen's", x, y, z, radius, clear, path.GetPathLength(), bot->GetName(), passAdd, passOther);
                else
                    TC_LOG_WARN("playerbot.v2.raid", "[raid] Thaddius: {} side spot ({:.1f},{:.1f},{:.1f}), {:.0f} y from the add: no spot's walk keeps out of the adds' "
                                "reach ({:.0f} y and {:.0f} y), this one passes the add at {:.0f} y and the other at {:.0f} y, path {:.0f} y from {}",
                                side == kThadSideStalagg ? "Stalagg's" : "Feugen's", x, y, z, radius, clear, otherClear, passAdd, passOther, path.GetPathLength(),
                                bot->GetName());
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
    {
        g_thadDraw.erase(first->GetInstanceId());   // the pull is on (or over): the next one is drawn afresh
        return;
    }
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
            if (z <= INVALID_HEIGHT + 1.0f || std::fabs(z - anchor.GetPositionZ()) > 2.0f || !add->IsWithinLOS(x, y, z + 2.0f) ||
                add->GetExactDist2d(x, y) < ThadPrePullClear(m.p, add))
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

    // The opening: once the raid is told to attack (a member has an add for its victim: the scenario's order onto Stalagg, a leader's
    // attack), each platform's tank walks into its own add's aggro reach, both in the same pass. A tank sent at its add from its spot
    // never got there: idle:raid_prepull holds the spot until the adds fight (naxx_thaddius r25: Mctankwa ordered onto Stalagg 22 y
    // off, his victim set, never in combat, timeout at 1500 s). Both at once, because the add pulled first sends the other at the same
    // raider (classic_boss_thaddius.cpp JustEngagedWith): r24 had Stalagg cross to Feugen's tank and the raid wipe.
    if (!g_thadDraw.count(instance))
    {
        for (Member const& m : plan.members)
            if (Unit* victim = m.p->GetVictim(); victim && (victim->GetEntry() == kStalagg || victim->GetEntry() == kFeugen))
            {
                g_thadDraw.insert(instance);
                TC_LOG_INFO("playerbot.v2.raid", "[raid] Thaddius: {} attacks {}: the platform tanks draw their adds", m.p->GetName(), victim->GetName());
                break;
            }
    }
    if (g_thadDraw.count(instance))
    {
        Member const* drawer[3] = { nullptr, nullptr, nullptr };
        for (Member const& m : plan.members)
        {
            if (!m.bot || !m.tank || !m.p->IsAlive())
                continue;
            auto it = g_thadSpot.find(m.p->GetGUID().GetCounter());
            if (it == g_thadSpot.end() || it->second.instance != instance || !it->second.side)
                continue;
            Member const*& d = drawer[it->second.side];
            if (!d || m.p == plan.mainTank)
                d = &m;
        }
        for (uint8_t side : { kThadSideStalagg, kThadSideFeugen })
        {
            if (!drawer[side])
                continue;
            Player* const tank = drawer[side]->p;
            Creature* const add = side == kThadSideStalagg ? stalagg : feugen;
            ThadSpot const& spot = g_thadSpot[tank->GetGUID().GetCounter()];
            // On the line from the add to the tank's spot (in the add's sight, on its platform), inside its reach by kThadDrawMargin.
            float const reach = add->GetAttackDistance(tank) + add->GetCombatReach() + tank->GetCombatReach();
            float const want = std::max(reach - kThadDrawMargin, 1.0f);
            float const a = add->GetAbsoluteAngle(spot.x, spot.y);
            float const x = add->GetPositionX() + want * std::cos(a);
            float const y = add->GetPositionY() + want * std::sin(a);
            float z = tank->GetMap()->GetHeight(tank->GetPhaseShift(), x, y, add->GetPositionZ() + 3.0f, true, 8.0f);
            if (z <= INVALID_HEIGHT + 1.0f || std::fabs(z - add->GetPositionZ()) > 2.5f)
                z = add->GetPositionZ();
            RaidOrder& o = orders[tank->GetGUID().GetCounter()];
            o.active = true;
            o.prepull = true;
            SetMove(o, x, y, z, side == kThadSideStalagg ? "thaddius: draw Stalagg" : "thaddius: draw Feugen");
        }
    }
    dump << "  Thaddius split:Stalagg tanks " << count[0][kThadSideStalagg] << " healers " << count[1][kThadSideStalagg] << " ranged "
         << count[2][kThadSideStalagg] << " melee " << count[3][kThadSideStalagg] << " | Feugen tanks " << count[0][kThadSideFeugen] << " healers "
         << count[1][kThadSideFeugen] << " ranged " << count[2][kThadSideFeugen] << " melee " << count[3][kThadSideFeugen] << "\n";
}

// Between the phases: Stalagg and Feugen are down, Thaddius is not yet fighting (14 s: THAD_TRANSITION in classic_boss_thaddius.cpp).
// He does not walk to the raid: at phase two he attacks the nearest raider, and while that one is out of his
// melee reach he throws Ball Lightning at it, and his first Polarity Shift comes 10 s in. So the raid walks onto his dais now: the tanks to
// his side toward the door (the nearest raider takes him), everyone else to the Positive and Negative sides of RaidRules::standByAura, half
// to each by kind, so that a shift sends only those whose charge changed across. naxx_thaddius r9-2 and r9-3: the raid stood where phase
// one ended, 100 y from him at 3433,-2998; he went to a healer, Ball Lightning killed four at 148-161 s while the tanks walked over, and
// the first shift found the raid in one heap on the way: 19 died to Positive / Negative Charge in one pass (r9-3, 124-130 s).
constexpr uint32_t kThaddius = 15928;
constexpr float    kThadTwoTankDist = 3.0f;    // the tanks stand this far from his edge, toward the door
constexpr float    kThadTwoArrive = 1.5f;      // a raider this near its dais spot stands on it
constexpr float    kThadDoorX = 3421.9f, kThadDoorY = -3017.5f;   // Thaddius Door, where the raid comes in
std::unordered_map<uint32_t, uint32_t> g_thadTwoLogged;   // instance -> when the walk to his dais was last logged
std::unordered_map<uint32_t, uint32_t> g_thadTwoSince;    // instance -> when a dead add was first seen
constexpr uint32_t kThadTwoWindowMs = 30000;

// Thaddius between the phases, or nullptr: alive and still uninteractible, both adds not alive, and a dead Stalagg or Feugen seen here
// within kThadTwoWindowMs (before the pull both stand alive; he takes their bodies away 10 s into the 14 s change, HandleUnsummonAdd).
// The raid may still count as in the fight then (r11: the plan kept its pull, so the pre-pull pass never ran and nobody walked), so the
// combat move pass (ThaddiusTwoMove) and the pre-pull pass (NaxxPrePullThaddiusTwo) both ask this.
Creature* ThadBetweenPhases(Player* ref)
{
    Creature* thad = ref->FindNearestCreature(kThaddius, 250.0f, true);
    // Uninteractible until the coils' beam hits him 13 s into the change (classic_boss_thaddius EVENT_THAD_TRANSITION_2): once that goes the
    // fight's own steps (standByAura) take over.
    if (!thad || !thad->IsUninteractible() || ref->FindNearestCreature(kStalagg, 250.0f, true) ||
        ref->FindNearestCreature(kFeugen, 250.0f, true))
        return nullptr;
    uint32_t const now = GameTime::GetGameTimeMS();
    uint32_t& since = g_thadTwoSince[ref->GetInstanceId()];
    if (ref->FindNearestCreature(kStalagg, 250.0f, false) || ref->FindNearestCreature(kFeugen, 250.0f, false))
    {
        if (!since || now - since > kThadTwoWindowMs)
            since = now ? now : 1u;
    }
    else if (!since || now - since > kThadTwoWindowMs)
        return nullptr;
    return thad;
}

// This member's spot on his dais: a tank at his side toward the door, everyone else at one of RaidRules::standByAura's two sides, half of
// each kind to each side by the member's place in the plan (the same answer in either pass), with a small ring per bot. False when the
// member is no bot, dead, out of the room, or the rules name no two sides.
bool ThadTwoSpot(Plan const& plan, Member const& self, Creature const* thad, float& x, float& y, char const*& why, int& side)
{
    if (!self.bot || !self.p->IsAlive() || self.p->GetExactDist2d(thad) > kThadRoom || !plan.rules)
        return false;
    std::vector<float> sides;
    float sideDist = 9.0f;
    for (StandByAura const& rule : plan.rules->standByAura)
        if (rule.enemy == kThaddius)
        {
            sides.push_back(rule.bearing);
            sideDist = rule.distance;
        }
    if (sides.size() < 2)
        return false;
    uint8_t const kind = ThadKind(self);
    uint32_t index = 0, sameKind = 0;
    for (Member const& m : plan.members)
    {
        if (!m.bot || !m.p->IsAlive() || m.p->GetExactDist2d(thad) > kThadRoom)
            continue;
        ++index;
        if (m.p == self.p)
            break;
        if (ThadKind(m) == kind)
            ++sameKind;
    }
    float const tx = thad->GetPositionX(), ty = thad->GetPositionY();
    if (self.tank)
    {
        float const d = thad->GetCombatReach() + kThadTwoTankDist;
        float const a = thad->GetAbsoluteAngle(kThadDoorX, kThadDoorY) + (float(index % 3) - 1.0f) * 0.35f;
        x = tx + d * std::cos(a);
        y = ty + d * std::sin(a);
        why = "thaddius: tank by him before phase two";
        side = -1;
        return true;
    }
    side = int(sameKind % 2);
    // A small ring round the side's spot, a different place per bot (the side's tolerance is 4 y).
    float const angle = float(index) * 2.4f;
    float const radius = float(index % 3) * 1.2f;
    x = tx + sideDist * std::cos(sides[size_t(side)]) + radius * std::cos(angle);
    y = ty + sideDist * std::sin(sides[size_t(side)]) + radius * std::sin(angle);
    why = side == 0 ? "thaddius: first side before phase two" : "thaddius: second side before phase two";
    return true;
}

void LogThadTwo(Player* ref, Creature const* thad, uint32_t sent, char const* pass)
{
    uint32_t const now = GameTime::GetGameTimeMS();
    uint32_t& logged = g_thadTwoLogged[ref->GetInstanceId()];
    if (logged && now - logged < 5000)
        return;
    logged = now ? now : 1u;
    TC_LOG_INFO("playerbot.v2.raid", "[raid] Thaddius: the adds are down, the raid walks onto his dais at {:.1f},{:.1f} ({} pass{}{})", thad->GetPositionX(),
                thad->GetPositionY(), pass, sent ? ", raiders sent " : "", sent ? std::to_string(sent) : std::string());
}

// The fight's move pass: each raider to its dais spot, held there once on it (no formation step takes it back toward where phase one ended).
bool ThaddiusTwoMove(Plan const& plan, Member const& self, RaidOrder& out)
{
    if (!self.bot)
        return false;
    Creature* thad = ThadBetweenPhases(self.p);
    if (!thad)
        return false;
    float x = 0.0f, y = 0.0f;
    char const* why = nullptr;
    int side = 0;
    if (!ThadTwoSpot(plan, self, thad, x, y, why, side))
        return false;
    LogThadTwo(self.p, thad, 0, "fight");
    if (self.p->GetExactDist2d(x, y) <= kThadTwoArrive && std::fabs(self.p->GetPositionZ() - thad->GetPositionZ()) < 3.0f)
    {
        out.at_post = true;
        return true;
    }
    float z = 0.0f;
    if (!Usable(self.p, x, y, thad->GetPositionZ(), z))
        return false;
    return SetMove(out, x, y, z, why);
}

// The same walk when the raid is out of combat between the phases: pre-pull orders.
void NaxxPrePullThaddiusTwo(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders, std::ostringstream& dump)
{
    Player* first = plan.members.front().p;
    Creature* thad = ThadBetweenPhases(first);
    if (!thad)
        return;
    uint32_t sent = 0;
    for (Member const& m : plan.members)
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        char const* why = nullptr;
        int side = 0;
        if (!ThadTwoSpot(plan, m, thad, x, y, why, side) || !Usable(m.p, x, y, thad->GetPositionZ(), z))
            continue;
        RaidOrder& o = orders[m.p->GetGUID().GetCounter()];
        o.active = true;
        o.prepull = true;
        SetMove(o, x, y, z, why);
        ++sent;
    }
    dump << "  Thaddius phase two: " << sent << " to his dais\n";
    LogThadTwo(first, thad, sent, "pre-pull");
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

// Phase one: a tank holds the add of the platform it stands on, whichever add is hitting it. When one add is pulled the other attacks the
// same raider (classic_boss_thaddius.cpp JustEngagedWith), and the tank assignment gives a boss to the tank it hits: naxx_thaddius 1116 had
// Feugen pulled onto Feugen's tank, Stalagg sent after it 105 y away, Stalagg handed to Feugen's tank and Feugen to Stalagg's, and both
// tanks running across the room after the wrong add. So each tank is denied the add whose platform it is not on; Stalagg's tank takes him
// back where he stands (it is the nearer). Magnetic Pull throws the tanks across and swaps the adds' victims, so the platform a tank is on,
// not the side the split gave it, says which add is its. A tank between the platforms is denied neither.
void ThaddiusTankTargets(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders)
{
    Unit* adds[2] = { nullptr, nullptr };
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kStalagg || enemy->GetEntry() == kFeugen)
            adds[enemy->GetEntry() == kStalagg ? 0 : 1] = enemy;
    if (!adds[0] || !adds[1])
        return;
    Creature* const homes[2] = { adds[0]->ToCreature(), adds[1]->ToCreature() };
    if (!homes[0] || !homes[1])
        return;
    for (Member const& m : plan.members)
    {
        if (!m.bot || !m.tank)
            continue;
        auto it = orders.find(m.p->GetGUID().GetCounter());
        if (it == orders.end())
            continue;
        int on = -1;
        for (int i = 0; i < 2; ++i)
            if (m.p->GetExactDist2d(homes[i]->GetHomePosition()) <= kThadMeleeOffSide)
                on = i;
        if (on < 0)
            continue;
        RaidOrder& o = it->second;
        o.forbidden_targets.push_back(adds[1 - on]->GetGUID());
        o.fallback_target = adds[on]->GetGUID();
    }
}

// Phase one: a tank holding Stalagg or Feugen away from the add's own platform walks it back there (the add follows its tank). The add pulled
// second goes after the first one's tank (classic_boss_thaddius.cpp JustEngagedWith) and its own tank catches it only on the way down
// (naxx_thaddius 1118 r3: Feugen sent at Stalagg's tank, Mctankwb's taunt 10 s later at 3463,-3025, below the ramp, 55 y from his platform,
// and he was held there: Magnetic Pull threw Stalagg's tank at a Feugen off his platform). Its platform is where its side fights it and
// where the other add's pull throws its tank.
constexpr float kThadAddHomeTol = 8.0f;   // an add this near its spawn is on its platform
bool ThaddiusTankMove(Plan const& plan, Member const& self, RaidOrder& out)
{
    if (!self.bot || !self.tank)
        return false;
    for (Unit* enemy : plan.enemies)
    {
        if ((enemy->GetEntry() != kStalagg && enemy->GetEntry() != kFeugen) || enemy->GetVictim() != self.p)
            continue;
        Creature* add = enemy->ToCreature();
        if (!add)
            continue;
        Position const& home = add->GetHomePosition();
        if (add->GetExactDist2d(home) <= kThadAddHomeTol || self.p->GetExactDist2d(home) <= kThadSpotTol)
            return false;
        return SetMove(out, home.GetPositionX(), home.GetPositionY(), home.GetPositionZ(),
                       add->GetEntry() == kStalagg ? "thaddius: Stalagg back to his platform" : "thaddius: Feugen back to his platform");
    }
    return false;
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
// The camp is south of his corner along the east wall (RaidEncounterData_NAXX.cpp, the ranged and healer rows): 36 y from his tank, inside
// heal reach, and 33-38 y from where he stands beside it, out of his eat reach (about 17 y from his centre since 0055; 28.5 y before). The
// camp at 3300,-3135 (0113) was 22-26 y from him: every chow that went for a healer or a ranged was eaten at the camp, and after Decimate
// they reached him in a few steps (naxx_gluth 1116 run 18: "Gluth healed" 24 times, chows gone 5-15 y (core) from him on Mchealsa/pb/drb,
// 70% -> 99% at one Decimate with eight 5% chows gone at 2 y).
constexpr float    kGluthCampX     = 3328.0f, kGluthCampY = -3150.0f;
constexpr float    kGluthDragX     = 3328.0f, kGluthDragY = -3114.0f;   // the north-east corner (floor to x 3338, y -3107), 35 y from the camp
constexpr float    kGluthFloorZ    = 297.8f;
constexpr float    kGluthArrived   = 20.0f;   // he stands this close to the drag spot: where he stands now is his spot
// He left the east wall: farther than this from the drag spot, drag him again. Not a distance from the spot where he stopped: he steps
// after his tank by up to its 10 y step, and at 12 y from that spot (0085) the step itself sent the tank back to the wall, every few
// seconds (naxx_gluth 1115h: the holder moved 62 of 90 s between the wall and its step, 15 white hits and no Sunder in 90 s at 86 rage,
// and every DPS stood 26-29 s in the threat hold under it). He settles within 20 y of the drag spot and his tank stands 10 y past him:
// even on top of his tank he is within 30 y of it.
constexpr float    kGluthLeftWall  = 32.0f;
constexpr float    kGluthSwapSide  = 5.0f;    // the swap tank stands this far beside the holder's spot
constexpr float    kGluthSpotTol   = 3.0f;
constexpr float    kGluthChowTol   = 5.0f;
constexpr uint32_t kGluthStaleMs   = 10000;   // an anchor not looked at for this long belongs to an earlier pull
constexpr uint32_t kGluthDragHoldMs = 15000;  // the longest the DPS wait for the drag (the holder runs 50-70 y with him at its back)
struct GluthPost { float x, y; };
constexpr GluthPost kGluthChowPosts[] = {
    // On the chows' way to the camp, where most of them go (a chow starts on a random raider, then on whoever heals near it): from the south
    // spawn (3308,-3186) it passes 3318,-3168, from the south-west one (3268,-3172) 3298,-3161, from the west one (3253,-3132) 3290,-3141.
    // The posts by the spawns (until 0062: 3302,-3176 and 3292,-3164) were 16-25 y off those lines, so the chow tanks ran after every chow
    // across the room and back: naxx_gluth 1118 r2, 74-76 of their first 90 s moving, no Swipe or Thunder Clap, the chows they held back on the
    // healers, the druid chow tank dead at 75-104 s, before the first Decimate. Still 51-65 y from him, out of his eat reach after Decimate
    // (from the west post 0120 had at 38 y the 5% chows were in his reach within 2 s: naxx_gluth 1116 run 19, eaten 4 s later, 76% -> 99%).
    { 3316.0f, -3170.0f },   // south: 4 y off the south spawn's way, 23 y from the camp, 65 y from him
    { 3296.0f, -3152.0f },   // west: 8-10 y off the south-west and west spawns' ways, 32 y from the camp, 51 y from him
};
// A chow tank that already holds chows takes a loose chow only this near its post: one sent across the room after a chow on a healer
// dragged its own chows along, or left them, and came back (1118 r2, Mctankda t=78-86 s: soak and post in turn every second).
constexpr float kGluthPostPickup = 15.0f;
// A chow tank holding this many fewer chows than the other goes further for a loose one: as far as the other post (the posts are 27 y apart).
constexpr int   kGluthBalanceGap     = 2;
constexpr float kGluthBalancePickup  = 32.0f;
// A chow tank holding this many more chows than another has one taunted off it by the lighter one (the taunt ready, the chow within
// kGluthBalancePickup of the lighter one's post). The chows come along the west post's way from two of the three spawns and stay on whoever
// hits them first: the bear there held 5-7 to the warrior's 1-3 and died at 55-98 s in every naxx_gluth 1118 run from r4 to r8; in r8-3 its
// six chows then went to the healers and on to his tanks, and he ate them (57% -> 82%).
constexpr int   kGluthShedGap        = 3;
// A loose chow this near him, or on a raider this near him (his tanks, a healer by them), walks into his reach: any chow tank goes for it,
// however many it holds and however far from its post (both posts are 51-65 y from him).
constexpr float kGluthReachNear      = 32.0f;
// A warrior chow tank's walls (NaxxGluthChowPickups): this many chows on it and at or under this health.
constexpr int      kGluthWallChows  = 4;
constexpr float    kGluthWallPct    = 50.0f;
constexpr uint32_t kGluthShieldWall = 871;
constexpr uint32_t kGluthLastStand  = 12975;
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
    // Two clocks call this: the bots' moves with the brain's update time (getMSTime, taken after the world tick began) and the target
    // election with GameTime (taken at its start), a few ms apart. Unsigned, a look a few ms "in the past" read as 49 days stale and wiped
    // the spot every plan: the holder alternated "drag to the east wall" and "camp side of him" every second (naxx_gluth 1116 run 10) and
    // he walked to and fro between the wall and the camp. Signed, a look that is not newer is simply not stale.
    if (!a.seen || int32_t(now - a.seen) > int32_t(kGluthStaleMs))
        a = GluthAnchor{};
    if (!a.seen || int32_t(now - a.seen) > 0)
        a.seen = now ? now : 1u;
    float const gx = gluth->GetPositionX(), gy = gluth->GetPositionY();
    if (a.settled && Dist2D(gx, gy, kGluthDragX, kGluthDragY) > kGluthLeftWall)
        a.settled = false;
    // He settles where he has stopped, not where he first comes within kGluthArrived: he walks in from the camp side, so the first point
    // inside the circle was always its camp edge, and the holder's step (10 y toward the camp, until 0114) from there put him by the camp
    // (naxx_gluth 1116 run 5: the main tank on "camp side of him" at x=3292 y=-3110, 2 y from the camp, dead at 24 s with the raid round him).
    // ... and only once his tank has reached the drag spot: on the way he stops each time he catches up with it, and settling at the first
    // such stop left him at the camp edge of the circle, the holder's step 10 y nearer the camp (naxx_gluth 1116 run 9: him at x=3299-3313
    // y=-3120 for the whole fight, 15-25 y from the camp and on the chows' way from the south spawn, 1.65M damage on him and 99% at the end).
    Unit const* tank = gluth->GetVictim();
    bool const tankAtWall = tank && Dist2D(tank->GetPositionX(), tank->GetPositionY(), kGluthDragX, kGluthDragY) <= kGluthSpotTol + 1.0f;
    if (!a.settled && tankAtWall && !gluth->isMoving() && Dist2D(gx, gy, kGluthDragX, kGluthDragY) <= kGluthArrived)
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

// A chow tank sent to take a chow off the chow tank that holds too many (NaxxGluthChowPickups): the chow, and until when the walk over holds.
struct GluthGoOver { ObjectGuid chow; uint32_t until = 0; };
std::unordered_map<uint64_t, GluthGoOver> g_gluthGoOver;
constexpr uint32_t kGluthGoOverMs = 4000;   // the walk over is renewed by every pickups pass that still wants it

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

// A chow on this bot, alive and still a chow the tanks hold (not one Decimate sent walking to him: it is left at 5%). Any chow whose victim
// the bot is, not only the one the bot attacks: the swap tank turns to Gluth for its threat while its chows keep hitting it, and the
// old test (the bot's own victim) then read "no chow" and sent it back beside him with the chows at its back (naxx_gluth 1116 run 4:
// Mctankwb between "chow post" and "beside his tank" every few seconds, about 32 of the 59 chows eaten, 3.2M damage on him and 99% at the end).
constexpr float kGluthDecimatedPct = 10.0f;
bool ChowOnMe(Plan const& plan, Player* bot)
{
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kZombieChow && enemy->IsAlive() && enemy->GetVictim() == bot && !enemy->HealthBelowPct(kGluthDecimatedPct))
            return true;
    return false;
}

// A raider a chow has gone for keeps the chow out of his eat reach, and the chow tanks pick it up where it is (NaxxGluthChowPickups). Without
// this the raider stayed by him or ran to the main tank by him, and the chow came within his 15 y and was eaten (naxx_gluth 1116 run 3:
// 12 loose chows, Gluth back to 99% after each 10% the raid took off; run 13, him held in the corner: chows on Mchealsb, Mchealdrc, Mchealpd,
// Mchealdra and Mchuntd gone 7-17 y from him at full health, led to a chow tank next to him).
// Only a step straight away from him, never a walk to a chow post: chows go mostly for healers through their healing threat, and a healer at a
// post 46-65 y from his corner is out of heal reach of his tank (run 14: "take the chow to a chow post" about 90 times, mostly healers; all four
// tanks dead by about 110 s, then him loose on the camp).
// His eat reach is the script's: a chow whose 2D distance to him is under 15 y, the core's distance, which takes off both combat reaches
// (classic_boss_gluth.cpp DoSearchZombieChow, WorldObject::GetDistance2d). His combat reach is a dozen yards, so a chow is eaten up to about
// 30 y from his centre; until 0119 this read 15 y from his centre and the raider stopped at 27 y, inside it (naxx_gluth 1116 run 17: 4.07M
// damage on him, him at 80-99% for 348 s, about 40 chows eaten and 6 of them logged).
// 0055: the script now measures the gap between the two bodies as VMaNGOS does (centre distance less both bounding radii, his 1.5 y and a
// chow's 0.65 y), not TC's GetDistance2d less both combat reaches: a chow is eaten within about 17 y of his centre, not 28.5 y.
constexpr float kGluthEatReach  = 15.0f;   // classic_boss_gluth.cpp DoSearchZombieChow: body gap < 15
constexpr float kChowBounding   = 0.65f;   // Zombie Chow bounding radius (creature_model_info, all four models)
constexpr float kGluthChowSide  = 3.0f;    // a chow on a raider stands within its melee: its combat reach and a step
constexpr float kGluthEatMargin = 4.0f;
// How far from his centre (2D) a chow is eaten.
float GluthEatCentre(Unit* gluth)
{
    return kGluthEatReach + gluth->GetBoundingRadius() + kChowBounding;
}
// The body gap the script tests (classic_boss_gluth.cpp).
float GluthChowGap(Unit* gluth, Unit* chow)
{
    return gluth->GetExactDist2d(chow) - gluth->GetBoundingRadius() - chow->GetBoundingRadius();
}
// The centre distance from him a raider with a chow on it keeps (its chow then stays out of his eat reach).
float GluthChowSafeCentre(Unit* gluth, Unit* bot)
{
    return GluthEatCentre(gluth) + kGluthChowSide + kGluthEatMargin + bot->GetCombatReach();
}
bool GluthChowCarry(Plan const& plan, Member const& self, Unit* gluth, RaidOrder& out)
{
    Player* bot = self.p;
    if (!ChowOnMe(plan, bot))
        return false;
    float const safe = GluthChowSafeCentre(gluth, bot);
    float const d = bot->GetExactDist2d(gluth);
    if (d >= safe)
        return false;
    out.post_enemy = gluth->GetGUID();
    float const gx = gluth->GetPositionX(), gy = gluth->GetPositionY();
    float const k = (safe + 2.0f) / std::max(d, 0.1f);
    if (GluthMoveTo(bot, out, gx + (bot->GetPositionX() - gx) * k, gy + (bot->GetPositionY() - gy) * k, 1.0f, "gluth: take the chow out of his reach"))
        return true;
    // Straight away from him is the wall (a raider between him and his corner): out toward the camp instead.
    float const c = std::max(std::hypot(kGluthCampX - gx, kGluthCampY - gy), 0.1f);
    float const kc = (safe + 2.0f) / c;
    return GluthMoveTo(bot, out, gx + (kGluthCampX - gx) * kc, gy + (kGluthCampY - gy) * kc, 1.0f, "gluth: take the chow out of his reach");
}

// Taunt is a melee-range spell (355 and Growl 6795: range index 2 in the client data), so a chow tank must stand next to a loose chow to take
// it, and a chow runs as fast as the tank: chased from behind it is caught only where it stops, by its raider. One that went for his tank or
// a melee DPS stops beside him and is eaten (naxx_gluth 1116 run 19: 12 of the 28 eats were chows on Mctankwa / Mctankwb at full health,
// gone 0-16 y (core) from him). The chow tank goes to the point of the chow's way that it reaches first, short of his eat reach, and waits
// there for it.
bool GluthCutOff(Player* bot, Unit* chow, Unit* gluth, RaidOrder& out)
{
    Unit* target = chow->GetVictim();
    if (!target || !chow->isMoving())
        return false;
    float const ax = chow->GetPositionX(), ay = chow->GetPositionY();
    float const dx = target->GetPositionX() - ax, dy = target->GetPositionY() - ay;
    float const len2 = dx * dx + dy * dy;
    if (len2 < 1.0f)
        return false;
    float t = ((bot->GetPositionX() - ax) * dx + (bot->GetPositionY() - ay) * dy) / len2;
    t = std::clamp(t, 0.0f, 1.0f);
    // Short of his eat reach: the chow must be taken before it gets there.
    float const reach = GluthEatCentre(gluth) + kGluthChowSide + kGluthEatMargin;
    while (t > 0.0f && Dist2D(ax + dx * t, ay + dy * t, gluth->GetPositionX(), gluth->GetPositionY()) < reach)
        t = std::max(0.0f, t - 0.05f);
    float const px = ax + dx * t, py = ay + dy * t;
    float const chowToP = t * std::sqrt(len2);
    float const botToP = bot->GetExactDist2d(px, py);
    if (t <= 0.0f || chowToP <= botToP + 2.0f)
        return false;   // it gets there first: chase it (the tank brain's pick-up)
    out.post_enemy = chow->GetGUID();
    return GluthMoveTo(bot, out, px, py, 2.0f, "gluth: cut off the chow");
}

bool GluthMove(Plan const& plan, Member const& self, uint32_t now, RaidOrder& out)
{
    if (!self.bot)
        return false;
    Unit* gluth = FindGluth(plan);
    if (!gluth)
        return false;
    if (!self.tank)
        return GluthChowCarry(plan, self, gluth, out);
    int post = -1;
    GluthJob job = GluthJobOf(plan, gluth, self.p, post);
    Player* bot = self.p;
    // The swap tank stays beside his tank whatever is on it: it never attacks a chow (NaxxGluthTargets), and a chow tank taunts a chow that
    // went for it (NaxxGluthChowPickups). Until 0117 one with a chow on it took the chow to a post, and the chows kept coming to the tank
    // that had hit them: it stayed a chow tank for the whole fight, Mortal Wound went on his tank unswapped and it died at 5+ stacks
    // (naxx_gluth 1116 run 15: Mctankwb at the west post from 6 s, no taunt swap in 62 s, Mctankwa dead at 62 s with him at 75%, then him
    // loose on the camp; 1115f, before the redirect: Mctankwb held chows 3-14 y from him and he ate them).
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
        // The holder stays on the drag spot, the swap tank beside it. A step toward the camp from where he stopped (10 y, until 0114)
        // did not leave him there: he follows his tank to melee, so each step took him 10 y nearer the camp (naxx_gluth 1116 run 12: drag
        // done at 8 s, then "camp side of him" and him at x=3299-3308 y=-3122..-3137 for the rest of the fight, on top of the camp).
        float const cx = kGluthCampX - kGluthDragX, cy = kGluthCampY - kGluthDragY;
        float const len = std::max(0.1f, std::sqrt(cx * cx + cy * cy));
        float const ux = cx / len, uy = cy / len;
        float x = kGluthDragX, y = kGluthDragY;
        if (job == GluthJob::Swap)
        {
            x -= uy * kGluthSwapSide;
            y += ux * kGluthSwapSide;
        }
        return GluthMoveTo(bot, out, x, y, kGluthSpotTol, job == GluthJob::Holder ? "gluth: hold him in the corner" : "gluth: beside his tank");
    }
    if (job != GluthJob::Chows)
        return false;
    // A chow tank sent to take a chow off the other chow tank walks to that chow until its taunt is in reach: the walk back to its own post
    // won over the walk over on every pass, and the taunt (4 y) never came. naxx_gluth 1118 r9-2: Mctankwc "goes over" to Mctankda's chows
    // five times in 21 s (5, 6, 7 chows on Mctankda, 1 on Mctankwc), the post walk took it back each time, and Mctankda died at 60 s.
    if (auto it = g_gluthGoOver.find(bot->GetGUID().GetCounter()); it != g_gluthGoOver.end())
    {
        Unit* chow = nullptr;
        if (now <= it->second.until)
            for (Unit* enemy : plan.enemies)
                if (enemy->GetGUID() == it->second.chow)
                    chow = enemy;
        if (!chow || !chow->IsAlive() || chow->GetVictim() == bot || chow->HealthBelowPct(kGluthDecimatedPct))
            g_gluthGoOver.erase(it);
        else if (bot->GetExactDist2d(chow) > 3.0f)
        {
            float z = 0.0f;
            if (Usable(bot, chow->GetPositionX(), chow->GetPositionY(), chow->GetPositionZ(), z))
                return SetMove(out, chow->GetPositionX(), chow->GetPositionY(), z, "gluth: over to take a chow off the other chow tank");
        }
        else
            return false;   // in reach: the pickups pass elects its taunt
    }
    // A chow tank going for a loose chow cuts it off on its way, or runs to it (the tank brain's pick-up); one with a chow on it, or with
    // nothing, holds its post.
    if (Unit* victim = bot->GetVictim())
        if (victim->GetEntry() == kZombieChow && victim->IsAlive() && victim->GetVictim() != bot)
            return GluthCutOff(bot, victim, gluth, out);
    GluthPost const& p = kGluthChowPosts[post];
    return GluthMoveTo(bot, out, p.x, p.y, kGluthChowTol, "gluth: chow post");
}

} // namespace

void NaxxGluthTargets(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders);

// After PublishTargets: in Thaddius' phase one a non-tank bot only attacks the add on its own platform (the other is 85 y away and a 194 y
// walk round). The kill-together rule's spread still holds: an own add it forbids (more than 10% ahead of the other) leaves the bot with
// nothing to hit until the other side catches up.
// ---------------------------------------------------------------------------
// The Four Horsemen: corner seats chosen by the marks each raider carries
// ---------------------------------------------------------------------------
// Each horseman marks everyone within his mark's radius every 12 s (classic_boss_four_horsemen.cpp: the first 20 s after the pull, then
// 12 s, the four timers kept together): Mograine 28834 and Zeliek 28835 reach 70 y, Korth'azz 28832 and Blaumeux 28833 65 y (SpellEffect
// radius index 25 / 24, SpellRadius 70938). A mark lasts 75 s (SpellDuration 62) and each new one adds a stack and hurts by the stacks it
// brings: 2 -> 250, 3 -> 1000, 4 -> 3000, 5 and more -> 1000 a stack (SpellHitTarget). Their tanks hold them in the room's four corners
// (RaidEncounterData_NAXX.cpp), where Korth'azz and Mograine stand 59 y apart (inside each other's marks) and the others 70-120 y apart.
// Every raider that is not holding a horseman takes a seat: a melee one at a horseman's side, a ranged one or a healer on a ring 27 y
// round him. The seat is the one whose next mark costs least, read from the stacks the raider carries and when they fall off; a raider
// changes seats only in the few seconds after a mark (or at once when its seat is gone), so it walks between marks, as a raid rotates.
// Healers are spread so that every held horseman has his share of them in reach of his tank. Zeliek's Holy Wrath (28883) leaps 10 y from
// raider to raider and doubles with each leap, on a raider within his 35 y (SpellRange 35): only ranged seats and healers sit within his
// reach, 12 y apart and from his tank, and no melee seat is at his side. naxx21 1116: the raid fought all four where they stood, 23-25 y from
// it at the door, and one Holy Wrath killed 17 at 12 s.
// The seats are laid round each horseman's corner, not round where he stands: all four run at whoever they see first (74 y,
// classic_boss_four_horsemen.cpp AggroRadius), so for the first seconds they stand together in the raid at its landing, and seats round
// them there kept the raid in one heap. naxx22 1116: Zeliek's first Holy Wrath at 12 s killed 9 in that heap, 7.5-19 y from him, all four
// tanks among them. From the pull the ranged and the healers walk to their corner seats, 12 y apart wherever they are (a Holy Wrath
// thrown while Zeliek is dragged across the room finds nobody within its leap), and the melee wait beside a corner until its horseman
// stands in it.
struct HorseDef { uint32_t entry; uint32_t mark; float radius; char const* name; float cx, cy, cz; };
constexpr HorseDef kHorse[4] = {   // the corners: RaidEncounterData_NAXX.cpp, the tanks' stand rows
    { 16062, 28834, 70.0f, "Mograine",  2584.32f, -2971.96f, 241.35f }, { 16064, 28832, 65.0f, "Korth'azz", 2542.70f, -3014.06f, 241.34f },
    { 16065, 28833, 65.0f, "Blaumeux",  2468.26f, -2947.50f, 241.28f }, { 16063, 28835, 70.0f, "Zeliek",    2517.64f, -2897.25f, 241.28f } };
constexpr float    kHorseInCorner = 12.0f;       // a horseman this close to his corner is held in it: his melee go in
constexpr int      kHorseZeliek = 3;
constexpr int      kHorseKorthazz = 1;
constexpr uint32_t kHorseVoidZone = 16697;     // Blaumeux's Void Zone (7 y avoid circle in the rules)
constexpr int32_t  kMarkEveryMs = 12000;
constexpr int32_t  kMarkLastsMs = 75000;
constexpr int32_t  kHorseReseatWindowMs = 5000;  // after a mark, the time a raider may change seats
constexpr float    kHorseMarkMargin = 3.0f;      // a seat this close to a mark's edge counts as inside it
constexpr float    kHorseRing = 27.0f;           // a ranged seat's distance from its horseman
constexpr float    kHorseReach = 36.0f;          // a ranged raider attacks a horseman this close to its seat
constexpr float    kHolyWrathReach = 35.0f + 3.0f;
constexpr float    kHolyWrathLeap = 12.0f;       // 10 y and a margin; every ranged and healer seat keeps it from the others
constexpr float    kHolyWrathLane = kHolyWrathLeap + 2.0f;   // no seat this close to Zeliek's way from where he stands to his corner
constexpr float    kHorseMeleeWait = 8.0f;       // a melee raider waits this far from its horseman's corner, toward the room's middle
constexpr float    kHorseVoidClear = 7.5f;       // a seat or a walk keeps this far from a Void Zone (its hits reach 5 y)
constexpr uint32_t kHorseSwitchCost = 200;       // a new seat must save at least this much of the next mark
constexpr uint32_t kHorseHealerSwitchCost = 800; // a healer's new seat must save this much: it walks only from a third stack on
constexpr int32_t  kHorseWalkInLeadMs = 1500;    // a next tank waiting inside his mark sets off this much before its walk in would end on it
constexpr int      kKorthMeteorShare = 2;        // melee DPS kept by Korth'azz's side for his Meteor, beside his tank

uint32_t MarkDamage(uint32_t stacks)
{
    switch (stacks)
    {
        case 0: case 1: return 0;
        case 2: return 250;
        case 3: return 1000;
        case 4: return 3000;
        default: return 1000u * stacks;
    }
}

// wx/wy/wz (wait true): where a melee raider waits while its horseman is not yet in his corner (HorsemenSeats).
// spare: a tank that holds no horseman; x/y/z is where it waits, out of the mark of <horse> when next is set (it takes him over at his tank's
// third mark, HorsemenMove), else the spot whose next mark costs it least while its stacks fall off.
struct HorseSeat
{
    uint32_t instance = 0; int horse = -1; bool melee = false; float x = 0, y = 0, z = 0; uint32_t cost = 0; bool wait = false; float wx = 0, wy = 0, wz = 0;
    bool spare = false; bool next = false;
};
std::unordered_map<uint64_t, HorseSeat> g_horseSeat;       // raider -> its seat
std::unordered_map<uint64_t, uint32_t> g_horsePassAt;       // group -> the plan time its seats were chosen
std::unordered_map<uint64_t, int32_t> g_horseToNext;        // group -> ms to the next mark, as read on that pass
std::unordered_map<uint64_t, uint32_t> g_horseWalkInLog;    // spare tank -> when its walk in was last logged

Unit* HorseHolderOf(Unit* horse) { return horse ? horse->GetVictim() : nullptr; }

// The marks at which a horseman's tank hands him over (RaidRules::tankSwap).
uint32_t HorseSwapStacks(Plan const& plan, uint32_t entry)
{
    if (plan.rules)
        for (TankSwap const& rule : plan.rules->tankSwap)
            if (rule.enemy == entry)
                return std::max<uint32_t>(rule.stacks, 1u);
    return 3;
}

// The marks a tank may carry of a horseman's when it takes him over (RaidRules::tankSwap takerStacks).
uint32_t HorseTakerStacks(Plan const& plan, uint32_t entry)
{
    if (plan.rules)
        for (TankSwap const& rule : plan.rules->tankSwap)
            if (rule.enemy == entry)
                return rule.takerStacks;
    return 1;
}

// A walk to (x, y) that passes a Void Zone goes round it: the point beside the zone, kHorseVoidClear and a step out, on the bot's side of
// the line. Returns false when the straight walk is clear. naxx_horsemen r29: Mcmaged left a zone, was sent back to its seat across it on
// the next pass, and died on the way (21:33:18, 5.8 y from it); two more died walking past zones.
bool HorseVoidDetour(Player* bot, float x, float y, float& dx, float& dy)
{
    std::list<Creature*> voids;
    bot->GetCreatureListWithEntryInGrid(voids, kHorseVoidZone, bot->GetExactDist2d(x, y) + kHorseVoidClear + 2.0f);
    float const bx = bot->GetPositionX(), by = bot->GetPositionY();
    float const lx = x - bx, ly = y - by, len2 = lx * lx + ly * ly;
    Creature const* worst = nullptr;
    float worstT = 2.0f;
    for (Creature const* v : voids)
    {
        if (!v->IsAlive() || SegmentDist2D(bx, by, x, y, v->GetPositionX(), v->GetPositionY()) >= kHorseVoidClear)
            continue;
        float const t = len2 > 0.01f ? ((v->GetPositionX() - bx) * lx + (v->GetPositionY() - by) * ly) / len2 : 0.0f;
        if (!worst || t < worstT) { worst = v; worstT = t; }   // the first zone on the way
    }
    if (!worst)
        return false;
    // Out from the zone's centre, square to the walk, on the side the bot stands.
    float const len = std::sqrt(std::max(len2, 0.01f));
    float nx = -ly / len, ny = lx / len;
    if ((bx - worst->GetPositionX()) * nx + (by - worst->GetPositionY()) * ny < 0.0f) { nx = -nx; ny = -ny; }
    dx = worst->GetPositionX() + (kHorseVoidClear + 1.5f) * nx;
    dy = worst->GetPositionY() + (kHorseVoidClear + 1.5f) * ny;
    return true;
}

// The move to a horsemen spot, round any Void Zone on the way.
bool HorseWalk(Player* bot, float x, float y, float zHint, char const* reason, RaidOrder& out)
{
    float dx = 0.0f, dy = 0.0f, z = 0.0f;
    if (HorseVoidDetour(bot, x, y, dx, dy))
    {
        if (!Usable(bot, dx, dy, bot->GetPositionZ(), z, /*urgent*/ true))
            return false;
        return SetMove(out, dx, dy, z, "horsemen: round a Void Zone");
    }
    if (!Usable(bot, x, y, zHint, z, /*urgent*/ true))
        return false;
    return SetMove(out, x, y, z, reason);
}

// Seats for this plan, once for the whole raid.
void HorsemenSeats(Plan const& plan, uint32_t now)
{
    uint32_t& passAt = g_horsePassAt[plan.groupLow];
    if (passAt == now)
        return;
    passAt = now;
    Unit* horses[4] = {};
    for (Unit* enemy : plan.enemies)
        for (int h = 0; h < 4; ++h)
            if (enemy->GetEntry() == kHorse[h].entry)
                horses[h] = enemy;
    if (plan.members.empty())
        return;
    uint32_t const instance = plan.members.front().p->GetInstanceId();
    // When the last mark landed: the youngest mark on anyone. Before the first one nobody carries a mark and every seat costs nothing.
    int32_t youngest = -1;
    for (Member const& m : plan.members)
        for (HorseDef const& d : kHorse)
            if (Aura const* a = m.p->GetAura(d.mark))
            {
                int32_t const age = a->GetMaxDuration() - a->GetDuration();
                if (youngest < 0 || age < youngest)
                    youngest = age;
            }
    int32_t const sinceMark = youngest < 0 ? kMarkEveryMs : youngest % kMarkEveryMs;
    int32_t const toNext = kMarkEveryMs - sinceMark;
    if (g_horseToNext.size() > 64)
        g_horseToNext.clear();
    if (g_horseWalkInLog.size() > 256)
        g_horseWalkInLog.clear();
    g_horseToNext[plan.groupLow] = toNext;
    bool const window = youngest >= 0 && sinceMark < kHorseReseatWindowMs;
    // The stacks a raider will carry when the next mark lands, before it adds one.
    auto stacksAtNext = [&](Player* p, int h) -> uint32_t
    {
        Aura const* a = p->GetAura(kHorse[h].mark);
        return a && a->GetDuration() > toNext ? a->GetStackAmount() : 0;
    };
    auto markCost = [&](Player* p, float x, float y) -> uint32_t
    {
        uint32_t cost = 0;
        for (int h = 0; h < 4; ++h)
            if (horses[h] && Dist2D(x, y, kHorse[h].cx, kHorse[h].cy) <= kHorse[h].radius + kHorseMarkMargin)
                cost += MarkDamage(stacksAtNext(p, h) + 1);
        return cost;
    };
    std::list<Creature*> voids;
    plan.members.front().p->GetCreatureListWithEntryInGrid(voids, kHorseVoidZone, 200.0f);
    auto nearVoid = [&](float x, float y)
    {
        for (Creature* v : voids)
            if (v->IsAlive() && Dist2D(x, y, v->GetPositionX(), v->GetPositionY()) < kHorseVoidClear)
                return true;
        return false;
    };
    // The ring seats round each horseman's corner: on its floor and in its sight. Read once (the room does not change; every instance
    // of the map has the same floor), under a lock (instances update on their own threads).
    struct Spot { int horse; float x, y, z; };
    static std::mutex s_ringMu;
    static std::vector<Spot> s_ring;
    std::vector<Spot> ringAll;
    {
    std::lock_guard<std::mutex> lk(s_ringMu);
    if (s_ring.empty())
    {
        Player* const anyone = plan.members.front().p;
        Map* const map = anyone->GetMap();
        for (int h = 0; h < 4; ++h)
            for (int k = 0; k < 24; ++k)
            {
                float const a = float(k) * float(M_PI) / 12.0f;
                float const x = kHorse[h].cx + kHorseRing * std::cos(a);
                float const y = kHorse[h].cy + kHorseRing * std::sin(a);
                float const z = map->GetHeight(anyone->GetPhaseShift(), x, y, kHorse[h].cz + 3.0f, true, 8.0f);
                if (z <= INVALID_HEIGHT + 1.0f || std::fabs(z - kHorse[h].cz) > 3.0f
                    || !map->isInLineOfSight(anyone->GetPhaseShift(), x, y, z + 2.0f, kHorse[h].cx, kHorse[h].cy, kHorse[h].cz + 2.0f,
                                             LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::M2))
                    continue;
                s_ring.push_back({ h, x, y, z });
            }
        TC_LOG_INFO("playerbot.v2.raid", "[raid_horsemen] {} ring seats round the four corners", s_ring.size());
    }
    ringAll = s_ring;
    }
    std::vector<Spot> ring;
    for (Spot const& s : ringAll)
        if (horses[s.horse] && !nearVoid(s.x, s.y))
            ring.push_back(s);
    Unit* const zeliek = horses[kHorseZeliek];
    Unit* const zeliekTank = HorseHolderOf(zeliek);
    // Ranged and healer seats taken this pass, and Zeliek's tank: a new one keeps a leap from each.
    std::vector<std::pair<float, float>> wrathSeats;
    std::vector<std::pair<float, float>> rangedSeats;   // ranged seats taken this pass: two raiders do not share one
    if (zeliekTank)
        wrathSeats.push_back({ zeliekTank->GetPositionX(), zeliekTank->GetPositionY() });
    // Zeliek away from his corner is on his way to it (his tank drags him there): every seat keeps a Holy Wrath leap and a margin from that
    // way, not only from his corner and his tank's spot now. naxx_horsemen af48-1: all four opened on one tank by the raid's landing spot;
    // its drag to Zeliek's corner (2561,-3005 to 2517,-2897) ran through the Korth'azz healer and ranged seats, kept from before the pull
    // and checked against his corner only, and one Holy Wrath at 12 s jumped through fourteen (Zeliek at 2552.5,-3000.1, two healers
    // on their seats 9-11 y off his tank).
    bool const zeliekAway = zeliek &&
        Dist2D(zeliek->GetPositionX(), zeliek->GetPositionY(), kHorse[kHorseZeliek].cx, kHorse[kHorseZeliek].cy) > kHorseInCorner;
    auto inLane = [&](float x, float y)
    {
        if (!zeliekAway)
            return false;
        float const ax = zeliek->GetPositionX(), ay = zeliek->GetPositionY();
        float const bx = kHorse[kHorseZeliek].cx, by = kHorse[kHorseZeliek].cy;
        float const vx = bx - ax, vy = by - ay;
        float const len2 = vx * vx + vy * vy;
        float t = len2 > 0.0f ? ((x - ax) * vx + (y - ay) * vy) / len2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        return Dist2D(x, y, ax + t * vx, ay + t * vy) < kHolyWrathLane;
    };
    auto inWrath = [&](float x, float y)
    {
        return zeliek && (Dist2D(x, y, kHorse[kHorseZeliek].cx, kHorse[kHorseZeliek].cy) <= kHolyWrathReach || inLane(x, y));
    };
    auto wrathClear = [&](float x, float y)
    {
        if (inLane(x, y))
            return false;
        for (auto const& [sx, sy] : wrathSeats)
            if (Dist2D(x, y, sx, sy) < kHolyWrathLeap)
                return false;
        return true;
    };
    // A raider holding a horseman is his tank and keeps no seat.
    auto holds = [&](Player* p)
    {
        for (Unit* horse : horses)
            if (horse && horse->GetVictim() == p)
                return true;
        return false;
    };
    // Healers each held horseman needs in reach of his tank.
    int held = 0;
    for (Unit* horse : horses)
        if (horse && HorseHolderOf(horse))
            ++held;
    int healers = 0;
    for (Member const& m : plan.members)
        if (m.bot && m.healer && !holds(m.p))
            ++healers;
    int const healersEach = held ? healers / held : 0;
    int healersAt[4] = {};
    // Healers who leave each horseman's seats in this pass. At most half of a horseman's share go at one mark, unless the next mark there
    // takes a third of their health: the others stay with his tank until the next one. naxx_horsemen af52-1: at the first mark (19 s) all
    // twelve healers changed corners at once, walking 60-100 y (the mark there cost 250-500, the new seat 0), again at 31 s and 43 s; in the
    // first 30 s 0-3 of them cast at any tick at 96-99% mana, Blaumeux's bear tank had no healer in reach from 19 s to 27 s (91% -> 40%) and
    // died at 44 s.
    int healersLeft[4] = {};
    int const healerLeaveCap = std::max(1, (healersEach + 1) / 2);
    // Korth'azz's Meteor (28884) lands on a random raider within 20 y of him and is split among everybody within 8 y of it: the melee seated
    // by him share it with his tank, and they leave him only while kKorthMeteorShare of them stay (they still leave a seat whose next mark
    // takes a third of their health). naxx_horsemen af52-1: at the first mark all four rogues moved from his side to Blaumeux's at once
    // (that mark costs 0 there, 500 by him), the next Meteors fell whole on a walker (13132, 13430) and then split two ways on his tank
    // (6444), which died at 59 s; with him loose the raid's seats came apart and the marks from 56 s to 104 s killed 22.
    int korthMelee = 0;
    for (Member const& m : plan.members)
        if (m.bot && m.p->IsAlive() && !m.tank && !m.healer && !m.ranged && !holds(m.p))
            if (auto const it = g_horseSeat.find(m.p->GetGUID().GetCounter());
                it != g_horseSeat.end() && it->second.instance == instance && it->second.horse == kHorseKorthazz && it->second.melee)
                ++korthMelee;
    // Healers first (their share per horseman), then ranged, then melee and spare tanks.
    std::vector<Member const*> order;
    for (int pass = 0; pass < 3; ++pass)
        for (Member const& m : plan.members)
        {
            if (!m.bot)
                continue;
            if (holds(m.p))
            {
                g_horseSeat.erase(m.p->GetGUID().GetCounter());   // a tank that took a horseman over leaves its seat
                continue;
            }
            int const kind = m.healer ? 0 : (m.ranged ? 1 : 2);
            if (kind == pass)
                order.push_back(&m);
        }
    for (Member const* mp : order)
    {
        Member const& m = *mp;
        Player* const bot = m.p;
        uint64_t const low = bot->GetGUID().GetCounter();
        if (m.tank)
            continue;   // a spare tank: its own pass below
        HorseSeat& seat = g_horseSeat[low];
        seat.spare = seat.next = false;
        bool const melee = !m.healer && !m.ranged;
        bool const have = seat.instance == instance && seat.horse >= 0 && horses[seat.horse] && seat.melee == melee &&
                          !(melee ? false : nearVoid(seat.x, seat.y));
        uint32_t const keepCost = have ? markCost(bot, seat.x, seat.y) : UINT32_MAX;
        // A seat whose next mark takes a third of the raider's health is left as soon as it may be (naxx_horsemen r28: ten raiders died to
        // marks of 2400-2700 at 58 s, four rogues among them on their melee seat by Korth'azz, inside Mograine's mark too).
        uint32_t const keepLimit = std::min<uint32_t>(3000, bot->GetHealth() / 3);
        // A ranged or healer seat on Zeliek's way to his corner is left (af48-1: the seats by Korth'azz were kept as he came through).
        bool const laneHit = have && !melee && inLane(seat.x, seat.y);
        bool const meteorHold = have && melee && seat.horse == kHorseKorthazz && keepCost < keepLimit && korthMelee <= kKorthMeteorShare;
        if (have && (meteorHold || !window) && !laneHit && keepCost < keepLimit)
        {
            seat.cost = keepCost;
            if (!melee)
                wrathSeats.push_back({ seat.x, seat.y });
            if (!melee)
                rangedSeats.push_back({ seat.x, seat.y });
            if (m.healer)
                ++healersAt[seat.horse];
            continue;
        }
        // Korth'azz's side short of its Meteor share: a melee raider whose seat there stays under its mark limit goes in. His corner lies
        // inside Mograine's mark (59 y, 70 y reach), so his melee carry two marks and must leave at the third (2000 against a limit of
        // about 1300); the share is kept by others coming in, not by them staying. naxx_horsemen af53-1 (r1 0002): at the second mark all
        // four rogues left together and nobody took their place, the next Meteor fell whole on his tank (13442) and he died.
        bool const korthShort = melee && horses[kHorseKorthazz] && korthMelee < kKorthMeteorShare;
        // The cheapest seat; a healer goes to a horseman still short of healers first, a DPS prefers the horseman closest to death.
        int best = -1;
        float bx = 0, by = 0, bz = 0;
        uint64_t bestScore = UINT64_MAX;
        uint32_t bestCost = 0;
        auto consider = [&](int h, float x, float y, float z)
        {
            if (melee ? inWrath(x, y) : !wrathClear(x, y))
                return;
            uint32_t const cost = markCost(bot, x, y);
            uint64_t score = uint64_t(cost) * 1000;
            if (m.healer && healersAt[h] >= healersEach)
                score += 500000;   // this horseman has his share
            if (!m.healer)
                score += uint64_t(horses[h]->GetHealthPct() * 100.0f);   // the weakest first
            if (korthShort && h == kHorseKorthazz && cost < keepLimit)
                score = 0;                                               // his Meteor share first
            if (!melee)
                for (auto const& [sx, sy] : rangedSeats)
                    if (Dist2D(x, y, sx, sy) < 4.0f)
                        score += 20000;                                    // taken
            score += uint64_t(bot->GetExactDist2d(x, y));                // then the nearest
            if (score < bestScore)
            {
                bestScore = score;
                best = h; bx = x; by = y; bz = z; bestCost = cost;
            }
        };
        if (melee)
        {
            for (int h = 0; h < 4; ++h)
                if (horses[h])
                    consider(h, kHorse[h].cx, kHorse[h].cy, kHorse[h].cz);
        }
        else
            for (Spot const& s : ring)
                consider(s.horse, s.x, s.y, s.z);
        if (best < 0)
            continue;
        // A move must pay: the seat it has stays unless the new one saves a real share of the next mark.
        bool const healerHeld = m.healer && have && best != seat.horse && healersLeft[seat.horse] >= healerLeaveCap;
        bool const meteorFill = korthShort && best == kHorseKorthazz && !(have && seat.horse == kHorseKorthazz);
        if (have && !laneHit && keepCost < keepLimit && !meteorFill &&
            (keepCost <= bestCost + (m.healer ? kHorseHealerSwitchCost : kHorseSwitchCost) || healerHeld) &&
            !(m.healer && healersAt[seat.horse] > healersEach))
        {
            seat.cost = keepCost;
            if (!melee)
                wrathSeats.push_back({ seat.x, seat.y });
            if (!melee)
                rangedSeats.push_back({ seat.x, seat.y });
            if (m.healer)
                ++healersAt[seat.horse];
            continue;
        }
        // Said when the seat changes: a spare tank by a horseman who has a tank re-takes the same seat each pass (naxx_horsemen r29: 171
        // lines for Mctankwd in 40 s, three seats).
        bool const sameSeat = seat.instance == instance && seat.horse == best && seat.melee == melee && Dist2D(seat.x, seat.y, bx, by) <= 3.0f;
        if (!sameSeat)
        {
            std::ostringstream stacks;
            for (int h = 0; h < 4; ++h)
                stacks << (h ? " " : "") << kHorse[h].name << " " << stacksAtNext(bot, h);
            TC_LOG_INFO("playerbot.v2.raid", "[raid_horsemen] {} takes a {} seat by {} at {:.1f},{:.1f}: next mark costs {} (was {}), stacks at the next mark: {}, mark in {} ms",
                        bot->GetName(), melee ? "melee" : (m.healer ? "healer" : "ranged"), kHorse[best].name, bx, by, bestCost,
                        have ? int64_t(keepCost) : int64_t(-1), stacks.str(), toNext);
        }
        if (melee && have && seat.horse == kHorseKorthazz && best != kHorseKorthazz)
            --korthMelee;
        else if (melee && best == kHorseKorthazz && !(have && seat.horse == kHorseKorthazz))
            ++korthMelee;
        if (m.healer && have && seat.horse != best)
            ++healersLeft[seat.horse];
        seat = { instance, best, melee, bx, by, bz, bestCost };
        if (!melee)
            wrathSeats.push_back({ bx, by });
        if (!melee)
            rangedSeats.push_back({ bx, by });
        if (m.healer)
            ++healersAt[best];
    }

    // Spare tanks: the tank rotation. Each held horseman gets the spare tank that will take him at his tank's third mark: one that will
    // carry no more of his marks when the next lands than the swap lets a taker carry (takerStacks 1), the fewest first, then the nearest
    // to his corner. It waits out of his mark, as close to his corner as the room allows, and walks in once that third mark has landed
    // (HorsemenMove); the generic swap (RaidRules::tankSwap) then has it taunt. A spare with no horseman to take next waits where its next
    // mark costs least, so its stacks fall off. A horseman nobody holds (the pull, his tank died) is first: his spare walks in at once.
    // naxx_horsemen r29: every spare stood beside a horseman and carried his marks, so none was ever clean for him; at the third mark at
    // 44 s the only clean tanks were the other horsemen's tanks, all four were sent over, no taunt was cast in 219 s, and the marks killed
    // the tanks at 4-5 stacks (4500-4800).
    {
        std::vector<Member const*> spares;
        for (Member const* mp : order)
            if (mp->tank)
                spares.push_back(mp);
        auto isTankP = [&](Unit const* u)
        {
            for (Member const& t : plan.members)
                if (t.p == u)
                    return t.tank;
            return false;
        };
        // The horsemen by how soon they need their next tank: one held by no tank first, then the most marks on his tank.
        std::vector<std::pair<int, int>> urgent;   // (marks on his tank, 99 when no tank holds him), horseman
        for (int h = 0; h < 4; ++h)
        {
            if (!horses[h])
                continue;
            Unit* const holder = HorseHolderOf(horses[h]);
            int heldBy = 0;
            if (holder)
                for (Unit* other : horses)
                    if (other && HorseHolderOf(other) == holder)
                        ++heldBy;
            bool const tankHolds = holder && isTankP(holder) && heldBy == 1;
            urgent.push_back({ tankHolds ? int(Stacks(holder, kHorse[h].mark)) : 99, h });
        }
        std::stable_sort(urgent.begin(), urgent.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
        // A spare may take a horseman while it carries no more of his marks at the next one than the swap lets a taker carry (takerStacks):
        // it arrives before that mark lands. Every raider stands in all four marks at the pull, so after the first mark at 20 s each one
        // carries a stack of every horseman's for 75 s. naxx_horsemen af39-1: with "none of his marks" as the test, no spare was fit for
        // any horseman from 20 s on; the next tank walking to Zeliek turned back and dropped him ("the raid order forbids it"), he ran
        // loose to the main tank, which held Blaumeux too, and no taunt swap was cast in the whole fight.
        // A next tank keeps its horseman while it stays fit for him: picked afresh each pass by the distance to his corner, the
        // spares swapped horsemen as they walked, each dropped the one it had just taken ("the raid order forbids it") and took the
        // next. naxx_horsemen af38-2: the four ran loose on a healer for 7 s, the main tank ended up holding three of them, Korth'azz's
        // melee followed it onto Zeliek, and one Holy Wrath at 12 s killed ten.
        std::vector<int> nextFor(spares.size(), -1);
        for (auto const& [marks, h] : urgent)
            for (size_t i = 0; i < spares.size(); ++i)
            {
                if (nextFor[i] >= 0 || (marks != 99 && stacksAtNext(spares[i]->p, h) > HorseTakerStacks(plan, kHorse[h].entry)))
                    continue;
                auto const it = g_horseSeat.find(spares[i]->p->GetGUID().GetCounter());
                if (it == g_horseSeat.end() || it->second.instance != instance || !it->second.spare || !it->second.next ||
                    it->second.horse != h)
                    continue;
                nextFor[i] = h;
                break;
            }
        for (auto const& [marks, h] : urgent)
        {
            if (std::find(nextFor.begin(), nextFor.end(), h) != nextFor.end())
                continue;
            // the fewest of his marks first, then the nearest to his corner
            int pick = -1;
            uint32_t pickStacks = 0;
            float pickDist = 0.0f;
            uint32_t const taker = HorseTakerStacks(plan, kHorse[h].entry);
            // A horseman no tank holds (on a raider, or on a tank holding another) is taken by the least marked spare even over the
            // taker's limit: no taunt swap is needed to pick him up, and the tanks holding the others leave him alone (HorsemenTargets).
            for (bool anyStacks : { false, true })
            {
                if (anyStacks && (pick >= 0 || marks != 99))
                    break;
                for (size_t i = 0; i < spares.size(); ++i)
                {
                    uint32_t const st = stacksAtNext(spares[i]->p, h);
                    if (nextFor[i] >= 0 || (!anyStacks && st > taker))
                        continue;
                    float const d = spares[i]->p->GetExactDist2d(kHorse[h].cx, kHorse[h].cy);
                    if (pick < 0 || st < pickStacks || (st == pickStacks && d < pickDist)) { pick = int(i); pickStacks = st; pickDist = d; }
                }
            }
            if (pick >= 0)
                nextFor[pick] = h;
            else
                TC_LOG_INFO("playerbot.v2.raid", "[raid_horsemen] no spare tank carries few enough of {}'s marks for his next tank ({} marks on his tank)",
                            kHorse[h].name, marks == 99 ? -1 : marks);
        }
        std::vector<std::pair<float, float>> spareTaken;
        for (size_t i = 0; i < spares.size(); ++i)
        {
            Player* const bot = spares[i]->p;
            int const n = nextFor[i];
            Spot const* best = nullptr;
            uint64_t bestScore = UINT64_MAX;
            uint32_t bestCost = 0;
            for (int outOfMark : { 1, 0 })   // the next tank's spot out of his mark; none left: the cheapest one
            {
                for (Spot const& sp : ring)
                {
                    if (n >= 0 && outOfMark && Dist2D(sp.x, sp.y, kHorse[n].cx, kHorse[n].cy) <= kHorse[n].radius + kHorseMarkMargin)
                        continue;
                    if (inWrath(sp.x, sp.y) && !wrathClear(sp.x, sp.y))
                        continue;
                    bool taken = false;
                    for (auto const& [tx, ty] : spareTaken)
                        if (Dist2D(sp.x, sp.y, tx, ty) < 4.0f)
                            taken = true;
                    if (taken)
                        continue;
                    uint32_t const cost = markCost(bot, sp.x, sp.y);
                    // The next tank: the shortest walk in to him. A resting one: the shortest walk from where it stands.
                    float const walk = n >= 0 ? Dist2D(sp.x, sp.y, kHorse[n].cx, kHorse[n].cy) : bot->GetExactDist2d(sp.x, sp.y);
                    uint64_t const score = uint64_t(cost) * 1000 + uint64_t(walk);
                    if (score < bestScore) { bestScore = score; best = &sp; bestCost = cost; }
                }
                if (best || n < 0)
                    break;
            }
            uint64_t const low = bot->GetGUID().GetCounter();
            if (!best)
            {
                g_horseSeat.erase(low);
                continue;
            }
            HorseSeat& seat = g_horseSeat[low];
            int const horse = n >= 0 ? n : best->horse;
            bool const outside = n < 0 || Dist2D(best->x, best->y, kHorse[n].cx, kHorse[n].cy) > kHorse[n].radius + kHorseMarkMargin;
            bool const same = seat.instance == instance && seat.spare && seat.horse == horse && seat.next == (n >= 0) &&
                              Dist2D(seat.x, seat.y, best->x, best->y) <= 3.0f;
            if (!same)
            {
                std::ostringstream stacks;
                for (int h = 0; h < 4; ++h)
                    stacks << (h ? " " : "") << kHorse[h].name << " " << stacksAtNext(bot, h);
                if (n >= 0)
                    TC_LOG_INFO("playerbot.v2.raid", "[raid_horsemen] {} is the next tank for {}: waits at {:.1f},{:.1f}, {:.0f} y from his corner{}, next mark costs {}, stacks at the next mark: {}, mark in {} ms",
                                bot->GetName(), kHorse[n].name, best->x, best->y, Dist2D(best->x, best->y, kHorse[n].cx, kHorse[n].cy),
                                outside ? "" : " (inside his mark: no spot out of it)", bestCost, stacks.str(), toNext);
                else
                    TC_LOG_INFO("playerbot.v2.raid", "[raid_horsemen] {} rests as a spare tank at {:.1f},{:.1f}: next mark costs {}, stacks at the next mark: {}, mark in {} ms",
                                bot->GetName(), best->x, best->y, bestCost, stacks.str(), toNext);
            }
            seat = { instance, horse, true, best->x, best->y, best->z, bestCost };
            seat.spare = true;
            seat.next = n >= 0;
            spareTaken.push_back({ best->x, best->y });
            wrathSeats.push_back({ best->x, best->y });
        }
    }

    // Where the melee wait until their horseman stands in his corner: each a Holy Wrath leap from every other waiting melee raider, every
    // ranged and healer seat and Zeliek's tank, and out of Zeliek's reach from where he stands now where the room allows. A spot beside the
    // corner toward the room's middle comes first, then the ring seats round the corner, nearest to that spot first. naxx_horsemen r27:
    // Zeliek was fought by Korth'azz's corner, 24 y from the one point where Korth'azz's melee waited; five of them stood on it and one
    // Holy Wrath at 123 s killed three, the next at 137 s four more.
    float const mx = (kHorse[0].cx + kHorse[1].cx + kHorse[2].cx + kHorse[3].cx) / 4.0f;
    float const my = (kHorse[0].cy + kHorse[1].cy + kHorse[2].cy + kHorse[3].cy) / 4.0f;
    std::vector<std::pair<float, float>> waitTaken;
    for (Member const* mp : order)
    {
        Member const& m = *mp;
        auto it = g_horseSeat.find(m.p->GetGUID().GetCounter());
        if (it == g_horseSeat.end() || it->second.instance != instance || !it->second.melee || it->second.horse < 0 || m.tank)
            continue;
        HorseSeat& seat = it->second;
        seat.wait = false;
        HorseDef const& d = kHorse[seat.horse];
        if (!horses[seat.horse] || Dist2D(horses[seat.horse]->GetPositionX(), horses[seat.horse]->GetPositionY(), d.cx, d.cy) <= kHorseInCorner)
            continue;
        float const a = std::atan2(my - d.cy, mx - d.cx);
        float const ox = d.cx + kHorseMeleeWait * std::cos(a), oy = d.cy + kHorseMeleeWait * std::sin(a);
        std::vector<Spot> candidates{ { seat.horse, ox, oy, d.cz } };
        for (Spot const& sp : ring)
            if (sp.horse == seat.horse)
                candidates.push_back(sp);
        auto clear = [&](float x, float y)
        {
            for (auto const& [sx, sy] : wrathSeats)
                if (Dist2D(x, y, sx, sy) < kHolyWrathLeap)
                    return false;
            for (auto const& [sx, sy] : waitTaken)
                if (Dist2D(x, y, sx, sy) < kHolyWrathLeap)
                    return false;
            return true;
        };
        Spot const* best = nullptr;
        float bestScore = 0.0f;
        for (Spot const& c : candidates)
        {
            if (!clear(c.x, c.y) || nearVoid(c.x, c.y))
                continue;
            float score = Dist2D(c.x, c.y, ox, oy);
            if (zeliek && Dist2D(c.x, c.y, zeliek->GetPositionX(), zeliek->GetPositionY()) <= kHolyWrathReach)
                score += 1000.0f;   // in his reach: only when no spot out of it is left
            if (inLane(c.x, c.y))
                score += 2000.0f;   // on his way to his corner: last of all
            if (!best || score < bestScore)
            {
                best = &c;
                bestScore = score;
            }
        }
        if (!best)
            continue;   // no spot a leap from the others: the point beside the corner (HorsemenMove)
        seat.wait = true;
        seat.wx = best->x; seat.wy = best->y; seat.wz = best->z;
        waitTaken.push_back({ best->x, best->y });
    }
}

// The horseman a raider's seat is by, or nullptr outside the fight.
Unit* HorseOfSeat(Plan const& plan, Player* bot, HorseSeat const*& seat)
{
    auto it = g_horseSeat.find(bot->GetGUID().GetCounter());
    if (it == g_horseSeat.end() || it->second.instance != bot->GetInstanceId() || it->second.horse < 0)
        return nullptr;
    seat = &it->second;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kHorse[it->second.horse].entry)
            return enemy;
    return nullptr;
}

bool AnyHorseman(Plan const& plan)
{
    for (Unit* enemy : plan.enemies)
        for (HorseDef const& d : kHorse)
            if (enemy->GetEntry() == d.entry)
                return true;
    return false;
}

// A ranged raider or healer walks to its seat and holds it; a spare tank waits at its seat and the next tank of a horseman walks in to take
// him over. A melee DPS is sent by its target (HorsemenTargets). A raider in a Void Zone is left to the hazard step.
bool HorsemenMove(Plan const& plan, Member const& self, uint32_t now, RaidOrder& out)
{
    if (!self.bot || !AnyHorseman(plan))
        return false;
    HorsemenSeats(plan, now);
    for (Unit* enemy : plan.enemies)
        if (enemy->GetVictim() == self.p)
            return false;   // a tank holding a horseman: his corner (RaidRules::stand)
    HorseSeat const* seat = nullptr;
    Unit* horse = HorseOfSeat(plan, self.p, seat);
    if (!horse)
        return false;
    Player* bot = self.p;
    std::list<Creature*> voids;
    bot->GetCreatureListWithEntryInGrid(voids, kHorseVoidZone, kHorseVoidClear + 2.0f);
    for (Creature* v : voids)
        if (v->IsAlive() && bot->GetExactDist2d(v) < kHorseVoidClear)
            return false;
    if (seat->melee)
    {
        // Until its horseman stands in his corner, a melee DPS waits beside the corner: following him while his tank drags him there kept
        // it in the heap round Zeliek. In the corner, it goes in by its target (HorsemenTargets). Never a tank: the tank by a horseman is the
        // one that takes him off the raider he opened on and drags him to the corner. naxx_horsemen r23: with the tanks waiting at the
        // corners too, all four horsemen stayed on the main tank at Mograine's corner, five tanks stood idle 40 y off at Korth'azz's, the
        // main tank died at 6 s, and Zeliek's Holy Wrath ran through the pile (in r22, before the wait, all four were on their own tank by 9 s).
        HorseDef const& d = kHorse[seat->horse];
        // Korth'azz's melee go with him from the pull: his Meteor (28884, 12825 split among everybody within 8 y of the player it lands on)
        // kills whoever takes it alone. naxx_horsemen r29: his tank dragged him toward his corner while his melee waited by it, the first
        // Meteor at 21:33:39 split two ways (6001 on the tank, 6264 on a hunter) and killed both. They stand behind his tank, 3 y off it.
        // Only behind a tank holding him alone: af38-2's main tank held Zeliek as well, and his Holy Wrath went through the melee. Else
        // they wait by his corner, as the other horsemen's melee do.
        Unit* const korthHolder = seat->horse == kHorseKorthazz ? HorseHolderOf(horse) : nullptr;
        bool korthTank = false;
        for (Member const& t : plan.members)
            if (korthHolder && t.tank && t.p == korthHolder)
                korthTank = true;
        for (Unit* other : plan.enemies)
            if (korthTank && other != horse && other->GetVictim() == korthHolder)
                for (HorseDef const& od : kHorse)
                    if (other->GetEntry() == od.entry)
                        korthTank = false;
        if (!self.tank && korthTank && Dist2D(horse->GetPositionX(), horse->GetPositionY(), d.cx, d.cy) > kHorseInCorner)
        {
            Unit* const holder = korthHolder;
            if (holder == bot)
                return false;
            out.hold_melee = true;
            float const a = horse->GetAbsoluteAngle(holder);
            float const sx = holder->GetPositionX() + 3.0f * std::cos(a), sy = holder->GetPositionY() + 3.0f * std::sin(a);
            if (bot->GetExactDist2d(sx, sy) <= 2.0f)
                return true;
            return HorseWalk(bot, sx, sy, holder->GetPositionZ(), "horsemen: melee with Korth'azz's tank", out);
        }
        if (!self.tank && Dist2D(horse->GetPositionX(), horse->GetPositionY(), d.cx, d.cy) > kHorseInCorner)
        {
            out.hold_melee = true;
            float const mx = (kHorse[0].cx + kHorse[1].cx + kHorse[2].cx + kHorse[3].cx) / 4.0f;
            float const my = (kHorse[0].cy + kHorse[1].cy + kHorse[2].cy + kHorse[3].cy) / 4.0f;
            float const a = std::atan2(my - d.cy, mx - d.cx);
            // its own waiting spot, a Holy Wrath leap from the others (HorsemenSeats), else the point beside the corner
            float const wx = seat->wait ? seat->wx : d.cx + kHorseMeleeWait * std::cos(a);
            float const wy = seat->wait ? seat->wy : d.cy + kHorseMeleeWait * std::sin(a);
            if (bot->GetExactDist2d(wx, wy) <= 3.0f)
                return true;
            return HorseWalk(bot, wx, wy, seat->wait ? seat->wz : d.cz, "horsemen: melee waits by the corner", out);
        }
        if (!self.tank || !seat->spare)
            return false;
        // A spare tank. The next one for its horseman walks in when his tank carries the swap's marks, or no tank holds him: from a seat
        // out of his mark it sets off once the mark that brings his tank to the swap has landed, so it reaches him with no more of his
        // marks than it waited with, inside the 12 s before the following one. The swap can only taunt after that mark anyway (his tank
        // carries the swap's marks from then on), and a taker that walked in as it landed carried one more: with a stack from the first
        // mark it was over the swap's takerStacks and never taunted. From a seat inside his mark (no spot out of it) it still sets off so
        // that it arrives as the mark lands. In reach, the swap's taunt is elected (ElectTankSwaps). Otherwise it waits at its seat.
        if (seat->next)
        {
            Unit* const holder = HorseHolderOf(horse);
            bool tankHolds = false;
            for (Member const& t : plan.members)
                if (t.tank && holder && t.p == holder)
                    tankHolds = true;
            // A tank holding another horseman as well does not hold him: it keeps one and drops the rest (HorsemenTargets), and his next tank
            // takes him now, as HorsemenSeats picks it (no tank holds him). naxx_horsemen af48-1: all four opened on Mctankwc, which kept
            // Mograine and dropped Zeliek each second ("the raid order forbids it"); Zeliek's next tank waited at its seat 73 y off for a
            // swap at his third mark, and Zeliek stayed loose in the raid until the Holy Wrath at 12 s.
            for (Unit* other : plan.enemies)
                if (tankHolds && other != horse && other->GetVictim() == holder)
                    for (HorseDef const& od : kHorse)
                        if (other->GetEntry() == od.entry)
                            tankHolds = false;
            uint32_t const need = HorseSwapStacks(plan, horse->GetEntry());
            uint32_t const marks = tankHolds ? Stacks(holder, d.mark) : 0;
            auto const it = g_horseToNext.find(plan.groupLow);
            int32_t const toNext = it != g_horseToNext.end() ? it->second : kMarkEveryMs;
            float const speed = std::max(bot->GetSpeed(MOVE_RUN), 1.0f);
            int32_t const walkMs = int32_t(std::max(0.0f, bot->GetExactDist2d(horse) - 5.0f) / speed * 1000.0f) + kHorseWalkInLeadMs;
            bool const seatInMark = Dist2D(seat->x, seat->y, d.cx, d.cy) <= d.radius + kHorseMarkMargin;
            if (!tankHolds || marks >= need || (seatInMark && marks + 1 >= need && toNext <= walkMs))
            {
                if (bot->GetExactDist2d(horse) <= 5.0f)
                    return false;
                uint32_t& logged = g_horseWalkInLog[bot->GetGUID().GetCounter()];
                if (!logged || now - logged >= 5000)
                {
                    logged = now;
                    TC_LOG_INFO("playerbot.v2.raid", "[raid_horsemen] {} walks in to take {} off {} ({} of his marks, the swap at {}), next mark in {} ms, walk {} ms",
                                bot->GetName(), d.name, holder ? holder->GetName() : std::string("nobody"), marks, need, toNext, walkMs);
                }
                float const a = horse->GetAbsoluteAngle(bot);
                float const x = horse->GetPositionX() + 3.0f * std::cos(a), y = horse->GetPositionY() + 3.0f * std::sin(a);
                return HorseWalk(bot, x, y, horse->GetPositionZ(), "horsemen: next tank walks in", out);
            }
        }
        if (bot->GetExactDist2d(seat->x, seat->y) <= 3.0f)
        {
            out.at_post = true;
            return true;
        }
        return HorseWalk(bot, seat->x, seat->y, seat->z, seat->next ? "horsemen: next tank waits out of his mark" : "horsemen: spare tank rests", out);
    }
    out.post_enemy = horse->GetGUID();
    if (bot->GetExactDist2d(seat->x, seat->y) <= 3.0f)
    {
        out.at_post = true;
        return true;
    }
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    if (HorseVoidDetour(bot, seat->x, seat->y, dx, dy))
        return Usable(bot, dx, dy, bot->GetPositionZ(), dz, /*urgent*/ true) && SetMove(out, dx, dy, dz, "horsemen: round a Void Zone");
    return SetMove(out, seat->x, seat->y, seat->z, "horsemen: seat");
}

// A seated raider attacks only the horseman of its seat (a ranged one: any horseman in reach of the seat); a spare tank attacks none.
void HorsemenTargets(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders)
{
    if (!AnyHorseman(plan))
        return;
    auto isHorseman = [](Unit const* u)
    {
        for (HorseDef const& d : kHorse)
            if (u->GetEntry() == d.entry)
                return true;
        return false;
    };
    for (Member const& m : plan.members)
    {
        if (!m.bot || m.healer)
            continue;
        auto it = orders.find(m.p->GetGUID().GetCounter());
        if (it == orders.end())
            continue;
        RaidOrder& o = it->second;
        // A tank holding a horseman keeps that one and touches no other: each horseman is taken by his own next tank. Before, a holder had
        // no order here at all, so it took every horseman that came at it (58 "boss_on_it" pickups in af41-1) and dragged him from his
        // corner, and the horseman it had held was left to whoever came next.
        if (m.tank)
        {
            std::vector<Unit*> heldHere;
            for (Unit* enemy : plan.enemies)
                if (isHorseman(enemy) && enemy->GetVictim() == m.p)
                    heldHere.push_back(enemy);
            if (!heldHere.empty())
            {
                Unit* keep = heldHere.front();
                for (Unit* h : heldHere)
                    if (m.p->GetVictim() == h)
                        keep = h;
                for (Unit* enemy : plan.enemies)
                    if (isHorseman(enemy) && enemy != keep)
                        o.forbidden_targets.push_back(enemy->GetGUID());
                continue;
            }
        }
        HorseSeat const* seat = nullptr;
        Unit* own = HorseOfSeat(plan, m.p, seat);
        if (!own)
            continue;
        bool holding = false;
        for (Unit* enemy : plan.enemies)
            if (enemy->GetVictim() == m.p)
                holding = true;
        if (holding)
            continue;   // a tank on its horseman
        for (Unit* enemy : plan.enemies)
        {
            bool isHorse = false;
            for (HorseDef const& d : kHorse)
                if (enemy->GetEntry() == d.entry)
                    isHorse = true;
            if (!isHorse || enemy == own)
                continue;
            if (seat->melee || Dist2D(seat->x, seat->y, enemy->GetPositionX(), enemy->GetPositionY()) > kHorseReach)
                o.forbidden_targets.push_back(enemy->GetGUID());
        }
        if (m.tank)
        {
            // A spare tank builds no threat on him before its taunt, while another tank holds him. One nobody holds (the pull, or his
            // tank died) is its to pick up. naxx_horsemen r25 (combined 1118): at the pull no tank held anyone, so every tank was a
            // spare forbidden its own horseman; the four ran loose on a healer for 18 s, the main tank took and dropped them in turn
            // ("the raid order forbids it") and the raid fell from 25 s on.
            // A tank that holds two or more of them has one too many: the seat tanks of each take theirs. naxx_horsemen r26: the main
            // tank picked all four up at the pull, the spare tanks stood together at (2566,-2988) forbidden theirs, and one Holy Wrath
            // jumped through five of them at 124 s.
            Unit const* holder = HorseHolderOf(own);
            Player const* holderP = holder ? holder->ToPlayer() : nullptr;
            bool tankHolds = false;
            for (Member const& t : plan.members)
                if (t.tank && holderP && t.p == holderP)
                    tankHolds = true;
            if (tankHolds)
            {
                int heldByHim = 0;
                for (Unit* enemy : plan.enemies)
                    for (HorseDef const& d : kHorse)
                        if (enemy->GetEntry() == d.entry && enemy->GetVictim() == holder)
                            ++heldByHim;
                if (heldByHim > 1)
                    tankHolds = false;
            }
            // A resting spare attacks none: it is no horseman's next tank, and every one of them has his own. naxx_horsemen af41-1: the
            // spares' rest seats lay by Mograine; while his tank held a second horseman he counted as held by nobody, five spare tanks
            // went for him at once ("raid:switch_target") and dropped him a second later ("raid:drop_target"), over and over, and his
            // marks went round all of them: by the third mark no spare carried few enough to take anyone, one swap in 287 s.
            if (tankHolds || !seat->next)
                o.forbidden_targets.push_back(own->GetGUID());
        }
        else
            o.fallback_target = own->GetGUID();
    }
}

// ---------------------------------------------------------------------------
// Loatheb: the spores go to one melee group at a time
// ---------------------------------------------------------------------------
// He summons a Spore (16286) every 13 s at one of two spots chosen at the pull (classic_boss_loatheb.cpp: (2951,-4016) or (2870,-3978)),
// which drifts at a random raider. When it dies it casts Fungal Bloom (29232) on the five nearest within 10 y: 90 s of -100% threat,
// +50% melee crit and +60% spell crit. The raid sends one melee group to each spore in turn, so every group gets the bloom about once a
// rotation and the five charges land on melee, not on the healers and casters the spore floated to. Before 0158 the encounter data gave
// spores priority 4 for everyone: whoever stood nearest killed it, mostly the ranged (it drifts at a random raider), and the melee hit
// Loatheb with no bloom, so they took most of the threat as the fight went on.
// Each new spore is given to the next group (party subgroup) that has a melee bot alive, in group order round the raid; those melee attack
// it, every other bot leaves it alone (a real player still hits whatever it likes). A spore keeps its group while that group has a melee
// bot alive; when the last one dies the spore goes to the next group.
constexpr uint32_t kLoatheb      = 16011;
constexpr uint32_t kLoathebSpore = 16286;
constexpr uint32_t kFungalBloom  = 29232;
struct SporeRotation
{
    int8_t lastGroup = -1;                            // the group the last new spore went to
    std::unordered_map<uint64_t, uint8_t> sporeGroup; // spore guid -> its group
};
std::mutex g_sporeMu;                                 // plans for different instances run on different map threads
std::unordered_map<uint64_t, SporeRotation> g_sporeRot;   // Loatheb's guid -> his spores' groups

bool LoathebMelee(Member const& m) { return m.bot && !m.tank && !m.healer && !m.ranged; }

void LoathebSporeTargets(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders)
{
    Unit* loatheb = nullptr;
    std::vector<Unit*> spores;
    for (Unit* enemy : plan.enemies)
    {
        if (enemy->GetEntry() == kLoatheb)
            loatheb = enemy;
        else if (enemy->GetEntry() == kLoathebSpore)
            spores.push_back(enemy);   // sorted by guid: in the order they were summoned
    }
    std::lock_guard<std::mutex> lk(g_sporeMu);
    if (!loatheb)
    {
        if (g_sporeRot.size() > 16)   // a fight that ended (a wipe, a kill): nothing reads it again
            g_sporeRot.clear();
        return;
    }
    // The melee bots by group, and how many of each group carry a bloom (only for the log).
    std::map<uint8_t, std::vector<Member const*>> groups;
    for (Member const& m : plan.members)
        if (LoathebMelee(m))
            groups[m.p->GetSubGroup()].push_back(&m);
    if (groups.empty())
        return;   // no melee bot: the generic priority (everyone on the spores) stays
    SporeRotation& rot = g_sporeRot[loatheb->GetGUID().GetCounter()];
    // Spores gone from the fight (killed, or despawned) leave the map.
    for (auto it = rot.sporeGroup.begin(); it != rot.sporeGroup.end();)
    {
        bool live = false;
        for (Unit* s : spores)
            if (s->GetGUID().GetCounter() == it->first)
                live = true;
        it = live ? std::next(it) : rot.sporeGroup.erase(it);
    }
    auto nextGroup = [&](int last) -> uint8_t
    {
        auto g = groups.upper_bound(static_cast<uint8_t>(last < 0 ? 0xFF : last));
        if (last < 0 || g == groups.end())
            g = groups.begin();
        return g->first;
    };
    for (Unit* spore : spores)
    {
        uint64_t const key = spore->GetGUID().GetCounter();
        auto it = rot.sporeGroup.find(key);
        if (it != rot.sporeGroup.end() && groups.count(it->second))
            continue;
        bool const reassigned = it != rot.sporeGroup.end();
        uint8_t const g = nextGroup(rot.lastGroup);
        rot.sporeGroup[key] = g;
        rot.lastGroup = static_cast<int8_t>(g);
        uint32_t bloomed = 0;
        for (Member const* m : groups[g])
            if (m->p->HasAura(kFungalBloom))
                ++bloomed;
        TC_LOG_INFO("playerbot.v2.raid", "[raid_loatheb] spore {} at {:.1f},{:.1f} {} group {} ({} melee, {} with Fungal Bloom)",
            key, spore->GetPositionX(), spore->GetPositionY(), reassigned ? "goes over to" : "goes to", g + 1, groups[g].size(), bloomed);
    }
    for (Member const& m : plan.members)
    {
        if (!m.bot)
            continue;
        auto oit = orders.find(m.p->GetGUID().GetCounter());
        if (oit == orders.end())
            continue;
        RaidOrder& o = oit->second;
        Unit* mine = nullptr;
        for (Unit* spore : spores)
        {
            bool const ours = LoathebMelee(m) && rot.sporeGroup[spore->GetGUID().GetCounter()] == m.p->GetSubGroup();
            if (!ours)
                o.forbidden_targets.push_back(spore->GetGUID());
            else if (!mine)
                mine = spore;   // the oldest of the group's spores first
        }
        if (mine && std::find(o.forbidden_targets.begin(), o.forbidden_targets.end(), mine->GetGUID()) == o.forbidden_targets.end())
        {
            o.attack_target = mine->GetGUID();
            o.fallback_target = loatheb->GetGUID();
        }
        else if (!spores.empty() && !m.tank && !m.healer)
            o.fallback_target = loatheb->GetGUID();   // a bot on a spore that is not its group's goes back to him
    }
}

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
    ThaddiusTankTargets(plan, orders);
    HorsemenTargets(plan, orders);
    NaxxGluthTargets(plan, orders);
    LoathebSporeTargets(plan, orders);
}

// A loose chow (spawned onto a random raider, on nobody's tank) is sent to the nearest chow tank, which goes for it: the pick-up rules
// alone did not take it, the chow ran at its raider by the camp, past him, and he ate it (naxx_gluth 1116 rerun: one chow at a time
// loose on Mcshadb / Mchuntc / Mchealpc for most of 345 s, the chow tanks on raid:move / drop_target / switch_target, Gluth at 97-99%
// with the raid's 11k a second on him). Each chow tank takes the nearest loose chow; the rest go to the next nearest tank.
std::unordered_set<uint64_t> g_gluthChowLogged;
std::unordered_map<uint64_t, uint32_t> g_gluthWallLogged;   // chow tank -> when its wall line was last logged
std::unordered_map<uint64_t, uint32_t> g_gluthShedLogged;   // chow tank -> when its load-evening line was last logged
// The chows seen on the last plan near him, to name the ones he ate: one gone from the fight that was last seen inside his 15 y with health
// left (naxx_gluth 1116 runs 4-9: 38-57 chows "killed" a run, him back at 99-100%, and no line saying which were eaten, where, or on whom).
struct GluthChowSeen { float x, y, dist; uint32_t pct; std::string victim; };
std::unordered_map<uint64_t, std::unordered_map<uint64_t, GluthChowSeen>> g_gluthChowSeen;   // Gluth's guid -> chow guid -> last look
constexpr float kGluthEatSeen = kGluthEatReach + 3.0f;   // his eat reach (body gap) and one plan's walk
struct GluthHealth { uint32_t pct = 0; bool reachLogged = false; };
std::unordered_map<uint64_t, GluthHealth> g_gluthHealth;

void NoteGluthChowsEaten(Plan const& plan, Unit* gluth)
{
    auto& seen = g_gluthChowSeen[gluth->GetGUID().GetCounter()];
    std::unordered_map<uint64_t, GluthChowSeen> now;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kZombieChow && enemy->IsAlive())
        {
            Unit* v = enemy->GetVictim();
            now[enemy->GetGUID().GetCounter()] = { enemy->GetPositionX(), enemy->GetPositionY(), GluthChowGap(gluth, enemy),
                                                   uint32_t(enemy->GetHealthPct()), v ? v->GetName() : std::string("nobody") };
        }
    for (auto const& [guid, c] : seen)
        if (!now.count(guid) && c.dist <= kGluthEatSeen && c.pct > 0)
            TC_LOG_INFO("playerbot.v2.raid", "[raid_gluth] Zombie Chow {} gone by him (eaten, or killed there): last at {:.1f},{:.1f}, {:.0f} y (body gap) from Gluth at {:.1f},{:.1f}, {}% health, on {}",
                        guid, c.x, c.y, c.dist, gluth->GetPositionX(), gluth->GetPositionY(), c.pct, c.victim);
    // His health going up is a chow eaten (5% each, nothing else heals him): logged with the chows nearest him on the last plan, so an
    // eat the line above misses still shows (run 17: his health back up 10-15% at a time with no line).
    GluthHealth& h = g_gluthHealth[gluth->GetGUID().GetCounter()];
    if (!h.reachLogged)
    {
        h.reachLogged = true;
        TC_LOG_INFO("playerbot.v2.raid", "[raid_gluth] Gluth bounding radius {:.2f} y: a chow is eaten within {:.1f} y of his centre (2D)",
                    gluth->GetBoundingRadius(), GluthEatCentre(gluth));
    }
    uint32_t const pct = uint32_t(gluth->GetHealthPct());
    if (h.pct && pct >= h.pct + 3)
    {
        std::string near;
        for (auto const& [guid, c] : seen)
            if (!now.count(guid))
                near += Trinity::StringFormat(" {} at {:.0f} y ({}%, on {})", guid, c.dist, c.pct, c.victim);
        TC_LOG_INFO("playerbot.v2.raid", "[raid_gluth] Gluth healed {}% -> {}% at {:.1f},{:.1f}; chows gone since the last look:{}", h.pct, pct,
                    gluth->GetPositionX(), gluth->GetPositionY(), near.empty() ? std::string(" none") : near);
    }
    h.pct = pct;
    seen = std::move(now);
    if (g_gluthChowSeen.size() > 64)
        g_gluthChowSeen.clear();
    if (g_gluthHealth.size() > 64)
        g_gluthHealth.clear();
}

constexpr uint32_t kChowTaunts[] = { 355, 6795 };   // Taunt, Growl

void NaxxGluthChowPickups(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders, Unit* gluth)
{
    NoteGluthChowsEaten(plan, gluth);
    std::vector<Player*> chowTanks;
    std::unordered_map<Player*, int> postOf;
    for (Member const& m : plan.members)
    {
        int post = -1;
        if (m.bot && m.tank && m.p->IsAlive() && GluthJobOf(plan, gluth, m.p, post) == GluthJob::Chows)
        {
            chowTanks.push_back(m.p);
            postOf[m.p] = post;
        }
    }
    if (chowTanks.empty())
        return;
    auto const onChowTank = [&](Unit* chow)
    {
        Unit* v = chow->GetVictim();
        return v && std::find(chowTanks.begin(), chowTanks.end(), v) != chowTanks.end();
    };
    // The chows each chow tank holds, counting the ones given to it on this pass.
    std::unordered_map<Player*, int> held;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kZombieChow && enemy->IsAlive() && !enemy->HealthBelowPct(kGluthDecimatedPct) && onChowTank(enemy))
            ++held[static_cast<Player*>(enemy->GetVictim())];
    std::unordered_set<Player*> taken;
    std::unordered_set<Player*> shedding;   // a chow tank having one chow taunted off it on this pass
    auto const tauntReady = [](Player* t)
    {
        for (uint32_t const id : kChowTaunts)
            if (t->HasSpell(id))
                if (SpellInfo const* info = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE); info && t->GetSpellHistory()->IsReady(info))
                    return true;
        return false;
    };
    for (Unit* enemy : plan.enemies)
    {
        // A 5% chow walking to him after Decimate is the DPS' (NaxxGluthKillDecimated): a chow tank sent after it would follow it into his reach.
        if (enemy->GetEntry() != kZombieChow || !enemy->IsAlive() || enemy->HealthBelowPct(kGluthDecimatedPct))
            continue;
        // On a chow tank: left there, unless that tank holds kGluthShedGap more than another, which then taunts one off it (kGluthShedGap).
        Player* const holder = onChowTank(enemy) ? static_cast<Player*>(enemy->GetVictim()) : nullptr;
        if (holder)
        {
            if (shedding.count(holder))
                continue;
            Player* lightest = nullptr;
            for (Player* t : chowTanks)
                if (t != holder && !taken.count(t) && held[t] + kGluthShedGap <= held[holder] && tauntReady(t) &&
                    enemy->GetExactDist2d(kGluthChowPosts[postOf[t]].x, kGluthChowPosts[postOf[t]].y) <= kGluthBalancePickup &&
                    (!lightest || held[t] < held[lightest]))
                    lightest = t;
            if (!lightest)
                continue;
            auto it = orders.find(lightest->GetGUID().GetCounter());
            if (it == orders.end() || it->second.cast_kind != RaidOrder::CastKind::None)
                continue;
            shedding.insert(holder);
            taken.insert(lightest);
            it->second.attack_target = enemy->GetGUID();
            g_gluthGoOver[lightest->GetGUID().GetCounter()] = { enemy->GetGUID(), getMSTime() + kGluthGoOverMs };   // the brain's clock (GluthMove's now)
            for (uint32_t const id : kChowTaunts)
                if (lightest->HasSpell(id) && Ready(lightest, id, enemy, true))
                {
                    it->second.cast_kind = RaidOrder::CastKind::Taunt;
                    it->second.cast_spell = id;
                    it->second.cast_target = enemy->GetGUID();
                    break;
                }
            uint32_t const now = GameTime::GetGameTimeMS();
            uint32_t& logged = g_gluthShedLogged[lightest->GetGUID().GetCounter()];
            if (!logged || now - logged > 5000u)
            {
                logged = now ? now : 1u;
                TC_LOG_INFO("playerbot.v2.raid", "[raid_gluth] {} takes Zombie Chow {} off {} to even the load ({} {} / {} {}){}", lightest->GetName(),
                            enemy->GetGUID().GetCounter(), holder->GetName(), holder->GetName(), held[holder], lightest->GetName(), held[lightest],
                            it->second.cast_kind == RaidOrder::CastKind::Taunt ? ": taunts" : ": goes over");
            }
            continue;
        }
        // Headed into his reach, and still far enough out that the chows of the tank going for it stay out of it (beside the tank, a chow's side
        // and a margin past his eat reach): one already closer is his before a tank gets there, and the tank would bring its own along.
        Unit* const onWhom = enemy->GetVictim();
        float const chowFromHim = gluth->GetExactDist2d(enemy);
        bool const intoReach = chowFromHim >= GluthEatCentre(gluth) + kGluthChowSide + kGluthEatMargin &&
                               (chowFromHim <= kGluthReachNear || (onWhom && gluth->GetExactDist2d(onWhom) <= kGluthReachNear));
        // The chow tank holding fewest chows takes it, a warrior before a bear when they hold as many, then the nearer. One holding chows
        // goes only for a chow near its post (kGluthPostPickup), or a little further while it holds kGluthBalanceGap fewer than the other.
        // Until 0065 the nearest took it: the west post lies on two of the three spawns' ways, and its bear held 6-8 chows when it died at
        // 72-87 s in all three naxx_gluth 1118 r4 runs, the warrior on the south post 2-5 (dead at 87, 104 and 161 s; Decimate at 105 s).
        int most = 0;
        for (Player* t : chowTanks)
            most = std::max(most, held[t]);
        Player* best = nullptr;
        for (Player* t : chowTanks)
        {
            if (taken.count(t))
                continue;
            int const h = held[t];
            if (h > 0 && !intoReach)
            {
                GluthPost const& p = kGluthChowPosts[postOf[t]];
                float const reach = most - h >= kGluthBalanceGap ? kGluthBalancePickup : kGluthPostPickup;
                if (enemy->GetExactDist2d(p.x, p.y) > reach)
                    continue;
            }
            if (!best)
            {
                best = t;
                continue;
            }
            int const hb = held[best];
            bool const war = t->GetClass() == CLASS_WARRIOR, warBest = best->GetClass() == CLASS_WARRIOR;
            if (h != hb ? h < hb : war != warBest ? war : t->GetExactDist2d(enemy) < best->GetExactDist2d(enemy))
                best = t;
        }
        if (!best)
            continue;   // every chow tank holds chows and this one is far from their posts: it comes to a post on its way to the camp
        auto it = orders.find(best->GetGUID().GetCounter());
        if (it == orders.end() || it->second.cast_kind == RaidOrder::CastKind::Taunt)
            continue;
        taken.insert(best);
        ++held[best];
        // The other chow tanks leave it to this one: their own pick-up (the tank brain's nearest loose mob) took it all the same.
        for (Player* t : chowTanks)
            if (t != best)
                if (auto ot = orders.find(t->GetGUID().GetCounter()); ot != orders.end())
                    ot->second.forbidden_targets.push_back(enemy->GetGUID());
        it->second.attack_target = enemy->GetGUID();
        // A chow on a raider or on his tanks is taken off with a taunt once in reach: the attack alone does not take it off the raider
        // whose heals or damage it is on, and the swap tank and his tank do not hit it.
        if (Unit* v = enemy->GetVictim(); v && v != best && it->second.cast_kind == RaidOrder::CastKind::None && !enemy->HealthBelowPct(kGluthDecimatedPct))
            for (uint32_t const id : kChowTaunts)
                if (best->HasSpell(id) && Ready(best, id, enemy, true))
                {
                    it->second.cast_kind = RaidOrder::CastKind::Taunt;
                    it->second.cast_spell = id;
                    it->second.cast_target = enemy->GetGUID();
                    TC_LOG_INFO("playerbot.v2.raid", "[raid_gluth] {} taunts Zombie Chow {} off {} ({:.0f} y from Gluth)", best->GetName(),
                                enemy->GetGUID().GetCounter(), v->GetName(), gluth->GetExactDist2d(enemy));
                    break;
                }
        if (g_gluthChowLogged.insert(enemy->GetGUID().GetCounter()).second)
        {
            Unit* v = enemy->GetVictim();
            std::string counts;
            for (Player* t : chowTanks)
                counts += Trinity::StringFormat(" {} {}", t->GetName(), held[t] - (t == best ? 1 : 0));
            TC_LOG_INFO("playerbot.v2.raid", "[raid_gluth] loose Zombie Chow {} at {:.1f},{:.1f} on {}: {} goes for it ({:.0f} y), Gluth {:.0f} y from the chow; chows held:{}",
                        enemy->GetGUID().GetCounter(), enemy->GetPositionX(), enemy->GetPositionY(), v ? v->GetName() : std::string("nobody"),
                        best->GetName(), best->GetExactDist2d(enemy), gluth->GetExactDist2d(enemy), counts);
        }
    }
    if (g_gluthChowLogged.size() > 512)
        g_gluthChowLogged.clear();
    if (g_gluthWallLogged.size() > 64)
        g_gluthWallLogged.clear();
    if (g_gluthShedLogged.size() > 64)
        g_gluthShedLogged.clear();
    if (g_gluthGoOver.size() > 64)
        g_gluthGoOver.clear();

    // A warrior chow tank under many chows opens Shield Wall, then Last Stand once the wall is spent, at half health: its own rule waits for
    // 20-25%, and under 6-9 chows that is one swing round from death (naxx_gluth 1118 r4 run 1: Mctankwc 36% at 84 s, dead at 87 s, neither
    // cast; run 2: Last Stand at 17% and Shield Wall at 3%, a second apart).
    for (Player* t : chowTanks)
    {
        if (t->GetClass() != CLASS_WARRIOR || held[t] < kGluthWallChows || t->GetHealthPct() > kGluthWallPct)
            continue;
        if (t->HasAura(kGluthShieldWall) || t->HasAura(kGluthLastStand))
            continue;
        auto it = orders.find(t->GetGUID().GetCounter());
        if (it == orders.end() || it->second.cast_kind != RaidOrder::CastKind::None)
            continue;
        for (uint32_t const spell : { kGluthShieldWall, kGluthLastStand })
        {
            if (!t->HasSpell(spell) || !Ready(t, spell, t, false))
                continue;
            it->second.cast_kind = RaidOrder::CastKind::Prepare;
            it->second.cast_spell = spell;
            it->second.cast_target = t->GetGUID();
            uint32_t const now = GameTime::GetGameTimeMS();
            uint32_t& logged = g_gluthWallLogged[t->GetGUID().GetCounter()];
            if (!logged || now - logged > 5000u)
            {
                logged = now ? now : 1u;
                TC_LOG_INFO("playerbot.v2.raid", "[raid_gluth] {} opens {} at {:.0f}% under {} chows", t->GetName(),
                            spell == kGluthShieldWall ? "Shield Wall" : "Last Stand", t->GetHealthPct(), held[t]);
            }
            break;
        }
    }
}

// Decimate leaves every chow at 5% (7.3k of 146k) and sends it walking to him: each one he eats gives him 5%. The DPS kill them first, the
// ones nearest him first, a few DPS to each (ranged spread over them, melee on the nearest to it). The rule table's kill priority for a
// chow under 10% did not do it: "on the focus 0" in every sample and him from 85% to 100% and from 70% to 99% at two Decimates
// (naxx_gluth 1116 run 18).
std::unordered_map<uint64_t, uint32_t> g_gluthDecimateLogged;
void NaxxGluthKillDecimated(Plan& plan, std::unordered_map<uint64_t, RaidOrder>& orders, Unit* gluth, uint32_t now)
{
    std::vector<Unit*> decimated;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kZombieChow && enemy->IsAlive() && enemy->HealthBelowPct(kGluthDecimatedPct))
            decimated.push_back(enemy);
    if (decimated.empty())
        return;
    std::stable_sort(decimated.begin(), decimated.end(), [&](Unit* a, Unit* b) { return gluth->GetExactDist2d(a) < gluth->GetExactDist2d(b); });
    uint32_t& logged = g_gluthDecimateLogged[gluth->GetGUID().GetCounter()];
    if (!logged || now - logged > 30000u)
    {
        logged = now ? now : 1u;
        TC_LOG_INFO("playerbot.v2.raid", "[raid_gluth] Decimate: {} chows at 5%, the DPS kill them (nearest {:.0f} y from him)", decimated.size(),
                    gluth->GetExactDist2d(decimated.front()));
    }
    // A DPS stays on the decimated chow it hits until the chow dies, and the fight script owns its target meanwhile (script_focus: the
    // skull and kill-focus pickers stand down, and no threat hold keeps it off). Until 0066 every plan dealt the ranged out again in order
    // of the chows' distance from him, which changed as they walked, and the pickers took the bots back to him between plans: naxx_gluth
    // 1118 r6 run 1, a warlock on a new chow every second and on Gluth in between, the raid's damage from 25k a second to 1-7k for the 40 s
    // the 5% chows took to reach him, none of them killed; he ate them all, 22% -> 67% (run 3: 29% -> 89%).
    std::unordered_map<Unit*, int> on;
    auto const give = [&](RaidOrder& o, Unit* pick)
    {
        ++on[pick];
        o.active = true;
        o.attack_target = pick->GetGUID();
        o.script_owns = true;
        o.script_focus = pick->GetGUID();
        o.hold_target = ObjectGuid::Empty;
    };
    std::vector<std::pair<Member const*, RaidOrder*>> todo;
    for (Member const& m : plan.members)
    {
        if (!m.bot || m.tank || m.healer || !m.p->IsAlive())
            continue;
        auto it = orders.find(m.p->GetGUID().GetCounter());
        if (it == orders.end())
            continue;
        Unit* v = m.p->GetVictim();
        if (v && std::find(decimated.begin(), decimated.end(), v) != decimated.end())
            give(it->second, v);
        else
            todo.emplace_back(&m, &it->second);
    }
    // The rest: a ranged DPS to the chow fewest are on, the one nearest him first; a melee DPS to the chow nearest it.
    for (auto const& [m, o] : todo)
    {
        Unit* pick = nullptr;
        for (Unit* c : decimated)
        {
            if (!pick)
                pick = c;
            else if (m->ranged ? on[c] < on[pick] : m->p->GetExactDist2d(c) < m->p->GetExactDist2d(pick))
                pick = c;
        }
        give(*o, pick);
    }
    if (g_gluthDecimateLogged.size() > 64)
        g_gluthDecimateLogged.clear();
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
        if (!m.bot || !m.tank)
            continue;
        GluthJob const job = GluthJobOf(plan, gluth, m.p, post);
        auto it = orders.find(m.p->GetGUID().GetCounter());
        if (it == orders.end())
            continue;
        // His tank and the swap tank never attack a chow: the chow they hit comes to them by him, and he eats it, or it keeps the swap tank
        // from his side (see GluthMove).
        if (job == GluthJob::Holder || job == GluthJob::Swap)
        {
            for (Unit* enemy : plan.enemies)
                if (enemy->GetEntry() == kZombieChow)
                    it->second.forbidden_targets.push_back(enemy->GetGUID());
            continue;
        }
        if (job != GluthJob::Chows || it->second.cast_kind == RaidOrder::CastKind::Taunt)
            continue;   // a swap taunt elected for this tank still goes
        it->second.forbidden_targets.push_back(gluth->GetGUID());
    }
    NaxxGluthKillDecimated(plan, orders, gluth, now);
    NaxxGluthChowPickups(plan, orders, gluth);
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
    NaxxPrePullRazuvious(plan, orders, dump);
    if (g_razOpener.size() > 64)
        g_razOpener.clear();
    if (g_razOpenLogged.size() > 64)
        g_razOpenLogged.clear();
    if (g_gothDraw.size() > 64)
        g_gothDraw.clear();
    if (g_gothSplitSince.size() > 64)
        g_gothSplitSince.clear();
    NaxxPrePullThaddius(plan, orders, dump);
    NaxxPrePullThaddiusTwo(plan, orders, dump);
    if (g_thadTwoLogged.size() > 64)
        g_thadTwoLogged.clear();
    if (g_thadTwoSince.size() > 64)
        g_thadTwoSince.clear();
    if (g_thadSpot.size() > 512)
        g_thadSpot.clear();
    if (g_thadAnchor.size() > 64)
        g_thadAnchor.clear();
    if (g_thadDraw.size() > 64)
        g_thadDraw.clear();
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
constexpr float    kSprayWallPct   = 80.0f;   // before her enrage, a tank going into a spray below this opens the wall all the same
constexpr int      kSprayWallLateMs = 2500;   // ... once the spray is this close (the heals before it have landed)

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

    // Enraged, a stun is a tank's death: the wall first, Last Stand once the wall is spent. Before the enrage too when her tank goes into
    // the stun hurt: ten seconds of her swings with no block, parry or dodge and no heal landing. naxx_maexxna sweep-1 (af52): the
    // spiderlings at 72 s went on her tank, it went into the second spray at 71%, came out at 38% and died at 90 s; two more tanks died
    // in the third spray, and she evaded at 8.6% with the raid dead.
    Member* hm = FindMember(plan, holder);
    if (!hm->bot || !hm->tank || holder->GetClass() != CLASS_WARRIOR || e.busy.count(holder->GetGUID().GetCounter()))
        return;
    bool const enraged = maex->HasAura(kMaexEnrage);
    bool const hurt = holder->GetHealthPct() < kSprayWallPct && inMs <= kSprayWallLateMs;
    if ((!enraged && !hurt) || holder->HasAura(kShieldWall) || holder->HasAura(kLastStand))
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
            TC_LOG_INFO("playerbot.v2.raid", "[raid_maexxna] {} opens {} at {:.0f}%, {}, {:.1f} s before Web Spray", holder->GetName(),
                        spell == kShieldWall ? "Shield Wall" : "Last Stand", holder->GetHealthPct(), enraged ? "she is enraged" : "not yet enraged",
                        inMs / 1000.0f);
        return;
    }
}

bool NaxxMechanicsMove(Plan const& plan, Member const& self, uint32_t now, RaidOrder& out)
{
    if (g_horseSeat.size() > 256)
        g_horseSeat.clear();
    if (g_horsePassAt.size() > 64)
        g_horsePassAt.clear();
    return HeiganMove(plan, self, now, out) || SapphironMove(plan, self, now, out) || GothikSideMove(plan, self, out) ||
           ThaddiusTwoMove(plan, self, out) || ThaddiusTankMove(plan, self, out) || ThaddiusSideMove(plan, self, out) ||
           GluthMove(plan, self, now, out) || HorsemenMove(plan, self, now, out);
}

// A raider Heigan ported into the tunnel kills the Eye Stalks round it before it walks back (see kEyeStalk). Healers do not: they walk.
std::unordered_map<uint64_t, uint32_t> g_stalkLogged;
void ElectHeiganTunnel(Election& e, uint32_t now)
{
    Plan& plan = e.plan;
    Unit* heigan = nullptr;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kHeigan)
            heigan = enemy;
    if (!heigan)
        return;
    for (Member const& m : plan.members)
    {
        if (!m.bot || m.healer || !m.p->IsAlive() || !HeiganPorted(m.p, heigan))
            continue;
        Creature* stalk = NearestStalk(m.p);
        auto it = e.orders.find(m.p->GetGUID().GetCounter());
        if (!stalk || it == e.orders.end())
            continue;
        it->second.attack_target = stalk->GetGUID();
        uint32_t& logged = g_stalkLogged[m.p->GetGUID().GetCounter()];
        if (!logged || now - logged >= 10000u)
        {
            logged = now ? now : 1u;
            TC_LOG_INFO("playerbot.v2.raid", "[raid] Heigan: {} was ported to the tunnel, kills the Eye Stalk at {:.1f},{:.1f} ({:.0f} y) before the walk back",
                        m.p->GetName(), stalk->GetPositionX(), stalk->GetPositionY(), m.p->GetExactDist2d(stalk));
        }
    }
    if (g_stalkLogged.size() > 256)
        g_stalkLogged.clear();
}

// ---------------------------------------------------------------------------
// Gluth: Decimate
// ---------------------------------------------------------------------------
// Decimate (28374, every 105 s from the pull: CLASSIC_GLUTH_DECIMATE_CD in classic_boss_gluth.cpp) puts every player at 5% health (28375,
// classic_spell_gluth_decimate) and he keeps hitting his tank. All forty raiders then stand at the same 5%, the triage scores tie, and the
// healers spread over the raid: naxx_gluth 1118 r7-1, Decimate at 104 s, eight healers went to raiders at 4% and one to his tank, who
// died at 107 s (melee 2503), then the next tank at 115 s, and the raid with them. Before it lands the healers put what outlasts it on his
// tank (Power Word: Shield absorbs part of the hit, Renew and Rejuvenation heal after it), each by one healer, and keep every heal on
// his tank that would be stopped as overheal; when it has landed his tank gets the fast heals of kDecimateHolderHealers healers and
// every other tank one, until each stands above kDecimateHealedPct.
constexpr uint32_t kDecimate          = 28374;
constexpr uint32_t kDecimateCycleMs   = 105000;
constexpr uint32_t kDecimatePrepMs    = 6000;    // the jobs before it start this long before it is due
constexpr uint32_t kDecimateLateMs    = 5000;    // and stop this long after it was due, if the clock was off and none came
constexpr uint32_t kDecimateAfterMs   = 8000;    // the heals after it are elected this long after it landed
constexpr float    kDecimateLowPct    = 7.0f;    // a raider at this health or less counts as decimated
constexpr float    kDecimateHealedPct = 50.0f;   // a tank above this is healed enough; the normal heal choice takes over
constexpr int      kDecimateHolderHealers = 3;
constexpr uint32_t kMortalWound       = 25646;   // his tank swap's aura (RaidEncounterData_NAXX.cpp: swap at 5)
constexpr uint32_t kMortalWoundSwap   = 5;

struct GluthDecimate
{
    uint32_t engaged = 0;   // first plan that saw him in the fight
    uint32_t seen = 0;      // last plan that saw him (a gap means a new pull: his guid stays the same after a wipe)
    uint32_t last = 0;      // when the last Decimate landed (most of the raid at 5% at once)
    uint32_t logged = 0;    // the Decimate whose heal jobs were logged
};
std::unordered_map<uint64_t, GluthDecimate> g_gluthDecimate;

// Before Decimate: one healer for each, on his tank. Power Word: Shield is held back by Weakened Soul.
SprayJob const kDecimatePrepJobs[] = {
    { 17,  kWeakenedSoul, 0, "gluth: shield his tank before Decimate" },
    { 139, 0,             0, "gluth: renew his tank before Decimate" },
    { 774, 0,             0, "gluth: rejuvenation on his tank before Decimate" },
};
// After Decimate: the healer's fastest heal (Flash Heal, Flash of Light, Lesser Healing Wave, Regrowth), then a slower one.
uint32_t const kDecimateFastHeals[] = { 2061, 19750, 8004, 8936, 2050, 635, 331, 5185 };

void ElectGluthDecimate(Election& e, uint32_t now)
{
    Plan& plan = e.plan;
    Unit* gluth = nullptr;
    for (Unit* enemy : plan.enemies)
        if (enemy->GetEntry() == kGluth)
            gluth = enemy;
    if (!gluth)
        return;
    GluthDecimate& st = g_gluthDecimate[gluth->GetGUID().GetCounter()];
    if (st.engaged && now - st.seen > 10000)
        st = GluthDecimate();
    st.seen = now;
    if (!st.engaged)
        st.engaged = now;
    // Landed: most of the raid at 5% or less at once (the clock follows the real one from then on).
    int low = 0, alive = 0;
    for (Member const& m : plan.members)
        if (m.p->IsAlive())
        {
            ++alive;
            if (m.p->GetHealthPct() <= kDecimateLowPct)
                ++low;
        }
    if (alive >= 4 && low * 2 >= alive && (!st.last || now - st.last > kDecimateCycleMs / 2))
    {
        TC_LOG_INFO("playerbot.v2.raid", "[raid_gluth] Decimate landed: {} of {} at {:.0f}% or less, {:.1f} s after the clock said ({} s into the fight)",
                    low, alive, kDecimateLowPct, (int(now) - int((st.last ? st.last : st.engaged) + kDecimateCycleMs)) / 1000.0f, (now - st.engaged) / 1000);
        st.last = now;
    }
    uint32_t due = (st.last ? st.last : st.engaged) + kDecimateCycleMs;
    if (Spell const* cast = gluth->GetCurrentSpell(CURRENT_GENERIC_SPELL); cast && cast->GetSpellInfo()->Id == kDecimate)
        due = now + uint32_t(std::max(0, cast->GetRemainingCastTime()));
    Unit* const tank = gluth->GetVictim();
    Player* const holder = tank ? tank->ToPlayer() : nullptr;
    if (!holder || !FindMember(plan, holder))
        return;

    // After it: the fast heals on the tanks.
    if (st.last && now - st.last <= kDecimateAfterMs)
    {
        std::vector<Player*> tanks{ holder };
        for (Member const& m : plan.members)
            if (m.tank && m.p != holder && m.p->IsAlive())
                tanks.push_back(m.p);
        std::string jobs;
        for (Player* t : tanks)
        {
            if (!t->IsAlive() || t->GetHealthPct() > kDecimateHealedPct)
                continue;
            int const want = t == holder ? kDecimateHolderHealers : 1;
            int got = 0;
            for (bool allowCasting : { false, true })
                for (Member const& m : plan.members)
                {
                    if (got >= want)
                        break;
                    if (!m.bot || !m.healer || m.p == t || Busy(m.p) != allowCasting || e.busy.count(m.p->GetGUID().GetCounter()) ||
                        Stunned(m.p) || ManaPctOf(m.p) < 5)
                        continue;
                    uint32_t spell = 0;
                    for (uint32_t const base : kDecimateFastHeals)
                        if ((spell = KnownRank(m.p, SpellFamily::Ranks(base))) && Ready(m.p, spell, t, false))
                            break;
                        else
                            spell = 0;
                    if (!spell)
                        continue;
                    RaidOrder* o = e.OrderOf(m.p);
                    if (!o)
                        continue;
                    o->cast_kind = RaidOrder::CastKind::Prepare;
                    o->cast_spell = spell;
                    o->cast_target = t->GetGUID();
                    o->keep_heals = true;
                    e.busy.insert(m.p->GetGUID().GetCounter());
                    e.dump << "  " << m.p->GetName() << ": gluth: heal " << spell << " -> " << t->GetName() << " after Decimate\n";
                    jobs += Trinity::StringFormat(" {}->{}", m.p->GetName(), t->GetName());
                    ++got;
                }
        }
        if (st.logged != st.last && !jobs.empty())
        {
            st.logged = st.last;
            TC_LOG_INFO("playerbot.v2.raid", "[raid_gluth] after Decimate, {} at {:.0f}% holds him: heals{}", holder->GetName(), holder->GetHealthPct(), jobs);
        }
        return;
    }

    // Before it: what outlasts it, on his tank, and no other tank takes him from it now (its taunt would leave the shield and the heals
    // over time on the tank he just left).
    if (now + kDecimatePrepMs < due || now > due + kDecimateLateMs)
        return;
    for (Member const& m : plan.members)
        if (m.bot && m.healer)
            if (RaidOrder* ho = e.OrderOf(m.p))
                ho->keep_heals = true;
    // The swap waits for it while his tank is short of the swap's Mortal Wound stacks (the tank swap election runs after this one and
    // takes no busy tank); one already at them is swapped, and the jobs go to the new tank.
    if (Member const* hm = FindMember(plan, holder); hm && hm->tank && Stacks(holder, kMortalWound) < kMortalWoundSwap)
        for (Member const& m : plan.members)
            if (m.tank && m.p != holder)
                e.busy.insert(m.p->GetGUID().GetCounter());
    int const inMs = int(due) - int(now);
    for (SprayJob const& job : kDecimatePrepJobs)
    {
        if (AnyRankOn(holder, job.base) || (job.blockedBy && holder->HasAura(job.blockedBy)))
            continue;
        Ranks const ranks = SpellFamily::Ranks(job.base);
        for (Member const& m : plan.members)
        {
            if (!m.bot || !m.healer || m.p == holder || Busy(m.p) || e.busy.count(m.p->GetGUID().GetCounter()) || ManaPctOf(m.p) < 10)
                continue;
            uint32_t const spell = KnownRank(m.p, ranks);
            if (!spell || !Ready(m.p, spell, holder, false))
                continue;
            RaidOrder* o = e.OrderOf(m.p);
            if (!o)
                continue;
            o->cast_kind = RaidOrder::CastKind::Prepare;
            o->cast_spell = spell;
            o->cast_target = holder->GetGUID();
            e.busy.insert(m.p->GetGUID().GetCounter());
            e.dump << "  " << m.p->GetName() << ": " << job.what << " " << spell << " -> " << holder->GetName() << "\n";
            TC_LOG_INFO("playerbot.v2.raid", "[raid_gluth] {} casts {} on {} ({:.0f}%) {:.1f} s before Decimate", m.p->GetName(), spell,
                        holder->GetName(), holder->GetHealthPct(), inMs / 1000.0f);
            break;
        }
    }
}

void NaxxMechanicsElect(Election& e, uint32_t now)
{
    ElectHeiganTunnel(e, now);
    ElectRazuvious(e, now);
    if (g_razFight.size() > 64)
        g_razFight.clear();
    ElectFaerlina(e);
    ElectGothikSides(e);
    ElectMaexxnaSpray(e, now);
    if (g_maexSpray.size() > 64)
        g_maexSpray.clear();
    if (g_airSpread.size() > 256)
        g_airSpread.clear();
    ElectGluthDecimate(e, now);
    if (g_gluthDecimate.size() > 64)
        g_gluthDecimate.clear();
}

} // namespace Playerbot::Raid::detail
