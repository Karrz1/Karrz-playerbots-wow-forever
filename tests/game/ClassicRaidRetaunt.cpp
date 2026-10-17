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

#include "../../src/modules/PlayerbotV2/Bot/Dungeon/TankMarkSwap.h"
#include "../../src/modules/PlayerbotV2/Bot/Raid/RaidEncounters.h"
#include "../../src/modules/PlayerbotV2/Bot/Raid/RaidFightScript.h"

using namespace Playerbot;

// Majordomo 2026-10-17: "Mctankwd taunts Majordomo back", and the next plan sent Mctankwa back onto him ("why=marker 0").
TEST_CASE("A retaunt swaps the two tanks' automatic markers", "[ClassicRaidRetaunt]")
{
    std::unordered_map<uint64_t, uint8_t> marks{ { 1, 0 }, { 2, 1 }, { 3, 2 } };   // 1 holds the boss (icon 0), 4 holds nothing
    CHECK(SwapTankMarks(marks, 4, 1));
    CHECK(marks.at(4) == 0);
    CHECK(marks.count(1) == 0);
    CHECK(marks.at(2) == 1);

    CHECK(SwapTankMarks(marks, 4, 2));
    CHECK(marks.at(4) == 1);
    CHECK(marks.at(2) == 0);
}

TEST_CASE("Swapping markers changes nothing without a marker or with one tank", "[ClassicRaidRetaunt]")
{
    std::unordered_map<uint64_t, uint8_t> marks{ { 1, 0 } };
    CHECK_FALSE(SwapTankMarks(marks, 2, 3));
    CHECK_FALSE(SwapTankMarks(marks, 1, 1));
    CHECK(marks.size() == 1);
    CHECK(marks.at(1) == 0);
}

TEST_CASE("Majordomo is the only scripted boss nobody waits on", "[ClassicRaidRetaunt]")
{
    using namespace Playerbot::Raid::Script;
    for (Fight const& f : kFights)
    {
        bool const expected = f.boss == 12018;
        CHECK(BossIsNoKillTarget(f, f.boss) == expected);
    }
    Fight const* maj = FightFor(409, 12018);
    REQUIRE(maj != nullptr);
    CHECK_FALSE(BossIsNoKillTarget(*maj, 11663));   // a Healer is a kill target
}

// Majordomo 2026-10-17: the ranged stopped at the edge of the Healers' circle and the circle moved with the mob (1.4 to 2.4 leave-hazard moves a second).
TEST_CASE("Majordomo's Healer and Elite circles are left with a margin", "[ClassicRaidRetaunt]")
{
    using namespace Playerbot::Raid;
    RaidRules const* mc = RulesForMap(409);
    REQUIRE(mc != nullptr);
    int healerRules = 0;
    for (AvoidEnemy const& a : mc->avoidEnemy)
        if (a.enemy == 11663 || a.enemy == 11664)
        {
            CHECK(a.pad > 0.0f);
            ++healerRules;
        }
        else
            CHECK(a.pad == 0.0f);   // every other avoid rule is unchanged
    CHECK(healerRules == 4);
}

// mc_majordomo 1023: the tanks held two Healers from the pull, so the election never sheeped them (one Polymorph in 116 s).
TEST_CASE("Majordomo's tanks take a Healer last", "[ClassicRaidRetaunt]")
{
    using namespace Playerbot::Raid;
    CHECK(TankLast(409, 11663));
    CHECK_FALSE(TankLast(409, 11664));     // the Elites are the tanks' mobs
    CHECK_FALSE(TankLast(409, 12018));     // and a tank holds Majordomo from the start
    CHECK(TankOrder(409, 12018) < TankOrder(409, 11664));
}
