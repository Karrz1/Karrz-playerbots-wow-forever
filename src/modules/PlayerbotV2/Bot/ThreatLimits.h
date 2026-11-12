// ThreatLimits - the one place the hard threat limits live, shared by the generic damage hold (States::EvaluateThreatHold) and the raid
// brain's hold orders (Raid::PublishThreatHolds). A mob switches to a non-tank at 110 % of the tank's threat in melee reach and 130 % at
// range; the hold never releases at or past these ratios, however long it has lasted (the release after a few seconds only applies below
// them). With separate copies, a caster at 1.15 x the tank was released by the brain and held for good by the generic gate.

#pragma once

namespace Playerbot {

constexpr float kThreatHardRatioMelee  = 1.0f;    // a melee DPS never releases above the tank's own threat
constexpr float kThreatHardRatioCaster = 1.10f;   // a caster keeps a tenth of margin under the 130 % a ranged attacker needs

// The core's aggro switch (ThreatManager::UpdateVictim): a mob turns on another attacker at 110 % of its victim's threat in melee reach and at
// 130 % at range. Below these a non-tank cannot take the mob, however close to the tank it holds.
constexpr float kAggroSwitchRatioMelee  = 1.10f;
constexpr float kAggroSwitchRatioRanged = 1.30f;

// A raid tank with tens of thousands of threat takes one cast as a few percent, so a caster keeps ten points under the 130 % there (n10
// mc_geddon: a mage held at 109 % of the tank for 20 s while the tank, out of melee in the Inferno, gained nothing). Early in a pull the
// tank holds a few thousand and one Frostbolt is a fifth of it: below kRaidTankThreatFloor the 110 % limit stays.
constexpr float kThreatHardRatioCasterRaid = 1.20f;
constexpr float kRaidTankThreatFloor = 8000.0f;

constexpr float ThreatHardRatio(bool melee, bool raid = false, float tank_threat = 0.0f)
{
    return melee ? kThreatHardRatioMelee
                 : (raid && tank_threat >= kRaidTankThreatFloor ? kThreatHardRatioCasterRaid : kThreatHardRatioCaster);
}

// A hold that starts at the tank's own threat is too late while the tank holds only a few thousand: the next cast is already in flight and
// is a large share of it (n14 Onyxia: tank 1586, a Frostbolt about 700, three casts took a mage to 132 % before the first poll could hold it).
// The hold therefore starts, and is not released, at the ratio from which one more typical hit still leaves the bot under the ratio that
// takes the boss (130 % ranged, 110 % melee, less a margin). That ratio reaches the normal limits once the tank holds a few thousand: a
// bound by tank threat alone froze casters for 20 s on Geddon, whose tank stands at 6-7k for half a minute (n17 mc_geddon).
constexpr float kNextHitThreatRanged = 1000.0f;   // a crit Frostbolt or Fireball
constexpr float kNextHitThreatMelee  = 350.0f;
constexpr float kPullSafeRanged      = 1.25f;
constexpr float kPullSafeMelee       = 1.05f;
constexpr float kMinEarlyHoldRatio   = 0.4f;

// The ratio of the tank's threat a non-tank holds at: <base>, or lower while one typical hit would take the bot past kPullSafe* (never
// under kMinEarlyHoldRatio). Unknown tank threat leaves <base>.
// <share> is what stays of the tank's threat after the boss's scheduled cut (1 when it has none): the pull-safe ratio is measured against the
// tank as it will be after the cut (n17 Onyxia: a crit took a mage held at 75 % of the tank to 105 % of it, 140 % of the tank after the cut).
constexpr float ThreatEarlyHoldRatio(float base, bool melee, float tank_threat, float share = 1.0f)
{
    if (tank_threat <= 0.0f)
        return base;
    float const r = (melee ? kPullSafeMelee : kPullSafeRanged) * share - (melee ? kNextHitThreatMelee : kNextHitThreatRanged) / tank_threat;
    float const floor = kMinEarlyHoldRatio < base ? kMinEarlyHoldRatio : base;
    float const lowered = r < base ? r : base;
    return lowered > floor ? lowered : floor;
}

// The class threat drops (Fade, Feign Death, Feint, Cower): spell, the aura it needs, and whether it is cast on the mob. The rotations
// cast them when a bot holds kThreatDropPct of the tank's threat on a mob; the generic hold casts them once at the hard limit.
struct ThreatDrop { unsigned char cls; unsigned spell; unsigned needs_aura; bool on_target; };
constexpr ThreatDrop kThreatDrops[] = {
    { 5, 586,  0,   false },   // Priest: Fade
    { 3, 5384, 0,   false },   // Hunter: Feign Death
    { 4, 1966, 0,   true  },   // Rogue: Feint
    { 11, 8998, 768, true },   // Druid: Cower (Cat Form)
};
constexpr int kThreatDropPct = 60;   // percent of the tank's threat from which a rotation sheds threat (VMaNGOS medium-threat trigger)
constexpr int kThreatHighPct = 80;   // VMaNGOS high-threat trigger

// How long a raid's DPS wait after the tank's first hit on a boss before they open by themselves (the owner's Attack still releases them at
// once). The tank runs in from the room's edge and builds its lead in that time; the dungeons' parties keep their own, shorter wait. The one
// number for the generic boss opening and for every boss fight script (Raid::Script::Fight::startWaitMs).
constexpr unsigned kRaidBossOpenWaitMs = 8000;

// The "a forgotten Attack must not wipe the raid" release (Playerbot.Raid.DpsWaitTimeoutMs): after the tanks have fought this long without the
// owner's Attack the DPS go anyway. A raid's, limited to the same eight seconds and defaulting to it; a party keeps its six.
constexpr unsigned kRaidDpsWaitTimeoutMaxMs = kRaidBossOpenWaitMs;
constexpr unsigned kPartyDpsWaitTimeoutMs   = 6000;

// Past the hard limit the over-threat hold waits for the tank's threat to rise. A tank that is stunned, feared, dead, in an Inferno
// or out of reach never raises it, and every DPS and healer at the limit stood idle for as long as that lasted.
// After kStalledTankHoldMs on a tank whose threat has not moved the hold releases: the class threat drop (Fade, Feign Death, Feint,
// Cower) has had its chance during the hold, and plain fighting takes over. The same clock bounds the healer's near-tank hold.
constexpr unsigned kStalledTankHoldMs = 6000;

struct StalledTankClock { unsigned since_ms = 0; unsigned last_ms = 0; float tank_threat = 0.0f; };

// One poll of a hold past the hard limit: true once the hold has lasted kStalledTankHoldMs with the tank's threat at a standstill.
// The clock restarts when the tank's threat rises since the previous poll (the tank is building, the hold is doing its job) and after
// gap_ms without a poll (a new hold). The threat it compares against is the last one polled, not the one the clock started with: after a
// threat wipe (a boss's reset or cut, the support scorecard's next run) the tank rebuilds from below the old value, and against the old
// value it never "rose" - 242 support scorecard: the fury held over from one run to the next was released every poll for the whole run
// ("tank threat stalled at 3697 for 6000 ms", then 5324, 6926 ...) and kept the boss from the paladin tank for 116 of 120 s.
constexpr bool StalledTankHoldExpired(StalledTankClock& c, unsigned now, float tank_threat, unsigned gap_ms)
{
    if (!c.since_ms || now - c.last_ms > gap_ms || tank_threat > c.tank_threat)
        c.since_ms = now;
    c.tank_threat = tank_threat;
    c.last_ms = now;
    return now - c.since_ms >= kStalledTankHoldMs;
}

}  // namespace Playerbot
