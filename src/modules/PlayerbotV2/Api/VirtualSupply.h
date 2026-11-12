// VirtualSupply - infinite food and water for bots (PlayerbotV2.Classic.ClassSupplies).
// The restock tops the bags up and makes room for food and drink (JunkDrop.h PickRoomForSupply), but bags full of gear and quest items
// can still leave a healer with nothing to drink. Bots have infinite supplies by design, so a bot that should eat or drink and has no
// item uses the food / water rank of its level without one: the item's on-use spell, cast with no item consumed.
// Pure decision logic, tested in tests/game/ClassicVirtualSupply.cpp.

#pragma once

#include <cstdint>

namespace Playerbot::Classic
{

// The food / drink the recovery rules pick: the best one in the bags, else (infinite supplies on) the supply rank of the bot's level.
// 0 = nothing to eat or drink.
inline constexpr std::uint32_t RecoveryItemOrVirtual(std::uint32_t in_bags, bool infinite_supplies, std::uint32_t supply_rank)
{
    if (in_bags)
        return in_bags;
    return infinite_supplies ? supply_rank : 0;
}

// At use time: the bot has no such item and the entry is a food / water supply rank its level allows, so it eats / drinks without it.
// A missing item of any other kind (a quest item, a potion) is never conjured from nothing.
inline constexpr bool UseVirtually(bool in_bags, bool infinite_supplies, bool food_or_water_rank)
{
    return !in_bags && infinite_supplies && food_or_water_rank;
}

} // namespace Playerbot::Classic
