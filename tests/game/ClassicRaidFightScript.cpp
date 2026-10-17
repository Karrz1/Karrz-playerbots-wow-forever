/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "tc_catch2.h"

#include "../../src/modules/PlayerbotV2/Bot/Raid/KickRules.h"
#include "../../src/modules/PlayerbotV2/Bot/Raid/RaidEncounters.h"
#include "../../src/modules/PlayerbotV2/Bot/Raid/RaidFightScript.h"
#include "../../src/modules/PlayerbotV2/Bot/Raid/ShieldRules.h"

using namespace Playerbot::Raid::Script;

namespace
{
constexpr uint32_t kMc = 409, kLucifron = 12118, kProtector = 12119;

Fight const& Luc()
{
    Fight const* f = FightFor(kMc, kLucifron);
    REQUIRE(f != nullptr);
    return *f;
}

Enemy E(uint64_t guid, uint32_t entry, float hp, bool eligible = true)
{
    Enemy e;
    e.guid = guid;
    e.entry = entry;
    e.hpPct = hp;
    e.eligible = eligible;
    return e;
}
}

TEST_CASE("Lucifron's script covers Lucifron and his Protectors on Molten Core only", "[ClassicRaidFightScript]")
{
    CHECK(FightFor(kMc, kLucifron) != nullptr);
    CHECK(FightFor(kMc, kProtector) == FightFor(kMc, kLucifron));
    CHECK(FightFor(kMc, 11982) == nullptr);      // Magmadar has none yet
    CHECK(FightFor(469, kLucifron) == nullptr);  // another map
}

TEST_CASE("The Protectors die before Lucifron, one at a time", "[ClassicRaidFightScript]")
{
    Enemy const all[] = { E(1, kLucifron, 100.f), E(2, kProtector, 100.f), E(3, kProtector, 100.f) };
    // Equal health: the lower guid, then the hurt one wins.
    CHECK(PickFocus(Luc(), all, 3, 0, 0) == 2);
    Enemy const hurt[] = { E(1, kLucifron, 100.f), E(2, kProtector, 100.f), E(3, kProtector, 80.f) };
    CHECK(PickFocus(Luc(), hurt, 3, 0, 0) == 3);
    // Both Protectors dead (or not eligible): Lucifron.
    Enemy const last[] = { E(1, kLucifron, 100.f), E(2, kProtector, 0.f, false), E(3, kProtector, 0.f, false) };
    CHECK(PickFocus(Luc(), last, 3, 0, 0) == 1);
}

TEST_CASE("The pick does not flip between two Protectors", "[ClassicRaidFightScript]")
{
    // Guid 2 was the pick; guid 3 is now slightly more hurt. The raid stays on 2 until it dies.
    Enemy const e[] = { E(1, kLucifron, 100.f), E(2, kProtector, 60.f), E(3, kProtector, 59.f) };
    CHECK(PickFocus(Luc(), e, 3, 0, 2) == 2);
    // The previous pick is dead: the other Protector.
    Enemy const dead[] = { E(1, kLucifron, 100.f), E(2, kProtector, 0.f, false), E(3, kProtector, 59.f) };
    CHECK(PickFocus(Luc(), dead, 3, 0, 2) == 3);
    // A previous pick of a later rank is dropped for an earlier one (Lucifron picked while no Protector was eligible).
    Enemy const back[] = { E(1, kLucifron, 100.f), E(2, kProtector, 100.f) };
    CHECK(PickFocus(Luc(), back, 2, 0, 1) == 2);
}

TEST_CASE("The owner's skull wins over the script's order", "[ClassicRaidFightScript]")
{
    Enemy const e[] = { E(1, kLucifron, 100.f), E(2, kProtector, 100.f), E(3, kProtector, 100.f) };
    CHECK(PickFocus(Luc(), e, 3, 1, 0) == 1);
    // A mark on an enemy that cannot be attacked (controlled, dead) is ignored.
    Enemy const shut[] = { E(1, kLucifron, 100.f, false), E(2, kProtector, 100.f), E(3, kProtector, 100.f) };
    CHECK(PickFocus(Luc(), shut, 3, 1, 0) == 2);
}

TEST_CASE("Nothing eligible or nothing on the list names no target", "[ClassicRaidFightScript]")
{
    Enemy const none[] = { E(1, kLucifron, 100.f, false), E(2, kProtector, 100.f, false) };
    CHECK(PickFocus(Luc(), none, 2, 0, 0) == 0);
    Enemy const stray[] = { E(9, 11671, 100.f) };   // a Core Hound is not part of the fight
    CHECK(PickFocus(Luc(), stray, 1, 0, 0) == 0);
    CHECK(PickFocus(Luc(), nullptr, 0, 0, 0) == 0);
}

