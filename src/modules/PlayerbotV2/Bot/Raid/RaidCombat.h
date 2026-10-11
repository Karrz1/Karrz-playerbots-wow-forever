// RaidCombat — the in-combat consumer of the raid brain's per-bot order.

#pragma once

namespace Playerbot {
class BotAI;
class BotSnapshotView;
class BotIntentEmitter;
}

namespace Playerbot::Raid {

// Called first thing in the in-combat dispatch. Returns true when the raid
// order took this tick (a hazard move, an elected taunt/dispel/purge/crowd
// control, dropping a forbidden target, or a melee hold); false lets the
// normal combat logic run.
bool DispatchRaidOrder(BotAI& ai, BotSnapshotView const& s, BotIntentEmitter& emit);

} // namespace Playerbot::Raid
