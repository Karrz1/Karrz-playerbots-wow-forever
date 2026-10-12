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

// What a bot remembers about the window it is answering. Keyed by the window's own serial (TradeData::GetSerial), never by a
// time gap: a proposal that follows a finished or cancelled trade at once (a player retrying) is a new window that must be begun
// again, and a gap rule carried the old "already begun" over so the proposer's window never opened.
struct Seen
{
    std::uint64_t serial = 0;
    std::uint32_t first_ms = 0;
    bool begun = false;
};

inline void Adopt(Seen& seen, std::uint64_t serial, std::uint32_t now_ms)
{
    if (seen.serial == serial) return;
    seen = Seen{};
    seen.serial = serial;
    seen.first_ms = now_ms ? now_ms : 1u;
}

constexpr Step NextStep(bool begun, std::uint32_t ms_since_proposal)
{
    if (ms_since_proposal >= kTradeIdleMs) return Step::Cancel;
    return begun ? Step::Wait : Step::Begin;
}

} // namespace Playerbot::Trade
