#include "RaidAdvice.h"
#include "RaidEncounters.h"
#include "../Dungeon/DungeonScript.h"

namespace Playerbot::Raid {

void ApplyRaidAdvice(uint32_t mapId, DungeonAdvice& advice)
{
    RaidRules const* rules = RulesForMap(mapId);
    if (!rules)
        return;
    advice.high_priority_kill_entries = KillOrder(mapId);
    if (!rules->bosses.empty())
        advice.bosses = rules->bosses;
    advice.dangerous_auras.clear();
    advice.spread_on_self_auras.clear();
    advice.kite_creature_entries.clear();
    advice.soak_spells.clear();
    advice.stack_on_cast_spells.clear();
    advice.cc_priority_entries.clear();
    advice.tank_swap_on_spells.clear();
    advice.dispel_priority_spells.clear();
    advice.dispel_enemy_priority_spells.clear();
}

} // namespace Playerbot::Raid
