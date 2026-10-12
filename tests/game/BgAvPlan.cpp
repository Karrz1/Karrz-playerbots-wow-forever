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

#include "../../src/modules/PlayerbotV2/Bot/Battleground/BgAvPlan.h"

using namespace Playerbot::BgAvPlan;

TEST_CASE("Alterac Valley: the captain squad goes in at five bots, and stays going", "[battleground][av]")
{
    CHECK(!CaptainGather(0, false));
    CHECK(!CaptainGather(4, false));
    CHECK(CaptainGather(5, false));
    CHECK(CaptainGather(0, true));   // the latch: the squad leaving the waiting spot does not send it back
    // the time cap: whoever is there goes after 75 s, but not nobody
    CHECK(!CaptainGather(2, false, 74000));
    CHECK(CaptainGather(1, false, 75000));
    CHECK(!CaptainGather(0, false, 200000));
}

TEST_CASE("Alterac Valley: mine bosses of the mines the team does not own", "[battleground][av]")
{
    // both mines neutral at the start: each team goes for Morloch and Taskmaster Snivvle
    for (uint8_t team : { 1, 2 })
    {
        auto t = MineTargets(team, 0, 0);
        REQUIRE(t.size() == 2);
        CHECK(t[0].entry == 11657u);
        CHECK(t[1].entry == 11677u);
    }
    // an owned mine is left alone
    CHECK(MineTargets(1, 1, 1).empty());
    CHECK(MineTargets(2, 2, 2).empty());
    // an enemy-held mine: the other team's boss (Alliance hunts Keetar / Masha, Horde hunts Umi Thorson / Agi Rumblestomp)
    auto a = MineTargets(1, 2, 2);
    REQUIRE(a.size() == 2);
    CHECK(a[0].entry == 13079u);
    CHECK(a[1].entry == 13088u);
    auto h = MineTargets(2, 1, 1);
    REQUIRE(h.size() == 2);
    CHECK(h[0].entry == 13078u);
    CHECK(h[1].entry == 13086u);
    // one of each
    auto m = MineTargets(2, 2, 0);
    REQUIRE(m.size() == 1);
    CHECK(m[0].entry == 11677u);
    // not read / not a team: nothing
    CHECK(MineTargets(1, 255, 255).empty());
    CHECK(MineTargets(0, 0, 0).empty());
    // the positions are the two mines (north near x 860, south near x -850)
    auto n = MineTargets(1, 0, 0);
    CHECK(n[0].x > 800.0f);
    CHECK(n[1].x < -800.0f);
}

TEST_CASE("Alterac Valley quests: the quester's next step", "[battleground][av]")
{
    QuestInput in;
    in.team = 1;
    // fresh: take the Armor Scraps quest from the armorer (Murgot Deepforge)
    AvStep s = NextStep(in);
    CHECK(s.kind == StepKind::Accept);
    CHECK(s.quest_id == 7223u);
    CHECK(s.place.entry == 13257u);
    // taken and not done: back to the fight (the scraps drop from kills)
    in.scraps_first = AvQStatus::Incomplete;
    CHECK(NextStep(in).kind == StepKind::None);
    // complete: hand in
    in.scraps_first = AvQStatus::Complete;
    s = NextStep(in);
    CHECK(s.kind == StepKind::TurnIn);
    CHECK(s.quest_id == 7223u);
    // handed in once: the repeat quest, then the same cycle
    in.scraps_first = AvQStatus::Rewarded;
    s = NextStep(in);
    CHECK(s.kind == StepKind::Accept);
    CHECK(s.quest_id == 6781u);
    in.scraps_repeat = AvQStatus::Complete;
    CHECK(NextStep(in).kind == StepKind::TurnIn);
    CHECK(NextStep(in).quest_id == 6781u);
    in.scraps_repeat = AvQStatus::Incomplete;
    CHECK(NextStep(in).kind == StepKind::None);

    // mines: nothing to do for a mine the team does not hold
    in.mine_owner_north = 0; in.mine_owner_south = 2;
    CHECK(NextStep(in).kind == StepKind::None);
    // the team takes Irondeep: accept the Irondeep supplies quest from the Stormpike quartermaster
    in.mine_owner_north = 1;
    s = NextStep(in);
    CHECK(s.kind == StepKind::Accept);
    CHECK(s.quest_id == 5892u);
    CHECK(s.place.entry == 12096u);
    // taken, the bags are short: fetch from the mine object
    in.mine_north = AvQStatus::Incomplete;
    s = NextStep(in);
    CHECK(s.kind == StepKind::UseMineGo);
    CHECK(s.place.entry == kMineGoNorth);
    // enough supplies (the quest completes itself): hand in at the quartermaster
    in.supplies_north = 10;
    CHECK(NextStep(in).kind == StepKind::None);
    in.mine_north = AvQStatus::Complete;
    s = NextStep(in);
    CHECK(s.kind == StepKind::TurnIn);
    CHECK(s.quest_id == 5892u);
    CHECK(s.place.entry == 12096u);
    // the mine is lost mid-quest: no more fetching
    in.mine_north = AvQStatus::Incomplete; in.supplies_north = 0; in.mine_owner_north = 2;
    CHECK(NextStep(in).kind == StepKind::None);
}

