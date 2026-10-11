// RacialRules - when a bot spends a racial: the pure decisions behind State_InCombat MaybeUseDefensiveRacial.
// Pure: no game types, tested in tests/game/ClassicRacialRules.cpp.

#pragma once

namespace Playerbot {

// Gnome Escape Artist (root / snare break, 1 minute cooldown). A root always; a snare only while the bot is on the move
// (following, kiting, stepping out of a hazard): a snared bot that stands in its fight loses nothing by it.
inline bool EscapeArtistWanted(bool rooted, bool snared, bool moving)
{
    return rooted || (snared && moving);
}

}  // namespace Playerbot
