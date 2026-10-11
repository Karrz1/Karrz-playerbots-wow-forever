// WorldLoadFacts - what the load-test row needs from the world population.
#pragma once

#include "Define.h"

namespace Playerbot::V2::Fleet {

struct WorldLoadFacts
{
    uint32 awake = 0;      // ambient residents logged in
    uint32 sleeping = 0;   // roster bots offline at their saved position
    uint32 rss_mb = 0;     // process memory
    double cpu_pct = 0.0;  // process CPU over the last interval, percent of one core
};

} // namespace Playerbot::V2::Fleet
