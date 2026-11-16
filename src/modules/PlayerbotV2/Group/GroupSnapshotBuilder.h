// GroupSnapshotBuilder - Constructs a GroupSnapshot for a member's bot. Runs
// on the world thread once per snapshot publish; the resulting object is
// immutable and safe for bot worker consumption.

#pragma once

#include "GroupSnapshot.h"
#include <memory>

class Player;
class Unit;

namespace Playerbot {

class GroupSnapshotBuilder
{
public:
    // Returns nullptr if the player isn't grouped or isn't in the world.
    // Otherwise builds a fresh GroupSnapshot summarizing every member.
    static std::shared_ptr<GroupSnapshot const> Build(Player* leader_member, SnapshotVer version);

    // A hit a grouped player took inside an instance, for the member's damage_taken_per_s. From Module::OnDamageTaken, on the map's
    // thread; takes its own lock, and returns before it for everything else (open world, ungrouped, creatures).
    static void NoteDamageTaken(Unit* victim, int32 amount);
};

} // namespace Playerbot
