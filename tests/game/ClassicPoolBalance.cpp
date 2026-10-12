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

#include "../../src/modules/PlayerbotV2/Fleet/PoolBalance.h"

#include <cstdlib>
#include <set>

using namespace Playerbot::V2::Fleet;

TEST_CASE("PoolBalance: levels map to five brackets", "[poolbalance]")
{
    CHECK(PoolBracketOf(10) == 0);
    CHECK(PoolBracketOf(19) == 0);
    CHECK(PoolBracketOf(20) == 1);
    CHECK(PoolBracketOf(49) == 3);
    CHECK(PoolBracketOf(50) == 4);
    CHECK(PoolBracketOf(60) == 4);
    CHECK(PoolBracketOf(5) == 0);
    CHECK(PoolBracketLo(4) == 50);
    CHECK(PoolBracketHi(4) == 60);
    CHECK(PoolBracketHi(1) == 29);
}

namespace
{
// Deterministic stand-in for urand: a fixed-stride walk through [0,1).
struct Rolls { double v = 0.0; double next() { v = std::fmod(v + 0.6180339887498949, 1.0); return v; } };
}

TEST_CASE("PoolBalance: only the Horde side of a lopsided pool is created, in proportion to what each bracket lacks", "[poolbalance]")
{
    PoolCounts c;
    c.alliance = { 386, 376, 375, 416, 594 };       // the live census of 69b0d8c2
    c.horde    = { 0, 0, 0, 0, 76 };
    Rolls r;
    std::array<int, kPoolBrackets> made{};
    for (int i = 0; i < 100; ++i)
    {
        SeedChoice const s = PickSeed(c, 50, r.next());
        REQUIRE_FALSE(s.alliance);
        made[s.bracket]++;
        c.horde[s.bracket]++;
    }
    for (std::size_t b = 0; b < 4; ++b)
        CHECK(made[b] >= 15);                        // every empty bracket gets Horde at once, not after the 50-60s are done
    CHECK(made[4] >= 10);
}

TEST_CASE("PoolBalance: a lopsided pool is brought to the ratio in every bracket, then grows evenly", "[poolbalance]")
{
    PoolCounts c;
    c.alliance = { 470, 480, 500, 490, 450 };
    c.horde    = { 20, 15, 10, 30, 5 };
    Rolls r;
    std::uint32_t horde_made = 0;
    for (int i = 0; i < 6000; ++i)
    {
        SeedChoice const s = PickSeed(c, 50, r.next());
        if (s.deficit <= 0) break;
        (s.alliance ? c.alliance : c.horde)[s.bracket]++;
        if (!s.alliance) ++horde_made;
    }
    for (std::size_t b = 0; b < kPoolBrackets; ++b)
        CHECK(std::abs(int(c.alliance[b]) - int(c.horde[b])) <= 2);
    CHECK(horde_made > 2000);
}

TEST_CASE("PoolBalance: the target ratio is honoured", "[poolbalance]")
{
    PoolCounts c;
    c.alliance = { 100, 100, 100, 100, 100 };
    Rolls r;
    for (int i = 0; i < 2000; ++i)
    {
        SeedChoice const s = PickSeed(c, 40, r.next());
        if (s.deficit <= 0) break;
        (s.alliance ? c.alliance : c.horde)[s.bracket]++;
    }
    for (std::size_t b = 0; b < kPoolBrackets; ++b)
        CHECK(std::abs(double(c.horde[b]) / double(c.horde[b] + c.alliance[b]) - 0.40) < 0.02);
}

TEST_CASE("PoolBalance: a balanced pool reports nothing lacking", "[poolbalance]")
{
    PoolCounts c;
    c.alliance = { 10, 10, 10, 10, 10 };
    c.horde = c.alliance;
    CHECK(PickSeed(c, 50, 0.5).deficit <= 0);
    PoolAdd(c, false, 33);
    CHECK(c.horde[2] == 11);
}
