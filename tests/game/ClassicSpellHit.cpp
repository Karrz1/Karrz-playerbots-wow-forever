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

#include "../../src/server/game/Spells/ClassicSpellHit.h"

TEST_CASE("A spell hits 96 / 95 / 94 percent of the time against targets 0 / 1 / 2 levels above", "[ClassicSpellHit]")
{
    CHECK(ClassicSpellHit::BaseHitChance(0, false) == 96.0f);
    CHECK(ClassicSpellHit::BaseHitChance(1, false) == 95.0f);
    CHECK(ClassicSpellHit::BaseHitChance(2, false) == 94.0f);
}

TEST_CASE("Three levels above is a step to 83 percent, then 11 per level for creatures, 7 for players", "[ClassicSpellHit]")
{
    CHECK(ClassicSpellHit::BaseHitChance(3, false) == 83.0f);
    CHECK(ClassicSpellHit::BaseHitChance(4, false) == 72.0f);
    CHECK(ClassicSpellHit::BaseHitChance(3, true) == 87.0f);
    CHECK(ClassicSpellHit::BaseHitChance(4, true) == 80.0f);
}

TEST_CASE("The base chance never falls below 22 percent", "[ClassicSpellHit]")
{
    CHECK(ClassicSpellHit::BaseHitChance(9, false) == 22.0f);
    CHECK(ClassicSpellHit::BaseHitChance(59, true) == 22.0f);
}

TEST_CASE("Below the caster's level the chance rises and the total is held between 1 and 99 percent", "[ClassicSpellHit]")
{
    CHECK(ClassicSpellHit::BaseHitChance(-3, false) == 99.0f);
    CHECK(ClassicSpellHit::ClampHitChance(ClassicSpellHit::BaseHitChance(-10, false)) == 99.0f);
    CHECK(ClassicSpellHit::ClampHitChance(0.0f) == 1.0f);
    CHECK(ClassicSpellHit::ClampHitChance(87.5f) == 87.5f);
}
