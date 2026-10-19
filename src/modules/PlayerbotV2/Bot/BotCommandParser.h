// BotCommandParser - Parses chat-driven bot commands ("follow", "stay",
// "attack", "release") and translates them into intents on the bot's queue.
//
// Runs on the world thread (called from the whisper hook). Keeps it lock-free
// by pushing into the per-bot IntentQueue, which the world drain consumes.

#pragma once

#include "BotTypes.h"
#include <string>

class Player;

namespace Playerbot {

class BotCommandParser
{
public:
    // Returns true if the message matched a known command (regardless of
    // success). A future iteration will return rich error info; the bool
    // shape is enough for "did the bot handle this?".
    //
    // Performs squad-address resolution at the top: if `msg` is prefixed
    // with `all:`, `tank:`, `Areon:`, etc., the command body is dispatched
    // to every bot the sender owns that matches the prefix, and the
    // whispered `bot_player` is used only as the messenger that replies
    // with a single summary line. The address resolver is in
    // BotAddressResolver.h.
    // `via` names the source for the [owner_order] log line (whisper / party / addon).
    static bool Dispatch(Player* sender, Player* bot_player, std::string const& msg, char const* via = "whisper");

    // Single-bot dispatch. Skips the address resolver — used internally
    // by Dispatch when a prefix has already been parsed and a per-target
    // command needs to run, and by tests that bypass the prefix layer.
    static bool DispatchSingle(Player* sender, Player* bot_player, std::string const& msg, char const* via = "whisper");

    // May `sender` give this bot a group order (the panel, a party / raid line)? Its owner, or for a bot with no owner a player
    // who is its controller in the group they share (GroupControl.h): the group leader when human, else the first human
    // member. An invited world bot obeys the leader exactly like a companion.
    static bool ControlsForGroupOrder(Player const* sender, Player const* bot_player);

    // The bot's role as its owner set it (`.bot add <class> tank`, setrole), else as its spec reads.
    static Role BotRole(Player* bot_player);

    // Latch a "release" order on a dead bot (the "release" / "release_all"
    // commands, and the owner's own Release Spirit via Module::OnSpiritReleased).
    // `why` is logged. World thread. Returns true when the bot is dead and holds
    // an order for this death; living bots ignore it.
    static bool OrderRelease(Player const* sender, Player const* bot, char const* why = "release order");

    // Drop the owner's "follow" order on `bot` (BotAI::owner_follow_order) and log [owner_order]
    // when it was set: the party-chat "pull" hands the fight back to the non-tanks this way.
    static void ReleaseOwnerFollowOrder(Player const* sender, Player const* bot, char const* why);

private:
    // The command table; DispatchSingle wraps it with the [owner_order] log line.
    static bool DispatchSingleImpl(Player* sender, Player* bot_player, std::string const& msg);
};

} // namespace Playerbot
