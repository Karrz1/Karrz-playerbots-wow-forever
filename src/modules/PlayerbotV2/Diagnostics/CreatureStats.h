// `.playerbot creaturestats <entry> [<entry>...]`: read-only audit of the final stats creatures get from the client's ExpectedStat tables times
// the stored modifiers. Each entry is summoned next to the GM, read, logged as one [creature_stats] line and despawned. Nothing is changed.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Player;

namespace Playerbot::Diagnostics::CreatureStats
{

// Reads and logs every entry; returns the number logged. `report` gets one line per entry (the logged line, or why the entry was skipped).
std::uint32_t Run(Player* near, std::vector<std::uint32_t> const& entries, std::vector<std::string>& report);

} // namespace Playerbot::Diagnostics::CreatureStats
