// ClassicHealScope - where the Classic healers' mana-efficiency rules apply (Combat/Apl/ApClassicHeal.h RaidHealing).
//
// They used to run in raid maps only. A level 21 party in Deadmines (playtest, 2026-10-09) runs out of mana for the same reasons a
// raid does - heals sized to the deficit, the efficient spell for everyone but an emergency or a tank under half health, heals
// already on their way counted, topping up held back at low mana, the tank pre-healed - so every group inside an instance map
// (5-man dungeon or raid) gets them. Open-world parties, solo play and battlegrounds keep the plain rules.
// Pure: no game types, tested in tests/game/ClassicHealScope.cpp.

#pragma once

namespace Playerbot::Combat::ClassicHeal {

inline bool EfficiencyRulesApply(bool raid_map, bool dungeon_map, bool battleground, bool vanilla_realm)
{
    return vanilla_realm && !battleground && (raid_map || dungeon_map);
}

// Playerbot.Behavior.HealerAssist (Discord 2026-11-16: "healers are extremely passive in combat, they don't use crowd control nor
// offensive spells"). Inside the efficiency scope a healer cast damage only after its patients needed no healing for 10 s and at
// 90 % mana or more; a five-player pull is often over by then, so the healer never helped. Outside raids (raid rules) the switch
// brings that to 3 s and 60 %. A raid keeps 10 s and 90 %: there the healer's mana is the encounter.
struct HealerDamageGate
{
    unsigned quiet_ms;
    int      mana_pct;
};
inline constexpr HealerDamageGate kRaidHealerDamageGate    { 10000u, 90 };
inline constexpr HealerDamageGate kDungeonHealerDamageGate {  3000u, 60 };

inline constexpr HealerDamageGate HealerDamageGateFor(bool raid_rules, bool healer_assist)
{
    return (healer_assist && !raid_rules) ? kDungeonHealerDamageGate : kRaidHealerDamageGate;
}

// The same switch lets a healer bot outside a raid hold an add with its own crowd control (Shackle Undead, Hibernate), but only
// while everyone it heals is at kHealerCcHealthPct or more and its own mana at kHealerCcManaPct or more, and in an instance it
// starts a new hold at most every kHealerCcNewHoldMs, after the damage dealers took theirs. Without these limits it is what the
// switch's off state guards against: rfd_tutenkash 127, a priest shackled each fresh ghoul between Greater Heals, 94 % to 15 %
// mana in 27 s, and the tank died.
inline constexpr int      kHealerCcManaPct   = 60;
inline constexpr int      kHealerCcHealthPct = 70;
inline constexpr unsigned kHealerCcNewHoldMs = 20000u;

inline constexpr bool HealerMayCrowdControl(bool healer_assist, bool raid_rules, int mana_pct, int lowest_hp_pct)
{
    return healer_assist && !raid_rules && mana_pct >= kHealerCcManaPct && lowest_hp_pct >= kHealerCcHealthPct;
}

// A damage-spec paladin (Retribution or Protection) heals only itself (leenergetic, Discord 2026-11-17: "When in a DPS spec, Paladins
// don't cast heals on allies at all"). Outside raid rules it now saves a group member in trouble the way a player would: a member at
// kHybridSaveHpPct or less while no healer of the group covers him (none alive on the map, none in heal range of him, none with
// kHybridHealerCoverManaPct mana), or at kHybridSaveAlwaysPct or less whoever covers him, with kHybridSaveManaPct of its own mana.
// A raid keeps its healers' plan: there the paladin's mana and its threat are the encounter's.
inline constexpr int kHybridSaveHpPct           = 35;
inline constexpr int kHybridSaveAlwaysPct       = 20;
inline constexpr int kHybridSaveManaPct         = 20;
inline constexpr int kHybridHealerCoverManaPct  = 10;

inline constexpr bool HybridMaySaveAlly(bool raid_rules, int patient_hp_pct, bool healer_covers, int mana_pct)
{
    if (raid_rules || mana_pct < kHybridSaveManaPct)
        return false;
    return patient_hp_pct <= kHybridSaveAlwaysPct || (patient_hp_pct <= kHybridSaveHpPct && !healer_covers);
}

}  // namespace Playerbot::Combat::ClassicHeal
