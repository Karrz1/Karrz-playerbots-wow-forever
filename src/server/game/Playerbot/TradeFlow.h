// TradeFlow - the bot's side of a trade a group member proposed. A proposal only opens the window for both sides once the proposed
// player answers with a begin; until then the proposer sees "You have requested to trade" and nothing else, and a second request
// answers "already trading". So: begin at once, then wait for the other side to fill and accept, and cancel what nobody finishes.
// Tested in tests/game/ClassicTradeFlow.cpp.

#pragma once

#include <cstdint>

namespace Playerbot::Trade {

enum class Step : unsigned char
{
    Begin,      // open the window now
    Wait,       // the window is open, the other side is choosing
    Cancel      // nobody finished: close it cleanly so the proposer can trade again
};

constexpr std::uint32_t kTradeIdleMs = 90000u;   // a window the other side leaves open this long is closed

constexpr Step NextStep(bool begun, std::uint32_t ms_since_proposal)
{
    if (ms_since_proposal >= kTradeIdleMs) return Step::Cancel;
    return begun ? Step::Wait : Step::Begin;
}

} // namespace Playerbot::Trade
