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

#include "../../src/modules/PlayerbotV2/Bot/GroupOrderText.h"

using namespace Playerbot::V2;

TEST_CASE("GroupOrder: exact orders are normalized", "[grouporder]")
{
    CHECK(ClassifyGroupOrder("follow", false) == "follow");
    CHECK(ClassifyGroupOrder("  Follow \t", false) == "follow");
    CHECK(ClassifyGroupOrder("assist", false) == "attack");
    CHECK(ClassifyGroupOrder("pullback", false) == "pull back");
    CHECK(ClassifyGroupOrder("Pull And Back", false) == "pull back");
}
TEST_CASE("GroupOrder: panel argument orders are recognized", "[grouporder]")
{
    CHECK(ClassifyGroupOrder("distance 8", false) == "distance 8");
    CHECK(ClassifyGroupOrder("assign skull 3", false) == "assign skull 3");
    CHECK(ClassifyGroupOrder("tank cross", false) == "tank cross");
    CHECK(ClassifyGroupOrder("formation circle", false) == "formation circle");
    CHECK(ClassifyGroupOrder("cc off", false) == "cc off");
}
TEST_CASE("GroupOrder: chatter and squad-prefix lines are not orders", "[grouporder]")
{
    CHECK_FALSE(ClassifyGroupOrder("", false));
    CHECK_FALSE(ClassifyGroupOrder("nice pull!", false));
    CHECK_FALSE(ClassifyGroupOrder("follow me please", false));
    CHECK_FALSE(ClassifyGroupOrder(";tank pull", false));
}
TEST_CASE("GroupOrder: owner words need the config switch", "[grouporder]")
{
    CHECK_FALSE(ClassifyGroupOrder("summon", false));
    CHECK(ClassifyGroupOrder("summon", true) == "summon");
    CHECK(ClassifyGroupOrder("drop quest Foo", true) == "drop quest foo");
    CHECK_FALSE(ClassifyGroupOrder("drop quest Foo", false));
}
TEST_CASE("GroupOrder: addon messages count only under the ForeverBots prefix", "[grouporder]")
{
    CHECK(kAddonOrderPrefix.size() <= 16);
    CHECK(ClassifyAddonOrder(kAddonOrderPrefix, "stay", false) == "stay");
    CHECK(ClassifyAddonOrder(kAddonOrderPrefix, "pull back", false) == "pull back");
    CHECK_FALSE(ClassifyAddonOrder("OTHER", "stay", false));
    CHECK_FALSE(ClassifyAddonOrder("", "stay", false));
    CHECK_FALSE(ClassifyAddonOrder(kAddonOrderPrefix, "hello there", false));
    CHECK_FALSE(ClassifyAddonOrder(kAddonOrderPrefix, ";follow", false));
}
