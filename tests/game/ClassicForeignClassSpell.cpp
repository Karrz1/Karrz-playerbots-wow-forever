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

#include "../../src/modules/PlayerbotV2/Api/ForeignClassSpell.h"

using Playerbot::Api::IsForeignClassSpell;

namespace
{
constexpr uint32_t Mask(unsigned cls) { return 1u << (cls - 1); }
constexpr unsigned kPaladin = 2, kRogue = 4, kShaman = 7;
}

TEST_CASE("ForeignClassSpell: a rogue levelling up keeps no paladin spell", "[foreignclassspell]")
{
    uint32_t const judgement = Mask(kPaladin);                 // only paladin trainers teach it
    CHECK(IsForeignClassSpell(judgement, Mask(kRogue)));
    CHECK_FALSE(IsForeignClassSpell(judgement, Mask(kPaladin)));
}

TEST_CASE("ForeignClassSpell: a spell several classes' trainers teach stays with each of them", "[foreignclassspell]")
{
    uint32_t const shared = Mask(kPaladin) | Mask(kShaman);
    CHECK_FALSE(IsForeignClassSpell(shared, Mask(kShaman)));
    CHECK(IsForeignClassSpell(shared, Mask(kRogue)));
}

TEST_CASE("ForeignClassSpell: a spell no class trainer teaches is nobody's foreign spell", "[foreignclassspell]")
{
    CHECK_FALSE(IsForeignClassSpell(0, Mask(kRogue)));
}
