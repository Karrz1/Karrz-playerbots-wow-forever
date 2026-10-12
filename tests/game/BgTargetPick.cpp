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

#include "../../src/modules/PlayerbotV2/Bot/Battleground/BgTargetPick.h"

#include <vector>

using namespace Playerbot::BgTargetPick;

namespace
{
    struct G { uint64_t v = 0; bool operator==(G const& o) const { return v == o.v; } };
    struct U
    {
        G guid; int hp = 100; float x = 0.f, y = 0.f;
        bool untargetable = false, is_pacified = false, cannot_reach = false, is_player = false, is_cc_locked = false, in_los = true;
        G victim;
    };
}

TEST_CASE("BG victim validity: a gone, dead or idle victim is swapped for the nearest enemy player", "[battleground][target]")
{
    G const me{1};
    U player; player.guid = {2}; player.is_player = true; player.x = 5.f;
    U mob; mob.guid = {3}; mob.x = 3.f;
    std::vector<U> enemies, attackers;
    Why why;

    SECTION("a victim that is not around any more")
    {
        enemies = { player };
        CHECK(Pick<U, G>(me, G{9}, 0.f, 0.f, false, 40.f, enemies, attackers, why).v == 2);
        CHECK(why == Why::Missing);
        enemies.clear();
        CHECK(Pick<U, G>(me, G{9}, 0.f, 0.f, false, 40.f, enemies, attackers, why).v == 0);
        CHECK(why == Why::Missing);   // nobody to switch to: the caller releases the victim
    }
    SECTION("a dead victim")
    {
        U dead = mob; dead.hp = 0;
        enemies = { dead, player };
        CHECK(Pick<U, G>(me, G{3}, 0.f, 0.f, false, 40.f, enemies, attackers, why).v == 2);
        CHECK(why == Why::Unfightable);
    }
    SECTION("a creature that does not fight back while an enemy player is within 15 y")
    {
        enemies = { mob, player };
        CHECK(Pick<U, G>(me, G{3}, 0.f, 0.f, false, 40.f, enemies, attackers, why).v == 2);
        CHECK(why == Why::IdleMob);
        U fighting = mob; fighting.victim = me;
        enemies = { fighting, player };
        CHECK(Pick<U, G>(me, G{3}, 0.f, 0.f, false, 40.f, enemies, attackers, why).v == 0);
        CHECK(why == Why::None);
        U far = player; far.x = 30.f;
        enemies = { mob, far };
        CHECK(Pick<U, G>(me, G{3}, 0.f, 0.f, false, 40.f, enemies, attackers, why).v == 0);
        CHECK(why == Why::None);
    }
    SECTION("a healthy player victim, no victim, and players a ranged bot cannot see or that are crowd-controlled")
    {
        enemies = { player };
        CHECK(Pick<U, G>(me, G{2}, 0.f, 0.f, false, 40.f, enemies, attackers, why).v == 0);
        CHECK(why == Why::None);
        CHECK(Pick<U, G>(me, G{}, 0.f, 0.f, false, 40.f, enemies, attackers, why).v == 0);
        CHECK(why == Why::None);
        U hidden = player; hidden.in_los = false;
        enemies = { hidden };
        CHECK(Pick<U, G>(me, G{9}, 0.f, 0.f, true, 40.f, enemies, attackers, why).v == 0);
        U locked = player; locked.is_cc_locked = true;
        enemies = { locked };
        CHECK(Pick<U, G>(me, G{9}, 0.f, 0.f, false, 40.f, enemies, attackers, why).v == 0);
    }
}
