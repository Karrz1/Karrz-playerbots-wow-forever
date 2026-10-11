// PetFollowSpeed - a pet following its owner scales its run speed with the gap to the owner (up to +30%) so it can catch up. The
// scale moves a little with every step the pair takes, and PetAI recomputes it on every update, so each pet sent the nearby players
// a speed packet (spline run speed + flight speed) several times a second: with a few hundred bots and pets, 43 packets a second
// to one client. A catch-up change smaller than this is not worth a packet; the pet keeps the speed it has. Tested in
// tests/game/ClassicPetFollowSpeed.cpp.

#pragma once

#include <cmath>

constexpr float kPetCatchUpSpeedHysteresis = 0.08f;

constexpr bool PetCatchUpSpeedWorthSending(float current_rate, float wanted_rate)
{
    return (wanted_rate > current_rate ? wanted_rate - current_rate : current_rate - wanted_rate) >= kPetCatchUpSpeedHysteresis;
}