TEST_CASE("The DPS start wait is bounded and starts at the first engaged enemy", "[ClassicRaidFightScript]")
{
    uint32_t const wait = Luc().startWaitMs;
    CHECK(wait == 8000u);                              // the raid's one boss opening wait: the tank takes a while to run in
    CHECK(wait == Playerbot::kRaidBossOpenWaitMs);
    CHECK_FALSE(StartWait(0, 10000, wait));            // nothing engaged yet: no clock
    CHECK(StartWait(10000, 10000, wait));
    CHECK(StartWait(10000, 17999, wait));
    CHECK_FALSE(StartWait(10000, 18000, wait));        // over: no threat hold afterwards, ever
    CHECK_FALSE(StartWait(10000, 9000, wait));         // a clock that ran backwards does not freeze the raid
}

// ---- Garr ----

namespace
{
constexpr uint32_t kGarr = 12057, kFiresworn = 12099;

Fight const& GarrFight()
{
    Fight const* f = FightFor(kMc, kGarr);
    REQUIRE(f != nullptr);
    return *f;
}
}

TEST_CASE("Garr's script covers Garr alone: the Firesworn are not its enemies, so their Banish stays", "[ClassicRaidFightScript]")
{
    CHECK(FightFor(kMc, kGarr) != nullptr);
    CHECK(FightFor(kMc, kFiresworn) == nullptr);        // the script drops crowd control on its own enemies only
    CHECK(FightFor(469, kGarr) == nullptr);             // another map
    CHECK(FightFor(kMc, kGarr) != FightFor(kMc, kLucifron));
    CHECK(GarrFight().startWaitMs == Playerbot::kRaidBossOpenWaitMs);
}

TEST_CASE("Every DPS bot hits Garr, whatever the Firesworn do", "[ClassicRaidFightScript]")
{
    Enemy const all[] = { E(1, kGarr, 100.f), E(2, kFiresworn, 30.f), E(3, kFiresworn, 100.f) };
    CHECK(PickFocus(GarrFight(), all, 3, 0, 0) == 1);   // the Firesworn rank behind everything: not on the list
    Enemy const noGarr[] = { E(1, kGarr, 100.f, false), E(2, kFiresworn, 30.f) };
    CHECK(PickFocus(GarrFight(), noGarr, 2, 0, 0) == 0); // Garr down or out of reach: the generic rules pick
    CHECK(PickFocus(GarrFight(), all, 3, 2, 0) == 2);    // the owner's skull still wins
}

TEST_CASE("Baron Geddon has the one-boss script: eight second start wait, DPS on him", "[ClassicRaidFightScript]")
{
    constexpr uint32_t kGeddon = 12056;
    Fight const* f = FightFor(kMc, kGeddon);
    REQUIRE(f != nullptr);
    CHECK(f->startWaitMs == Playerbot::kRaidBossOpenWaitMs);
    CHECK(FightFor(469, kGeddon) == nullptr);
    CHECK(f != FightFor(kMc, kGarr));
    Enemy const e[] = { E(1, kGeddon, 80.f) };
    CHECK(PickFocus(*f, e, 1, 0, 0) == 1);
    Enemy const out[] = { E(1, kGeddon, 80.f, false) };
    CHECK(PickFocus(*f, out, 1, 0, 0) == 0);
}

TEST_CASE("Shazzrah has the one-boss script", "[ClassicRaidFightScript]")
{
    constexpr uint32_t kShazzrah = 12264;
    Fight const* f = FightFor(kMc, kShazzrah);
    REQUIRE(f != nullptr);
    CHECK(f->startWaitMs == Playerbot::kRaidBossOpenWaitMs);
    Enemy const e[] = { E(1, kShazzrah, 100.f) };
    CHECK(PickFocus(*f, e, 1, 0, 0) == 1);
    CHECK(f != FightFor(kMc, 12056));
}

TEST_CASE("Sulfuron's priests die before him, one at a time, the same one for everybody", "[ClassicRaidFightScript]")
{
    constexpr uint32_t kSulfuron = 12098, kPriest = 11662;
    Fight const* f = FightFor(kMc, kSulfuron);
    REQUIRE(f != nullptr);
    CHECK(FightFor(kMc, kPriest) == f);
    CHECK(f->startWaitMs == Playerbot::kRaidBossOpenWaitMs);
    Enemy const all[] = { E(1, kSulfuron, 100.f), E(2, kPriest, 100.f), E(3, kPriest, 70.f), E(4, kPriest, 100.f), E(5, kPriest, 100.f) };
    CHECK(PickFocus(*f, all, 5, 0, 0) == 3);             // the most hurt priest first
    CHECK(PickFocus(*f, all, 5, 0, 3) == 3);             // and no flip-flop while it lives
    Enemy const last[] = { E(1, kSulfuron, 100.f), E(2, kPriest, 0.f, false), E(3, kPriest, 0.f, false), E(4, kPriest, 0.f, false), E(5, kPriest, 0.f, false) };
    CHECK(PickFocus(*f, last, 5, 0, 3) == 1);            // all four down: Sulfuron
}

