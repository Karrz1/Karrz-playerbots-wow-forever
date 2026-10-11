// FlightWait - the 5-minute wait between proactive flights exists so a bot whose last ride did not bring the goal in reach does not
// shuttle between nodes for ever. It is spent by trying; a try that never reached the flight master (the server refused the interaction
// because the bot stood out of range) is not a ride and must not spend it. Tested in tests/game/ClassicFlightWait.cpp.

#pragma once

namespace Playerbot::V2 {

enum class FlightTry : unsigned char
{
    Boarded,        // the flight started
    NotInRange,     // the flight master could not be interacted with from here; nothing was tried
    Refused         // reached the master but the server refused (cost, unknown node, no route)
};

constexpr bool FlightTrySpendsWait(FlightTry t) { return t != FlightTry::NotInRange; }

} // namespace Playerbot::V2
