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

#include "../../src/modules/PlayerbotV2/Bot/GroupControl.h"

#include <cstdint>

namespace
{
struct FakeGuid
{
    uint64_t v = 0;
    bool operator==(FakeGuid const& o) const { return v == o.v; }
    bool IsEmpty() const { return v == 0; }
};
using Member = Playerbot::GroupControl::Member<FakeGuid>;
}

TEST_CASE("GroupControl: the leader controls when he is a player", "[groupcontrol]")
{
    std::vector<Member> others = { { { 5 }, false }, { { 7 }, true }, { { 9 }, true } };
    CHECK(Playerbot::GroupControl::ResolveController<FakeGuid>({ 9 }, others).v == 9);
}

TEST_CASE("GroupControl: a bot leader hands control to the first player", "[groupcontrol]")
{
    std::vector<Member> others = { { { 5 }, false }, { { 7 }, true }, { { 9 }, true } };
    CHECK(Playerbot::GroupControl::ResolveController<FakeGuid>({ 5 }, others).v == 7);
    // The bot itself leads (it is not in `others`).
    CHECK(Playerbot::GroupControl::ResolveController<FakeGuid>({ 1 }, others).v == 7);
}

TEST_CASE("GroupControl: a group of bots has no controller", "[groupcontrol]")
{
    std::vector<Member> others = { { { 5 }, false }, { { 6 }, false } };
    CHECK(Playerbot::GroupControl::ResolveController<FakeGuid>({ 5 }, others).IsEmpty());
    CHECK(Playerbot::GroupControl::ResolveController<FakeGuid>({ 5 }, {}).IsEmpty());
}

TEST_CASE("GroupControl: a passive bot defends within the leash only", "[groupcontrol]")
{
    using namespace Playerbot::GroupControl;
    CHECK(PassiveMayDefend(false, 24.9f, 25.0f));
    CHECK(PassiveMayDefend(false, 25.0f, 25.0f));
    CHECK_FALSE(PassiveMayDefend(false, 25.1f, 25.0f));
    CHECK(PassiveMayDefend(true, 90.0f, 25.0f));    // a mob on the bot itself is always fought
    CHECK(PassiveMayDefend(false, 90.0f, 0.0f));    // leash off
}

TEST_CASE("GroupControl: aggressive attacks inside the radius only", "[groupcontrol]")
{
    using namespace Playerbot::GroupControl;
    CHECK(AggressiveMayAttack(10.0f, 10.0f));
    CHECK_FALSE(AggressiveMayAttack(10.5f, 10.0f));
}

TEST_CASE("GroupControl: a companion is released when its owner removes it from the group", "[groupcontrol]")
{
    using namespace Playerbot::GroupControl;
    // method: 0 default, 1 kick, 2 leave, 3 LFG kick
    CHECK(ReleaseCompanionOnRemoval(1, false, true, false));        // the owner's uninvite
    CHECK_FALSE(ReleaseCompanionOnRemoval(1, false, false, false)); // a kick by someone else
    CHECK(ReleaseCompanionOnRemoval(2, false, false, false));       // it leaves on its own
    CHECK_FALSE(ReleaseCompanionOnRemoval(0, false, true, false));  // an internal removal (a regroup, a convert's rebuild)
    CHECK_FALSE(ReleaseCompanionOnRemoval(3, false, true, false));  // an LFG kick
    CHECK_FALSE(ReleaseCompanionOnRemoval(1, true, true, false));   // a battleground group
    CHECK_FALSE(ReleaseCompanionOnRemoval(2, false, false, true));  // the bot is logging out (the owner going offline, a map change)
}