TEST_CASE("Golemagg's script names him alone; the Core Ragers are never the focus", "[ClassicRaidFightScript]")
{
    constexpr uint32_t kGolemagg = 11988, kRager = 11672;
    Fight const* f = FightFor(kMc, kGolemagg);
    REQUIRE(f != nullptr);
    CHECK(FightFor(kMc, kRager) == nullptr);   // not the script's enemies: the rules that keep them off every target stay in charge
    CHECK(f->startWaitMs == Playerbot::kRaidBossOpenWaitMs);
    Enemy const e[] = { E(1, kGolemagg, 60.f), E(2, kRager, 20.f), E(3, kRager, 20.f) };
    CHECK(PickFocus(*f, e, 3, 0, 0) == 1);
    Enemy const noBoss[] = { E(1, kGolemagg, 60.f, false), E(2, kRager, 20.f) };
    CHECK(PickFocus(*f, noBoss, 2, 0, 0) == 0);
}

// ---- Majordomo, version B ----

TEST_CASE("Majordomo: only the owner's skull is hit, and he is never the focus", "[ClassicRaidFightScript]")
{
    constexpr uint32_t kMajordomo = 12018, kHealer = 11663, kElite = 11664;
    Fight const* f = FightFor(kMc, kMajordomo);
    REQUIRE(f != nullptr);
    CHECK(f->skullOnly);
    CHECK(FightFor(kMc, kHealer) == f);
    CHECK(FightFor(kMc, kElite) == f);
    CHECK(f->startWaitMs == Playerbot::kRaidBossOpenWaitMs);
    Enemy const all[] = { E(1, kMajordomo, 100.f), E(2, kElite, 50.f), E(3, kElite, 100.f), E(4, kHealer, 100.f), E(5, kHealer, 80.f) };
    CHECK(PickFocus(*f, all, 5, 0, 0) == 0);              // no skull: nobody is hit, however hurt
    CHECK(PickFocus(*f, all, 5, 4, 5) == 4);              // the skull, not the last pick
    Enemy const sheeped[] = { E(1, kMajordomo, 100.f), E(4, kHealer, 100.f, false), E(5, kHealer, 80.f) };
    CHECK(PickFocus(*f, sheeped, 3, 4, 0) == 0);          // a skull that cannot be hit is not replaced by another
}

TEST_CASE("Majordomo: the second tank holds him, the Healers' Shock circle is 21 yards", "[ClassicRaidFightScript]")
{
    CHECK(Playerbot::Raid::OffTankMob(kMc, 12018));
    CHECK_FALSE(Playerbot::Raid::OffTankMob(kMc, 11664));
    Playerbot::Raid::RaidRules const* mc = Playerbot::Raid::RulesForMap(kMc);
    REQUIRE(mc != nullptr);
    CHECK(mc->tankSpot.empty());
    int shock = 0;
    for (auto const& a : mc->avoidEnemy)
        if (a.enemy == 11663)
        {
            CHECK(a.radius == 21.0f);
            ++shock;
        }
    CHECK(shock == 2);
}

// Majordomo's shields take half the raid off an add, not all of it: Magic Reflection the casters, Damage Shield the melee DPS. A hunter and a
// tank are stopped by neither (acmc, n040, n124 Majordomo 2026-10-15: either shield idled everybody for 10 s of every 30).
TEST_CASE("Majordomo: Magic Reflection stops the casters, Damage Shield the melee, nothing else", "[ClassicRaidFightScript]")
{
    using namespace Playerbot::Raid::Shields;
    CHECK(ShieldForbids(kMagicReflection, false, true));     // caster off a reflecting add
    CHECK_FALSE(ShieldForbids(kMagicReflection, true, false)); // melee keeps swinging at it
    CHECK(ShieldForbids(kDamageShield, true, false));        // melee off a Damage Shield add
    CHECK_FALSE(ShieldForbids(kDamageShield, false, true));  // casters keep casting at it
    CHECK_FALSE(ShieldForbids(kMagicReflection, false, false)); // hunter / tank: neither
    CHECK_FALSE(ShieldForbids(kDamageShield, false, false));
    CHECK_FALSE(ShieldForbids(21086, true, true));           // Encouragement is no shield
}

