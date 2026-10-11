// CompanionQuest - a companion (a bot grouped with its owner) quests like a real party member.
//
// The snapshot builder fills BotSnapshot::companion_quest on the WORLD thread (it reads the live owner and
// the bot): what the bot should accept because its owner holds the quest and the giver is here, what it
// should turn in because the owner stands at the ender, which of its own quests the owner could take, and
// which quest objects (chests, goobers, nodes) it needs. The idle rules in
// Bot/States/Rules/CompanionQuestRules.cpp read the result and emit intents; nothing here touches the
// Player from a rule.
//
// The owner's orders (QuestOrders.h) arm short latches here ("accept everything nearby", "turn in what you
// can"); the builder reads them the next tick.

#pragma once

#include "Define.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

class Player;

namespace Playerbot {

struct BotSnapshot;
class BotAI;

// One (giver, quest) pair the companion should act on. `via_order` is set for pairs that exist only
// because the owner ordered "accept" / "turn in" (the owner does not hold the quest / stand at the giver).
struct CompanionQuestAction
{
    ObjectGuid giver;
    uint32     quest_id = 0;
    bool       via_order = false;
};

// A quest object (chest, goober, gathering node, quest prop) the BOT needs for one of its own open
// objectives, close to the owner.
struct CompanionQuestObject
{
    ObjectGuid go;
    uint32     entry    = 0;
    uint32     quest_id = 0;
};

struct CompanionQuestState
{
    // True when the owner is online, grouped with the bot and on its map (the lists below are only filled then).
    bool  owner_here = false;
    float owner_x = 0.f, owner_y = 0.f, owner_z = 0.f;
    std::vector<CompanionQuestAction> accept;      // owner holds it, giver in scan, bot can take it
    std::vector<CompanionQuestAction> turn_in;     // bot's quest is complete, ender in scan, owner at the ender
    std::vector<uint32>               share;       // bot's open quests the owner could take (CanShareQuest)
    std::vector<CompanionQuestObject> objects;     // quest objects the bot needs near the owner

    void reset()
    {
        owner_here = false;
        owner_x = owner_y = owner_z = 0.f;
        accept.clear();
        turn_in.clear();
        share.clear();
        objects.clear();
    }
};

namespace CompanionQuest {

// Owner-order latches (QuestOrders arms them, the builder reads them). Thread-safe (a small mutex-guarded
// map keyed by the bot's guid counter); the window is the order's reach in ms.
enum class Latch : uint8 { AcceptAll = 0, TurnInAll = 1 };
void ArmLatch(uint64 bot_low, Latch which, uint32 now_ms, uint32 window_ms);
bool LatchActive(uint64 bot_low, Latch which, uint32 now_ms);
void ClearLatch(uint64 bot_low, Latch which);

// Fill snap.companion_quest. WORLD THREAD ONLY (reads the live owner and bot). Call after the quest log,
// nearby friends / objects and quest_discovery are built. No-op (state reset) for a bot that is not a
// companion in its owner's group.
void BuildState(Player* bot, BotAI* ai, BotSnapshot& snap);

// Can the bot take this quest from a giver now (not rewarded / in log, doable, quest log room, profession
// known)? The shared QuestDoable predicate plus log room. World thread.
bool BotCanTake(Player* bot, uint32 quest_id);

// "[Title]" for logs and whispers: the quest's log title, or "quest <id>" when unknown. Static data, any thread.
std::string QuestLabel(uint32 quest_id);

} // namespace CompanionQuest

} // namespace Playerbot
