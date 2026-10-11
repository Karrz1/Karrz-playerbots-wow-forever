// TankHazardHold - whether a tank standing in a damaging zone stays put while one of its mobs casts (State_Idle,
// RaidTankHazardStepOut).
//
// The hold exists for casts that depend on where the mob faces: a breath or cleave is aimed when the cast starts, and a tank that
// walks turns the mob toward whoever it walks past. It must not hold for a cast that is dropped on the tank's own position: a
// Defias Evoker's Flamestrike (spell 11829, repeated every 3-17 s on its victim) lands where the tank stood when the cast began,
// so a tank that stays "because the mob is casting" is exactly the one it hits (Deadmines scenario run, 2026-10-09: the level 21
// tank stood in the zone for the whole fight with four mobs on it and died to the third Flamestrike).
//
// Pure: no game types, tested in tests/game/ClassicTankHazardHold.cpp.

#pragma once

namespace Playerbot {

// zone_is_this_spell: the zone the tank stands in was made by the spell the mob is casting now (the next one drops on the same victim).
// ground_area_on_victim: the cast is an area spell whose centre is its target's position (Flamestrike, Blizzard, Rain of Fire).
inline bool TankHoldsWhileMobCasts(bool zone_is_this_spell, bool ground_area_on_victim)
{
    return !zone_is_this_spell && !ground_area_on_victim;
}

} // namespace Playerbot
