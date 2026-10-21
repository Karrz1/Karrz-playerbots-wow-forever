#pragma once
// Armor cannot be put on or taken off while a bot is fighting (EQUIP_ERR_NOT_IN_COMBAT, 61) or casting (EQUIP_ERR_CLIENT_LOCKED_OUT, 40).
// The gear step stores each generated piece before it equips it, so it must not start at all in that state: it would refuse every piece
// and still be marked done. Pure: tested in tests/game/ClassicArmorSwapGate.cpp.

namespace Playerbot::Gear {

constexpr bool CanSwapArmorNow(bool in_combat, bool casting)
{
    return !in_combat && !casting;
}

} // namespace Playerbot::Gear
