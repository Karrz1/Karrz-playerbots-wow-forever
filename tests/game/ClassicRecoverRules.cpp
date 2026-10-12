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

#include "../../src/modules/PlayerbotV2/Bot/RecoverRules.h"

using namespace Playerbot::Classic;

// Deadmines, live 2026-10-10: the priest at 87 % mana never sat because some mob with a victim was always within 45 y.
TEST_CASE("An enemy only blocks sitting down when it is engaged with the bot's side and close", "[ClassicRecoverRules]")
{
    CHECK_FALSE(EnemyBlocksRecovery(false, 10.0f * 10.0f));   // fighting someone else (a pet, another pack)
    CHECK_FALSE(EnemyBlocksRecovery(true, 35.0f * 35.0f));    // engaged but out of reach
    CHECK(EnemyBlocksRecovery(true, 25.0f * 25.0f));
    CHECK(MemberFightBlocksRecovery(40.0f * 40.0f));
    CHECK_FALSE(MemberFightBlocksRecovery(50.0f * 50.0f));
}
