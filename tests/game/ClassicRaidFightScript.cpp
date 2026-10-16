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

#include "../../src/modules/PlayerbotV2/Bot/Raid/RaidFightScript.h"

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
