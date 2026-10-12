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

#include "../../src/modules/PlayerbotV2/Bot/CasterStepOut.h"

using Playerbot::StepOutFromMob;
using Playerbot::StepOutMob;

static StepOutMob Engaged()
{
    StepOutMob m;
    m.alive = true;
    m.targetable = true;
    m.in_combat = true;
    m.held_by_tank_or_member = true;
    return m;
}

TEST_CASE("A casting mob held by the tank is stepped out from", "[ClassicCasterStepOut]")
{
    StepOutMob m = Engaged();
    m.casting_now = true;
    CHECK(StepOutFromMob(m));
}

TEST_CASE("A script-listed caster is stepped out from while idle", "[ClassicCasterStepOut]")
{
    StepOutMob m = Engaged();
    m.listed_caster = true;
    CHECK(StepOutFromMob(m));
}

TEST_CASE("A plain melee mob is not stepped out from", "[ClassicCasterStepOut]")
{
    CHECK_FALSE(StepOutFromMob(Engaged()));
}

TEST_CASE("A mob on the bot itself is left to the tank", "[ClassicCasterStepOut]")
{
    StepOutMob m = Engaged();
    m.listed_caster = true;
    m.held_by_tank_or_member = false;
    CHECK_FALSE(StepOutFromMob(m));
}

TEST_CASE("Dead, untargetable, out-of-combat and player units never count", "[ClassicCasterStepOut]")
{
    StepOutMob base = Engaged();
    base.listed_caster = true;
    StepOutMob a = base; a.alive = false;
    StepOutMob b = base; b.targetable = false;
    StepOutMob c = base; c.in_combat = false;
    StepOutMob d = base; d.is_player = true;
    CHECK_FALSE(StepOutFromMob(a));
    CHECK_FALSE(StepOutFromMob(b));
    CHECK_FALSE(StepOutFromMob(c));
    CHECK_FALSE(StepOutFromMob(d));
}
