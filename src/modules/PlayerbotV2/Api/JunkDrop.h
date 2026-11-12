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
    bool spare_supply = false; // a stack of a shared consumable the restock hands out and tops up again (potions, Demonic Runes,
                               // bandages): never food or drink, never a class tool or reagent, never the player's own items
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

// Room for a bot's food or drink (ClassicSupplies "Food" / "Water"): bots have endless supplies, and a healer with full bags and no drink
// cannot rest between pulls (deadmines_full on 0.1.23: the priest's bags were full, no water, healer_no_rest then healer_oom). The cheapest
// grey goes first; with none, the cheapest stack of a spare supply (it is restocked later, after the food and drink). Never anything else:
// the bot's gear and what its player gave it stay. npos when nothing may go.
inline std::size_t PickRoomForSupply(std::vector<JunkCandidate> const& bags)
{
    std::size_t best = kNoJunk;
    for (std::size_t i = 0; i < bags.size(); ++i)
        if (bags[i].poor && !bags[i].quest && (best == kNoJunk || bags[i].value < bags[best].value))
            best = i;
    if (best != kNoJunk)
        return best;
    for (std::size_t i = 0; i < bags.size(); ++i)
        if (bags[i].spare_supply && !bags[i].quest && (best == kNoJunk || bags[i].value < bags[best].value))
            best = i;
    return best;
}

} // namespace Playerbot
