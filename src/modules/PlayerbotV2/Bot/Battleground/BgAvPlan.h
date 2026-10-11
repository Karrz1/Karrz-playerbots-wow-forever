// Alterac Valley objective rules imported from ike3's bots (Karrz's earlier VMaNGOS bot tree (wow-1.12) raid10-revamp bcc8b9fe70c42a8fa5d50fa83a472da9e9d19546,
// BattleGroundTacticsAV.cpp): the mine-boss objective and the captain gathering threshold. Pure functions, no game state, so they can be
// tested offline (tests/game/BgAvPlan.cpp). The mid-field fighting tactics are ours and do not live here.
#pragma once

#include <cstdint>
#include <vector>

namespace Playerbot::BgAvPlan
{
    // The captain squad goes in once this many of the team stand at the waiting spot (ike3: attackers near the waiting spot >= 5).
    constexpr int kCaptainGatherCount = 5;
    constexpr bool CaptainGather(int at_stage, bool latched) { return latched || at_stage >= kCaptainGatherCount; }

    struct MineTarget { float x, y, z; uint32_t entry; char const* name; };

    // Mine owner world states (801 Irondeep = north, 804 Coldtooth = south): 0 neutral, 1 Alliance, 2 Horde, 255 unread.
    // `team` is 1 = Alliance, 2 = Horde. Killing the boss of a mine the team does not own takes it: a neutral mine is held by its trogg /
    // kobold boss (Morloch / Taskmaster Snivvle), an enemy-held one by that team's boss (Alliance Umi Thorson / Agi Rumblestomp, Horde
    // Keetar / Masha Swiftcut). Positions are world.creature spawns; both sides' bosses of a mine spawn at the same spot.
    inline std::vector<MineTarget> MineTargets(uint8_t team, uint8_t north, uint8_t south)
    {
        std::vector<MineTarget> out;
        if (team != 1 && team != 2) return out;
        if (north != 255 && north != team && north <= 2)
            out.push_back({ 863.838f, -445.468f, 50.846f, north == 0 ? 11657u : (team == 1 ? 13079u : 13078u), "Irondeep Mine boss" });
        if (south != 255 && south != team && south <= 2)
            out.push_back({ -848.902f, -92.931f, 68.633f, south == 0 ? 11677u : (team == 1 ? 13088u : 13086u), "Coldtooth Mine boss" });
        return out;
    }

    // ---- AV quests (ike3 BattleGroundTacticsAV.cpp: the AV quester role; quest data from the VMaNGOS import, NPC spawns from
    // world.creature) ----
    // A small share of the team (IsQuester) works the Armor Scraps quest and the two mine-supply quests instead of walking the front the
    // whole match. Turn in what is complete, accept what can be done, fetch mine supplies from a mine the team owns. Armor scraps drop
    // from kills (the quest collects them while the bot fights); riders, wing commanders, beacons and the world-boss turn-in are not
    // here.
    constexpr uint32_t kScrapsItem = 17422, kIrondeepSupplies = 17522, kColdtoothSupplies = 17542;
    constexpr uint32_t kSuppliesNeeded = 10;
    constexpr uint32_t kMineGoNorth = 178785, kMineGoSouth = 178784;

    // One bot in eight is a quester (ike3: guid % 5 for the 51-59 bracket).
    constexpr bool IsQuester(uint64_t guid_low) { return guid_low % 8u == 0; }

