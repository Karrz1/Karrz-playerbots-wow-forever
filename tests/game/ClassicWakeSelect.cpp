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

#include "../../src/modules/PlayerbotV2/Fleet/WakeSelect.h"

using namespace Playerbot::V2::Fleet;

namespace
{
constexpr uint32_t STORMWIND = 1519, ELWYNN = 12;

WakeCandidate C(uint64_t guid, float dist, uint32_t zone, bool faction_ok = true)
{
    WakeCandidate c;
    c.guid = guid; c.dist = dist; c.zone = zone; c.faction_ok = faction_ok;
    return c;
}
}

TEST_CASE("WakeSelect distance window and faction", "[wake]")
{
    WakeRule r;   // not a capital
    CHECK_FALSE(WakeEligible(C(1, 59.9f, ELWYNN), r));      // beside the player: would pop into view
    CHECK(WakeEligible(C(1, 60.0f, ELWYNN), r));
    CHECK(WakeEligible(C(1, 450.0f, ELWYNN), r));
    CHECK_FALSE(WakeEligible(C(1, 450.1f, ELWYNN), r));
    CHECK_FALSE(WakeEligible(C(1, 200.0f, ELWYNN, false), r));
}

TEST_CASE("WakeSelect in a capital takes only the capital's own zone", "[wake]")
{
    WakeRule r;
    r.capital = true; r.anchor_zone = STORMWIND;
    std::vector<WakeCandidate> in = { C(1, 100, STORMWIND), C(2, 120, ELWYNN), C(3, 300, STORMWIND), C(4, 90, ELWYNN) };
    auto const out = SelectWake(in, r, 10);
    REQUIRE(out.size() == 2);
    CHECK(out[0] == 1);
    CHECK(out[1] == 3);

    // outside a capital the zone does not matter
    r.capital = false;
    CHECK(SelectWake(in, r, 10).size() == 4);
}

TEST_CASE("WakeSelect never wakes a sleeper that lies in water", "[wake]")
{
    WakeRule r;
    WakeCandidate wet = C(1, 100, STORMWIND);
    wet.wet = true;                       // logged out in a canal on an earlier visit
    CHECK_FALSE(WakeEligible(wet, r));
    CHECK(SelectWake({ wet, C(2, 100, STORMWIND) }, r, 5) == std::vector<uint64_t>{ 2 });
}

TEST_CASE("WakeSelect never wakes more than wanted", "[wake]")
{
    WakeRule r;
    std::vector<WakeCandidate> in;
    for (uint64_t i = 1; i <= 50; ++i) in.push_back(C(i, 100, ELWYNN));
    CHECK(SelectWake(in, r, 0).empty());
    CHECK(SelectWake(in, r, 7).size() == 7);
    CHECK(SelectWake(in, r, 500).size() == 50);
}
