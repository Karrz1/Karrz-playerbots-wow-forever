#include "RaidEncounters.h"

// Ported from chris's raid10-revamp ike3 bots (Karrz1/wow-1.12). Script references (mc/..., bwl/..., ony/...) point at the
// VMaNGOS scripts that Forever's scripts/Custom/Classic instances were ported from; the ids are the same.

namespace Playerbot::Raid {

    // Movement and positioning for Molten Core (map 409). Maintained by the lead engineer.
    void RegisterMoltenCoreMovement(RaidRules& r)
    {
        // Living Bomb (Baron Geddon): the carrier runs from the group, everybody else keeps clear of the carrier.
        r.carrierRun.push_back({ 20475, 16.0f, Who::Anyone });
        r.avoidCarrier.push_back({ 20475, 14.0f, Who::Anyone });
        // Inferno (Baron Geddon, rooted for 8 seconds): everybody leaves his range from the cast start until the aura ends.
        // The damage spell 19698 reaches 20 yards (spell data); 26 gives a bot a still-moving margin, since the first ticks are
        // light (500) and the last (5000) kill (MC Geddon 2026-10-11: the 22 y circle left bots at 16-21 y to the end).
        r.avoidEnemyAura.push_back({ 12056, 19695, 24.0f, Who::Anyone });
        // The spell reaches 20 yards from his centre plus the victim's combat reach (WorldObject::IsInRange2d: ~21.5), and the circle above opens
        // only when the cast starts, with the heaviest ticks (4000-5000) five seconds in: casters and healers stand outside it all fight, as
        // vanilla raids do, so only the melee run out. MC Geddon 2026-10-12 b0a1c616: 27 Inferno deaths at 12-21 y, 14 of them in one tick, all
        // "raid:move" - the ranged block stood 22 y behind the tank, which is 13-20 y from Geddon, and was still running when the ticks landed.
        // The ranged / healer slot rows are measured from the circle's centre (the boss), not from the tank, and start outside radius + hazard
        // margin (RaidRangedApproachTank), so the circle and the stand slots are the same number. Heal range to the tank is 40 y.
        r.avoidEnemy.push_back({ 12056, 24.0f, Who::Ranged });
        r.avoidEnemy.push_back({ 12056, 24.0f, Who::Healer });
        // Threat resets and teleports that leave the boss on a non-tank: Lucifron, Magmadar (Panic), Gehennas, Shazzrah (Gate), Ragnaros (Wrath
        // 20566 runs ResetThreatList, boss_ragnaros.cpp:198). A tank taunts the boss back at once; with no one in melee reach he casts Magma Blast
        // (6000 fire, range unlimited) on whoever holds threat: three casters died to it in the last fight.
        for (uint32_t boss : { 12118u, 11982u, 12259u, 12264u, 11502u })
            r.retaunt.push_back(boss);
        // Ragnaros: Elemental Fire (20564, 2400 fire a tick for 8 s) is reapplied by every swing on the current victim. The tank who carries it
        // hands over to a clean one (taunt 10 s), so each tank takes one application per turn; the holder with it stops attacking while a clean
        // tank exists. Tanks drink Greater Fire Protection when he is engaged.
        r.tankSwap.push_back({ 11502, 20564, 1 });
        r.resistPotion.push_back({ 11502, 13457, Who::Tank });
        // Ragnaros' Lava Burst (21908) puts a trap (game object 178088, spell 21158: 1000 fire to the one who triggers it, radius 3.5) under the
        // raid for 3 s. Everybody but the tanks (who must stay in his melee reach) steps off it.
        r.avoidGameObject.push_back({ 178088, 5.0f, Who::NonTank });
        // Gehennas: Rain of Fire ground effects.
        r.avoidGroundEffect.push_back({ 12259, 19717, 3.0f, Who::Anyone });
        // Magmadar: Lava Bomb 19411 / 20474 makes the target summon a fire patch (Summon Lava Bomb 20494 / 20495, 30 / 60 s).
        // In 1.12 data both summon 177704, a trap of radius 5 that casts Conflagration 19428 (400 fire per second for 8 s);
        // 181742 is the later "small" patch, kept in case the 1.14 client data summons it.
        r.avoidGameObject.push_back({ 177704, 7.0f, Who::Anyone });
        r.avoidGameObject.push_back({ 181742, 5.0f, Who::Anyone });
        // Magmadar's tactic (chris 2026-10-10): the main tank takes him to the far side from the raid, so he turns his back on
        // everybody else (his frontal attacks, Lava Bomb victims and Panic runners then do not start at the raid), and the raid
        // stands in the 140-degree sector behind him. Melee at his heels, casters and healers further back but well inside heal range.
        r.faceAway.push_back({ 11982 });
        r.standBehind.push_back({ 11982, Who::Melee, 3.0f, 7.0f, 80.0f });
        r.standBehind.push_back({ 11982, Who::Ranged, 18.0f, 26.0f, 70.0f });
        r.standBehind.push_back({ 11982, Who::Healer, 18.0f, 26.0f, 70.0f });
        // Shazzrah blinks onto a random player and Arcane Explosions: casters and healers spread out.
        r.spread.push_back({ 12264, 11.0f, Who::Ranged });
        r.spread.push_back({ 12264, 11.0f, Who::Healer });
        // Ragnaros: ranged and healers do not stack.
        r.spread.push_back({ 11502, 9.0f, Who::Ranged });
        r.spread.push_back({ 11502, 9.0f, Who::Healer });
        // Ragnaros' knockback (Wrath) reaches 20 yards from him; the Firesworn explode on death with a knockback: casters and healers keep off both (ike3).
        r.avoidEnemy.push_back({ 11502, 20.0f, Who::Ranged });
        r.avoidEnemy.push_back({ 11502, 20.0f, Who::Healer });
        // Only a dying one: a Firesworn at full health is just an add (MC Garr 2026-10-10: every ranged and healer bot ran from each live add
        // and a healer died doing it).
        // Healers only: a hazard step-out outranks a cast, and the ranged DPS stepped away from the add they were finishing (adds
        // sat at 12, 6 and 5 percent for 10 s while five bots ran from them); casters choose their own casting spot by range.
        r.avoidEnemy.push_back({ 12099, 15.0f, Who::Healer, 15.0f });
        // Garr: the Firesworn carry Separation Anxiety (23487, mc/boss_garr.cpp:105): an add that strays from Garr is punished.
        // The tanks holding them keep within 30 yards of Garr (a tank further out walks back toward him).
        r.stayNear.push_back({ 12057, 30.0f, Who::Tank });
        // Golemagg: main tank holds him at the tank camp, the off tank drags the Core Ragers to the far camp, healers stand between.
        r.stand.push_back({ 11988, Who::MainTank, 795.73f, -994.88f, -207.19f, 6.0f, true, 0 });
        r.stand.push_back({ 11988, Who::OffTank, 846.65f, -1019.06f, -198.98f, 6.0f, true, 11672 });
        r.stand.push_back({ 11988, Who::Healer, 821.2f, -1007.0f, -203.0f, 8.0f, false, 0 });
    }
} // namespace Playerbot::Raid
