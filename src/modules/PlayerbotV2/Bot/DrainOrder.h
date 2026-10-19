// DrainOrder - where the intent drain pass starts. The bots are visited in registry order and a pass has two limits (the 30 ms wall
// budget, and the pathfinding window of Api PathBudget), so with a fixed start the bots at the tail wait every frame while the same
// head bots are served. A pass that hit a limit moves the start on by a sixth of the bots; the bots before the start go last in the
// same pass, so every bot is first in turn. Tested in tests/game/ClassicDrainOrder.cpp.
#pragma once

#include <algorithm>
#include <cstddef>

namespace Playerbot::V2
{

// The start index for the next pass. `start` is the current one, `bots` the number registered, `cut` whether this pass hit a limit.
inline std::size_t NextDrainStart(std::size_t start, std::size_t bots, bool cut)
{
    if (!bots) return 0;
    start %= bots;
    return cut ? (start + std::max<std::size_t>(1u, bots / 6u)) % bots : start;
}

} // namespace Playerbot::V2
