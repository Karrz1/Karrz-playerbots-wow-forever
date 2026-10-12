// PlayerCraftOrders - "craft <item link or name> [x5]" whispered by a player to a bot of their group: the bot makes it with the recipes
// and reagents it has and mails the result back. Closed to bots' own economy (CraftOrderBoard stays bot-to-bot); this is the owner's
// entry. Pure parsing / matching / planning is Bot/PlayerCraftOrder.h; this file is the world-thread glue (recipe scan, casts, mail).
//
// World thread only: Order() is called from the command parser, Tick() from the module's world update.
// Idea and pure logic from Trixx34/ForeverBots (GPL-2.0).

#pragma once

#include <cstdint>
#include <string>

class Player;

namespace Playerbot::V2::PlayerCraftOrders {

// `text` is the order as typed ("craft ..."). Whispers the answer back (queued, refused, or the recipe list). Returns true when the text
// was a craft order (even a refused one), so the parser stops looking.
bool Order(Player* requester, Player* bot, std::string const& text);

// Runs the queued crafts one at a time per bot and mails the result. Cheap: call about once a second.
void Tick(std::uint32_t now_ms);

// Orders in flight (for tests and /diag).
std::uint32_t Pending();

} // namespace Playerbot::V2::PlayerCraftOrders