    struct AvPlace { uint32_t entry; float x, y, z; };
    struct TeamQuests
    {
        uint32_t scraps_first, scraps_repeat, mine_north, mine_south;   // quest ids
        AvPlace armorer, quartermaster;                                    // quest NPCs (give and take every quest above)
        AvPlace go_north, go_south;                                        // the mine supply objects (Irondeep N, Coldtooth S)
    };
    // Alliance: scraps 7223 then 6781 (Murgot Deepforge), Irondeep 5892 / Coldtooth 6982 (Stormpike Quartermaster). Horde: scraps 7224
    // then 6741 (Smith Regzar), Coldtooth 5893 / Irondeep 6985 (Frostwolf Quartermaster).
    inline TeamQuests const& QuestsFor(uint8_t team)
    {
        static TeamQuests const alliance = { 7223, 6781, 5892, 6982, { 13257, 647.61f, -61.1548f, 41.7405f }, { 12096, 587.633f, -45.9816f, 37.5438f },
                                             { kMineGoNorth, 881.273f, -442.002f, 54.664f }, { kMineGoSouth, -853.671f, -91.427f, 68.569f } };
        static TeamQuests const horde    = { 7224, 6741, 6985, 5893, { 13176, -1251.5f, -316.327f, 62.6565f }, { 12097, -1293.79f, -194.407f, 72.4398f },
                                             { kMineGoNorth, 881.273f, -442.002f, 54.664f }, { kMineGoSouth, -853.671f, -91.427f, 68.569f } };
        return team == 2 ? horde : alliance;
    }
    // Every quest id the planner looks at, for the snapshot's rewarded mask (bit i = kQuestIds[i] was handed in once).
    constexpr uint32_t kQuestIds[8] = { 7223, 6781, 5892, 6982, 7224, 6741, 5893, 6985 };
    constexpr uint32_t QuestBit(uint32_t quest_id)
    {
        for (uint32_t i = 0; i < 8; ++i) if (kQuestIds[i] == quest_id) return 1u << i;
        return 0;
    }

    enum class AvQStatus : uint8_t { None, Incomplete, Complete, Rewarded };
    struct QuestInput
    {
        uint8_t team = 1;                        // 1 Alliance, 2 Horde
        uint8_t mine_owner_north = 255, mine_owner_south = 255;
        AvQStatus scraps_first = AvQStatus::None, scraps_repeat = AvQStatus::None, mine_north = AvQStatus::None, mine_south = AvQStatus::None;
        uint32_t supplies_north = 0, supplies_south = 0;   // Irondeep / Coldtooth supply items in the bags
    };
    enum class StepKind : uint8_t { None, TurnIn, Accept, UseMineGo };
    struct AvStep { StepKind kind = StepKind::None; uint32_t quest_id = 0; AvPlace place{0, 0, 0, 0}; };

    // The next thing the quester does; None = back to the fight. Mine quests are only taken and worked while the team owns that mine
    // (a mine the enemy holds gives no supplies); the scraps quest is kept running all match (first quest once, then the repeat one).
    inline AvStep NextStep(QuestInput const& in)
    {
        TeamQuests const& q = QuestsFor(in.team);
        // turn in
        if (in.scraps_first == AvQStatus::Complete)  return { StepKind::TurnIn, q.scraps_first,  q.armorer };
        if (in.scraps_repeat == AvQStatus::Complete) return { StepKind::TurnIn, q.scraps_repeat, q.armorer };
        if (in.mine_north == AvQStatus::Complete)    return { StepKind::TurnIn, q.mine_north,    q.quartermaster };
        if (in.mine_south == AvQStatus::Complete)    return { StepKind::TurnIn, q.mine_south,    q.quartermaster };
        // accept
        if (in.scraps_first == AvQStatus::None)
            return { StepKind::Accept, q.scraps_first, q.armorer };
        if (in.scraps_first == AvQStatus::Rewarded && in.scraps_repeat == AvQStatus::None)
            return { StepKind::Accept, q.scraps_repeat, q.armorer };
        bool const own_n = in.mine_owner_north == in.team, own_s = in.mine_owner_south == in.team;
        if (own_n && in.mine_north == AvQStatus::None) return { StepKind::Accept, q.mine_north, q.quartermaster };
        if (own_s && in.mine_south == AvQStatus::None) return { StepKind::Accept, q.mine_south, q.quartermaster };
        // fetch supplies: the quest is open, the bags are short, the mine is ours
        if (own_n && in.mine_north == AvQStatus::Incomplete && in.supplies_north < kSuppliesNeeded)
            return { StepKind::UseMineGo, q.mine_north, q.go_north };
        if (own_s && in.mine_south == AvQStatus::Incomplete && in.supplies_south < kSuppliesNeeded)
            return { StepKind::UseMineGo, q.mine_south, q.go_south };
        return {};
    }
}
