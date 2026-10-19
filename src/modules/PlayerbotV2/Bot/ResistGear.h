// ResistGear - which slots a live resistance-gear swap may touch, and where it never runs.
//
// Before a boss that deals a resistance school (Raid::RaidRules::resistPotion rows: the same table as the potions) a grouped raid
// bot wears the vanilla pieces with that school's resistance (Gear::ResistPoints) and puts its own back once the boss is gone
// (BotPopulationManager::SwapResistGear). A tank keeps the armor and health of its head, shoulders, chest, legs, hands and feet.
// Pure: tested in tests/game/ClassicRaidResistPotions.cpp.

#pragma once

#include <cstdint>

namespace Playerbot::Classic {

// Equipment slot numbers (EquipmentSlots): head 0, neck 1, shoulders 2, chest 4, waist 5, legs 6, feet 7, wrists 8, hands 9, finger 10 / 11, back 14.
// Weapons, trinkets and the ranged slot always keep the role's picks.
constexpr bool ResistSwapSlot(std::uint8_t slot, bool tank)
{
    switch (slot)
    {
        case 1: case 5: case 8: case 10: case 11: case 14:   // neck, waist, wrists, rings, cloak: no armor to lose
            return true;
        case 0: case 2: case 4: case 6: case 7: case 9:      // head, shoulders, chest, legs, feet, hands
            return !tank;
        default:
            return false;
    }
}

// Maps the gear swap leaves alone: Molten Core and Onyxia's Lair tactics are frozen (their potion rows stay, no new behaviour).
constexpr bool ResistSwapMap(std::uint32_t map_id)
{
    return map_id != 409 && map_id != 249;
}

} // namespace Playerbot::Classic
