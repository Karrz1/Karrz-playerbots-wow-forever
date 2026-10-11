#include "DungeonTactics.h"

#include "../DungeonScript.h"

#include <algorithm>

namespace Playerbot::Tactics {

#define PB_TACTICS_MAP(key) extern MapTactics const kTactics_##key;
#include "DungeonTacticsMaps.inc"
#undef PB_TACTICS_MAP

namespace {

std::vector<MapTactics const*> const& Maps()
{
    static std::vector<MapTactics const*> const maps = {
#define PB_TACTICS_MAP(key) &kTactics_##key,
#include "DungeonTacticsMaps.inc"
#undef PB_TACTICS_MAP
    };
    return maps;
}

} // namespace

MapTactics const* TacticsForMap(uint32_t map)
{
    for (MapTactics const* m : Maps())
        if (m->map == map)
            return m;
    return nullptr;
}

MobTactic const* FindMob(uint32_t map, uint32_t entry)
{
    MapTactics const* m = TacticsForMap(map);
    if (!m)
        return nullptr;
    MobTactic const* end = m->mobs + m->mob_count;
    MobTactic const* it = std::lower_bound(m->mobs, end, entry, [](MobTactic const& a, uint32_t e) { return a.entry < e; });
    return it != end && it->entry == entry ? it : nullptr;
}

AdviceLists DeriveAdvice(MapTactics const& map)
{
    AdviceLists out;
    std::vector<MobTactic const*> ranked;
    for (size_t i = 0; i < map.mob_count; ++i)
    {
        MobTactic const& m = map.mobs[i];
        if (m.priority >= 2 && !(m.flags & Mob::Boss))
            ranked.push_back(&m);
        if (m.flags & Mob::CcPriority)
            out.cc_entries.push_back(m.entry);
        for (uint8_t k = 0; k < m.spell_count; ++k)
        {
            SpellTactic const& s = m.spells[k];
            if (s.severity >= 2 && (s.counters & Ctr::Interrupt) && s.cast_ms != 0)
                out.mandatory_interrupts.push_back(s.spell);
            if (s.severity >= 2 && (s.counters & Ctr::Dispel))
                out.dispel_first.push_back(s.spell);
            if (s.counters & Ctr::Purge)
                out.purge_enemy.push_back(s.spell);
        }
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](MobTactic const* a, MobTactic const* b) { return a->priority > b->priority; });
    for (MobTactic const* m : ranked)
        out.kill_order.push_back(m->entry);
    return out;
}

void AppendAdvice(uint32_t map, DungeonAdvice& advice)
{
    MapTactics const* m = TacticsForMap(map);
    if (!m)
        return;
    AdviceLists const l = DeriveAdvice(*m);
    auto cat = [](std::vector<uint32_t>& dst, std::vector<uint32_t> const& src) { dst.insert(dst.end(), src.begin(), src.end()); };
    cat(advice.high_priority_kill_entries, l.kill_order);
    cat(advice.mandatory_interrupt_spells, l.mandatory_interrupts);
    cat(advice.cc_priority_entries, l.cc_entries);
    cat(advice.dispel_priority_spells, l.dispel_first);
    cat(advice.dispel_enemy_priority_spells, l.purge_enemy);
}

} // namespace Playerbot::Tactics
