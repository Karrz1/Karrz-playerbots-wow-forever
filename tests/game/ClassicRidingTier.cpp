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

#include "../../src/modules/PlayerbotV2/Bot/World/RidingTier.h"

using namespace Playerbot::V2::World;

TEST_CASE("Riding skill by level: none below 40, 75 from 40, 150 from 60", "[ClassicRidingTier]")
{
    CHECK(RidingSkillForLevel(1) == 0);
    CHECK(RidingSkillForLevel(29) == 0);
    CHECK(RidingSkillForLevel(39) == 0);
    CHECK(RidingSkillForLevel(40) == 75);
    CHECK(RidingSkillForLevel(59) == 75);
    CHECK(RidingSkillForLevel(60) == 150);
}

TEST_CASE("A bot under level 40 loses its riding skill and its mounts", "[ClassicRidingTier]")
{
    RidingFix const had150 = PlanRiding(29, 150);       // Radoslaw: level 29, riding 150/150
    CHECK(had150.skill == 0);
    CHECK(had150.set_skill);
    CHECK(had150.strip_mounts);
    RidingFix const had75 = PlanRiding(15, 75);
    CHECK(had75.set_skill);
    CHECK(had75.strip_mounts);
    RidingFix const none = PlanRiding(15, 0);            // nothing to change
    CHECK_FALSE(none.set_skill);
    CHECK(none.strip_mounts);                            // still no mounts below 40
}

TEST_CASE("A bot at level 40-59 goes from 150 to 75 and keeps its mounts", "[ClassicRidingTier]")
{
    RidingFix const fix = PlanRiding(45, 150);
    CHECK(fix.skill == 75);
    CHECK(fix.set_skill);
    CHECK_FALSE(fix.strip_mounts);
    CHECK_FALSE(PlanRiding(45, 75).set_skill);
    CHECK(PlanRiding(40, 0).skill == 75);                // a new 40 gets the skill
    CHECK(PlanRiding(40, 0).set_skill);
    CHECK_FALSE(PlanRiding(60, 150).set_skill);
}
