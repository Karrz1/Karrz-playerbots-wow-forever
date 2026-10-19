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

#include "../../src/modules/PlayerbotV2/LfgRoleGap.h"

#include <vector>

using namespace Playerbot::V2;

namespace {
LfgRoleGap Gap(std::vector<uint8> const& roles) { return ComputeLfgRoleGap(roles); }
}

TEST_CASE("LfgRoleGap: an empty group lacks the whole party", "[lfggap]")
{
    CHECK(Gap({}) == LfgRoleGap{ 1, 1, 3 });
}

TEST_CASE("LfgRoleGap: a lone damage dealer needs a tank, a healer and two more damage", "[lfggap]")
{
    CHECK(Gap({ kLfgRoleDamage }) == LfgRoleGap{ 1, 1, 2 });
    CHECK(Gap({ 0 }) == LfgRoleGap{ 1, 1, 2 });         // no role known counts as damage
}

TEST_CASE("LfgRoleGap: real players take their own role first", "[lfggap]")
{
    CHECK(Gap({ kLfgRoleTank, kLfgRoleHealer }) == LfgRoleGap{ 0, 0, 3 });
    CHECK(Gap({ kLfgRoleTank, kLfgRoleHealer, kLfgRoleDamage, kLfgRoleDamage, kLfgRoleDamage }) == LfgRoleGap{ 0, 0, 0 });
}

TEST_CASE("LfgRoleGap: a second tank takes a free damage place", "[lfggap]")
{
    CHECK(Gap({ kLfgRoleTank, kLfgRoleTank }) == LfgRoleGap{ 0, 1, 2 });
}

TEST_CASE("LfgRoleGap: a player who ticked every role takes the first free one", "[lfggap]")
{
    uint8 const all = kLfgRoleTank | kLfgRoleHealer | kLfgRoleDamage;
    CHECK(Gap({ all }) == LfgRoleGap{ 0, 1, 3 });
    CHECK(Gap({ all, all }) == LfgRoleGap{ 0, 0, 3 });
}

TEST_CASE("LfgRoleGap: a full group needs no bots whatever the roles", "[lfggap]")
{
    CHECK(Gap({ kLfgRoleDamage, kLfgRoleDamage, kLfgRoleDamage, kLfgRoleDamage, kLfgRoleDamage, kLfgRoleDamage }) == LfgRoleGap{ 0, 0, 0 });
    CHECK(Gap({ 0, 0, 0, 0 }).Total() == 1);          // four damage dealers: the last place is open to any role
}

TEST_CASE("LfgRoleGap: the role mix of a 5-man party and a 10-man group", "[lfggap]")
{
    CHECK(LfgRoleMix(5) == LfgRoleGap{ 1, 1, 3 });
    CHECK(LfgRoleMix(10) == LfgRoleGap{ 1, 3, 6 });
    CHECK(LfgRoleMix(10).Total() == 10);
    CHECK(LfgRoleMix(3) == LfgRoleGap{ 1, 1, 3 });          // below a party counts as a party
    CHECK(LfgRoleMix(20) == LfgRoleGap{ 2, 6, 12 });
    CHECK(LfgRoleMix(40) == LfgRoleGap{ 4, 13, 23 });
}

TEST_CASE("LfgRoleGap: a 10-man group (Upper Blackrock Spire) lacks 1 tank, 3 healers, 6 damage when empty", "[lfggap]")
{
    CHECK(ComputeLfgRoleGap({}, 10) == LfgRoleGap{ 1, 3, 6 });
    CHECK(ComputeLfgRoleGap(std::vector<uint8>{ kLfgRoleDamage }, 10) == LfgRoleGap{ 1, 3, 5 });
}

TEST_CASE("LfgRoleGap: real players in a 10-man group take their roles first", "[lfggap]")
{
    std::vector<uint8> const tankAndHealers{ kLfgRoleTank, kLfgRoleHealer, kLfgRoleHealer };
    CHECK(ComputeLfgRoleGap(tankAndHealers, 10) == LfgRoleGap{ 0, 1, 6 });
    std::vector<uint8> const two{ kLfgRoleDamage, kLfgRoleDamage };
    CHECK(ComputeLfgRoleGap(two, 10) == LfgRoleGap{ 1, 3, 4 });
}

TEST_CASE("LfgRoleGap: a full 10-man group needs no bots, an 11th member takes no place", "[lfggap]")
{
    std::vector<uint8> const ten(10, kLfgRoleDamage);
    CHECK(ComputeLfgRoleGap(ten, 10).Total() == 0);
    std::vector<uint8> const eleven(11, kLfgRoleDamage);
    CHECK(ComputeLfgRoleGap(eleven, 10).Total() == 0);
}

TEST_CASE("LfgRoleGap: the 5-man default is unchanged by the size parameter", "[lfggap]")
{
    CHECK(ComputeLfgRoleGap({}, 5) == ComputeLfgRoleGap({}));
    std::vector<uint8> const six(6, kLfgRoleDamage);
    CHECK(ComputeLfgRoleGap(six, 5).Total() == 0);
}

TEST_CASE("LfgFill: nobody in range fills at once, whatever time is left", "[lfgfill]")
{
    CHECK(DecideLfgFill(180000, 0, true) == LfgFillAction::FillNow);
    CHECK(DecideLfgFill(0, 0, true) == LfgFillAction::FillNow);
    CHECK(DecideLfgFill(-5000, 0, true) == LfgFillAction::FillNow);
}

TEST_CASE("LfgFill: with real players in range the bots wait for the full time", "[lfgfill]")
{
    CHECK(DecideLfgFill(180000, 1, true) == LfgFillAction::Wait);
    CHECK(DecideLfgFill(1, 7, true) == LfgFillAction::Wait);
    CHECK(DecideLfgFill(0, 3, true) == LfgFillAction::FillAfterWait);
    CHECK(DecideLfgFill(-1000, 3, true) == LfgFillAction::FillAfterWait);
}

TEST_CASE("LfgFill: with InstantFillIfNobody off an empty server still waits", "[lfgfill]")
{
    CHECK(DecideLfgFill(60000, 0, false) == LfgFillAction::Wait);
    CHECK(DecideLfgFill(0, 0, false) == LfgFillAction::FillAfterWait);
    CHECK(DecideLfgFill(60000, 4, false) == LfgFillAction::Wait);
}