TEST_CASE("Alterac Valley quests: Horde ids and places", "[battleground][av]")
{
    QuestInput in;
    in.team = 2;
    AvStep s = NextStep(in);
    CHECK(s.quest_id == 7224u);
    CHECK(s.place.entry == 13176u);
    in.scraps_first = AvQStatus::Incomplete;
    // the Horde's own mine is Coldtooth (5893), Irondeep is the other one (6985)
    in.mine_owner_south = 2;
    s = NextStep(in);
    CHECK(s.kind == StepKind::Accept);
    CHECK(s.quest_id == 5893u);
    CHECK(s.place.entry == 12097u);
    in.mine_south = AvQStatus::Incomplete;
    s = NextStep(in);
    CHECK(s.kind == StepKind::UseMineGo);
    CHECK(s.place.entry == kMineGoSouth);
    in.mine_south = AvQStatus::Rewarded;   // not a state a repeatable mine quest keeps, but nothing may loop on it
    in.mine_owner_north = 2;
    s = NextStep(in);
    CHECK(s.quest_id == 6985u);
    // every quest id the planner uses has a bit in the rewarded mask
    for (uint8_t team : { 1, 2 })
    {
        TeamQuests const& q = QuestsFor(team);
        for (uint32_t id : { q.scraps_first, q.scraps_repeat, q.mine_north, q.mine_south })
            CHECK(QuestBit(id) != 0u);
    }
    // one in eight bots is a quester
    int n = 0;
    for (uint64_t g = 1; g <= 400; ++g) n += IsQuester(g) ? 1 : 0;
    CHECK(n == 50);
}

TEST_CASE("Alterac Valley: the attack list goes node by node, graveyards included", "[battleground][av]")
{
    // live nodes in the Horde attack order: Stonehearth GY, Stonehearth Bunker, Icewing, Stormpike GY
    std::vector<AvNodeView> nodes = {
        { 73.7f, -426.0f, false, false, false },     // Stonehearth GY (Alliance)
        { -154.0f, -446.0f, false, false, false },   // Stonehearth Bunker
        { 203.0f, -361.0f, false, false, false },    // Icewing
        { 669.0f, -294.0f, false, false, false },    // Stormpike GY
    };
    auto p = PickAttackNodes(2, nodes, 2);
    REQUIRE(p.size() == 2);
    CHECK(p[0] == 0);
    CHECK(p[1] == 1);
    nodes[0].ours = true;                             // taken: the window slides on
    p = PickAttackNodes(2, nodes, 2);
    REQUIRE(p.size() == 2);
    CHECK(p[0] == 1);
    CHECK(p[1] == 2);
    nodes[1].destroyed = true;                        // razed bunker is skipped
    nodes[2].enemy_flipping = true;                   // the defence pass owns this one
    p = PickAttackNodes(2, nodes, 2);
    REQUIRE(p.size() == 1);
    CHECK(p[0] == 3);
    // the Alliance list starts at Iceblood, not Stonehearth
    CHECK(PickAttackNodes(1, nodes, 2).empty());
    std::vector<AvNodeView> horde_nodes = { { -612.5f, -397.0f, false, false, false } };
    auto a = PickAttackNodes(1, horde_nodes, 2);
    REQUIRE(a.size() == 1);
}

TEST_CASE("Alterac Valley: Snowfall gets 40 percent of the free bots early and 20 percent later", "[battleground][av]")
{
    CHECK(SnowfallDetail(2, 0) == 0);
    CHECK(SnowfallDetail(20, 60000) == 8);
    CHECK(SnowfallDetail(20, 11u * 60u * 1000u) == 4);
    CHECK(SnowfallDetail(3, 11u * 60u * 1000u) == 1);
}

TEST_CASE("Alterac Valley: ike3 and mod-playerbots rules (defender flip, general push, boss wait, aggro range)", "[battleground][av]")
{
    CHECK(DefenderPastLine(2, -61.f));
    CHECK(!DefenderPastLine(2, -400.f));
    CHECK(DefenderPastLine(1, -500.f));
    CHECK(!DefenderPastLine(1, 100.f));
    CHECK(DefenderFlipChance(0) == 20);
    CHECK(DefenderFlipChance(3) == 65);
    CHECK(GeneralPushOk(0, 4, false, 0));            // every tower down: go
    CHECK(!GeneralPushOk(2, 1, true, 30));           // only one razed
    CHECK(!GeneralPushOk(2, 2, false, 19));          // two razed, no final graveyard, 19 near: wait
    CHECK(GeneralPushOk(2, 2, true, 0));             // two razed and the last graveyard is ours
    CHECK(GeneralPushOk(3, 2, false, 20));           // two razed and 20 near the boss
    CHECK(BossWait(2).x == 689.21f);
    CHECK(FinalGraveyard(1).x == 638.5f);
    CHECK(AggroRange(0) == 40.0f);
    CHECK(AggroRange(21u * 60u * 1000u) == 10.0f);
}