// The cast API refuses a spell at a reflecting target only when the reflect can send it back (Spell::m_canReflect). The tanks' abilities go through,
// so a tank keeps Taunt, Sunder Armor, Revenge, Heroic Strike and Shield Slam on a Magic Reflection add (n124 Majordomo 2026-10-15: all five refused).
TEST_CASE("Spell reflect refusal: magic spells are refused, abilities and reflect-piercing spells are not", "[ClassicRaidFightScript]")
{
    using namespace Playerbot::Raid::Shields;
    //                    magic   ability allow  noRefl noImm  passive
    CHECK(ReflectRefuses(true,  false,  false, false, false, false));   // Frostbolt, Shadow Bolt, Polymorph
    CHECK_FALSE(ReflectRefuses(true,  true,   false, false, false, false));   // a magic-class ability (Taunt-like)
    CHECK_FALSE(ReflectRefuses(false, true,   false, false, false, false));   // Sunder Armor, Heroic Strike, Revenge, Shield Slam
    CHECK_FALSE(ReflectRefuses(false, false,  false, false, false, false));   // a melee-class spell with no ability flag: not reflectable either
    CHECK(ReflectRefuses(false, true,   true,  false, false, false));   // flagged to allow reflection: refused whatever its class
    CHECK_FALSE(ReflectRefuses(true,  false,  false, true,  false, false));   // pierces reflection
    CHECK_FALSE(ReflectRefuses(true,  false,  false, false, true,  false));   // pierces immunities
    CHECK_FALSE(ReflectRefuses(true,  false,  false, false, false, true));    // a passive
}

// His Teleport resets his threat list every 20-30 s and nobody hits him (Priority -1), so he is on the retaunt list like the other threat-reset
// bosses (acmc 2026-10-15: on two healers for 30 s after a teleport).
TEST_CASE("Majordomo: a tank taunts him back after his teleport's threat reset", "[ClassicRaidFightScript]")
{
    CHECK(Playerbot::Raid::Retaunts(kMc, 12018));
    CHECK(Playerbot::Raid::Retaunts(kMc, kLucifron));        // the list before him is unchanged
    CHECK_FALSE(Playerbot::Raid::Retaunts(kMc, 11663));      // not the adds: they are held, not re-taunted off a DPS
}

// Dark Mending 19775 is the heal the Flamewaker Healers cast (sql/custom/world/2026_10_16_00_world_classic_flamewaker_healer_dark_mending): the
// kick election takes it before their Shadow Bolt, and gives it to a kicker that can kick now. In the three 2026-10-15 fights most kicks landed
// in the last 550 ms by the backups (rank 2+): a rank-0 kicker mid-Frostbolt cannot kick on sight.
TEST_CASE("Kick election: Dark Mending before every other cast, a free kicker before a busy one", "[ClassicRaidFightScript]")
{
    using namespace Playerbot::Raid;
    CHECK(InterruptOrder(kMc, 19775) == 0);
    CHECK(InterruptOrder(kMc, 19775) < InterruptOrder(kMc, 21077));          // the heal before the Healer's Shadow Bolt
    CHECK(InterruptOrder(kMc, 21077) < kNotInterruptFirst);
    CHECK(InterruptOrder(kMc, 20229) == kNotInterruptFirst);                 // Blast Wave: not listed
    CHECK(InterruptFirst(kMc, 19775));
    CHECK_FALSE(InterruptFirst(kMc, 20229));
    CHECK(InterruptOrder(1, 19775) == kNotInterruptFirst);                   // a map without rules

    Kicks::Cast const mending{ 0, 1500, 7 }, bolt{ 2, 400, 3 }, other{ kNotInterruptFirst, 350, 1 };
    CHECK(Kicks::CastBefore(mending, bolt));                                 // the list's order wins over the time left
    CHECK_FALSE(Kicks::CastBefore(bolt, mending));
    CHECK(Kicks::CastBefore(bolt, other));
    CHECK(Kicks::CastBefore(Kicks::Cast{ 2, 400, 9 }, Kicks::Cast{ 2, 900, 1 }));   // same order: the one about to land first
    CHECK(Kicks::CastBefore(Kicks::Cast{ 2, 400, 1 }, Kicks::Cast{ 2, 400, 9 }));   // then the lowest guid

    Kicks::Kicker const freeFar{ false, 20.f, 5 }, busyNear{ true, 3.f, 2 }, freeNear{ false, 3.f, 9 };
    CHECK(Kicks::KickerBefore(freeFar, busyNear));                           // a mage mid-Frostbolt cannot kick on sight
    CHECK_FALSE(Kicks::KickerBefore(busyNear, freeFar));
    CHECK(Kicks::KickerBefore(freeNear, freeFar));                           // both free: the nearest
    CHECK(Kicks::KickerBefore(Kicks::Kicker{ false, 3.f, 2 }, freeNear));    // same distance: the lowest guid
}
