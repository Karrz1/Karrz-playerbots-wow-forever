// CasterStepOut - which engaged mob a ranged / healer bot of a 5-man instance party keeps its distance from: the pure rule behind the
// dungeon use of State_Idle's ranged spacing (5b), which in raid maps steps back from engaged elites.
//
// In a dungeon party the casters follow the tank closely, and a bot that stands inside the pack (Deadmines 2026-10-09: the mage 1.5 y
// from the Defias Evoker) is in the splash of every melee mob and of the caster's own ground effects. A mob counts when it is alive,
// targetable, in combat, held by a tank or hitting another member (a mob on the bot itself is left to the tank: running only drags it
// along) and is a caster: it is casting now, or the dungeon script lists it as a high-priority kill (the script's casters). The step
// goes to the ranged stand-off (22 y) of the group of such mobs, never into a hazard and never while the bot is casting.
// Pure: no game types, tested in tests/game/ClassicCasterStepOut.cpp.

#pragma once

namespace Playerbot {

struct StepOutMob
{
    bool alive = false;
    bool is_player = false;
    bool targetable = false;
    bool in_combat = false;
    bool held_by_tank_or_member = false;   // its victim is a tank, a member or a member's pet - not the bot itself
    bool casting_now = false;
    bool listed_caster = false;            // the script's high_priority_kill_entries
};

inline bool StepOutFromMob(StepOutMob const& m)
{
    return m.alive && !m.is_player && m.targetable && m.in_combat && m.held_by_tank_or_member && (m.casting_now || m.listed_caster);
}

}  // namespace Playerbot
