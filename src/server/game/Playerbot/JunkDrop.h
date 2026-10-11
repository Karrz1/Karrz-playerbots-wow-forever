// JunkDrop - which grey item a bot with full bags destroys to make room for loot it cannot otherwise take.
// A group in a dungeon never leaves to vendor, so the bags only ever fill; a real player drops the worst grey for something better.
// Pure decision logic, tested in tests/game/ClassicJunkDrop.cpp.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Playerbot
{

struct JunkCandidate
{
    std::uint64_t value = 0;   // vendor value of the whole stack
    bool poor = false;         // grey quality: the only kind that may be destroyed
    bool quest = false;        // quest items are never destroyed
};

// Returns the index of the cheapest destroyable grey that is worth less than the incoming loot, or npos when none is.
// A non-grey incoming item beats any grey; a grey incoming item only displaces a strictly cheaper grey.
inline constexpr std::size_t kNoJunk = static_cast<std::size_t>(-1);

inline std::size_t PickJunkToDrop(std::vector<JunkCandidate> const& bags, bool incoming_poor, std::uint64_t incoming_value)
{
    std::size_t best = kNoJunk;
    for (std::size_t i = 0; i < bags.size(); ++i)
    {
        JunkCandidate const& c = bags[i];
        if (!c.poor || c.quest) continue;
        if (incoming_poor && c.value >= incoming_value) continue;
        if (best == kNoJunk || c.value < bags[best].value) best = i;
    }
    return best;
}

} // namespace Playerbot
