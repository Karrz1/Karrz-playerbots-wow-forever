#include "QuestObjectiveLocations.h"

#include "QuestReverseIndex.h"   // KillCreditAliasesFor / CreaturesWithLabel

#include "ConditionMgr.h"
#include "CreatureData.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "GameObjectData.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace Playerbot {

namespace {

// ---------------------------------------------------------------------------
// Built artifacts. Written once inside the call_once, read-only afterwards.
// ---------------------------------------------------------------------------
using SetPtr = std::shared_ptr<ObjectiveLocationSet const>;

std::unordered_map<uint64, SetPtr>   g_by_objective;      // (quest<<32 | objective id) -> set
std::unordered_set<uint64>           g_item_from_creature; // (item<<32 | creature entry)
std::unordered_set<uint64>           g_item_from_go;       // (item<<32 | go entry)
std::once_flag                       g_built;

inline uint64 ObjKey(uint32 quest_id, uint32 objective_id)
{
    return (uint64(quest_id) << 32) | uint64(objective_id);
}
inline uint64 PairKey(uint32 a, uint32 b)
{
    return (uint64(a) << 32) | uint64(b);
}

// A weighted source entry (creature or GO) of an objective.
using WeightedEntries = std::unordered_map<uint32, float>;   // entry -> weight (max over rows)

inline void AddWeighted(WeightedEntries& m, uint32 entry, float w)
{
    if (!entry) return;
    auto [it, inserted] = m.emplace(entry, w);
    if (!inserted && w > it->second)
        it->second = w;
}

// ---------------------------------------------------------------------------
// Clustering. Single-linkage over planar distance within one map, using a
// hash grid with cell size = link distance (each point only checks the 3x3
// neighbouring cells). Anchor = the member nearest the weighted centroid, so
// the published waypoint is a real spawn position (correct floor / Z, never
// the middle of a lake between two camps).
// ---------------------------------------------------------------------------
struct Pt
{
    uint32 map;
    float  x, y, z;
    float  w;
    float  extra_radius;   // AreaTrigger own radius, 0 for spawns
    uint32 entry;
    ObjectiveClusterKind kind;
};

constexpr float kLinkDist = 50.0f;

// Above these an item's loot sources are common trade goods, not a quest location.
constexpr size_t kMaxLootSourceEntries = 64;
constexpr size_t kMaxLootClusters = 200;

struct UnionFind
{
    std::vector<uint32> parent;
    explicit UnionFind(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0u); }
    uint32 Find(uint32 a)
    {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    }
    void Unite(uint32 a, uint32 b)
    {
        a = Find(a); b = Find(b);
        if (a != b) parent[b] = a;
    }
};

// Single linkage chains a whole field of evenly spread spawns (a boar meadow,
// a mine of kobolds) into one cluster hundreds of yards wide, whose anchor is
// then a spawn in the middle and whose "radius" says nothing. A component wider
// than this is split into kSplitCell squares, one cluster each.
constexpr float kMaxClusterRadius = 150.0f;
constexpr float kSplitCell = 100.0f;

ObjectiveCluster MakeCluster(std::vector<Pt> const& pts, size_t begin, std::vector<uint32> const& idx)
{
    double sw = 0.0, sx = 0.0, sy = 0.0;
    for (uint32 i : idx)
    {
        Pt const& p = pts[begin + i];
        const double w = std::max(double(p.w), 0.01);
        sw += w; sx += w * p.x; sy += w * p.y;
    }
    const float cx = float(sx / sw), cy = float(sy / sw);
    uint32 anchor = idx.front();
    float bestSq = std::numeric_limits<float>::max();
    for (uint32 i : idx)
    {
        Pt const& p = pts[begin + i];
        const float ex = p.x - cx, ey = p.y - cy;
        const float d = ex * ex + ey * ey;
        if (d < bestSq) { bestSq = d; anchor = i; }
    }
    Pt const& a = pts[begin + anchor];
    ObjectiveCluster c;
    c.map_id       = a.map;
    c.x            = a.x;
    c.y            = a.y;
    c.z            = a.z;
    c.anchor_entry = a.entry;
    c.kind         = a.kind;
    c.count        = uint32(idx.size());
    float rsq = 0.f, weight = 0.f, extra = 0.f;
    for (uint32 i : idx)
    {
        Pt const& p = pts[begin + i];
        const float ex = p.x - a.x, ey = p.y - a.y;
        rsq = std::max(rsq, ex * ex + ey * ey);
        weight += p.w;
        extra = std::max(extra, p.extra_radius);
    }
    c.radius = std::max(std::sqrt(rsq), extra);
    c.weight = weight;
    return c;
}

std::vector<ObjectiveCluster> ClusterPoints(std::vector<Pt>& pts)
{
    std::vector<ObjectiveCluster> out;
    if (pts.empty())
        return out;

    std::sort(pts.begin(), pts.end(), [](Pt const& a, Pt const& b) { return a.map < b.map; });

    size_t begin = 0;
    while (begin < pts.size())
    {
        size_t end = begin;
        while (end < pts.size() && pts[end].map == pts[begin].map)
            ++end;
        const size_t n = end - begin;

        UnionFind uf(n);
        std::unordered_map<int64, std::vector<uint32>> grid;
        grid.reserve(n);
        auto cellKey = [](int cx, int cy) -> int64 { return (int64(cx) << 32) ^ int64(uint32(cy)); };
        constexpr float kLinkSq = kLinkDist * kLinkDist;
        for (uint32 i = 0; i < n; ++i)
        {
            Pt const& p = pts[begin + i];
            const int cx = int(std::floor(p.x / kLinkDist));
            const int cy = int(std::floor(p.y / kLinkDist));
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                {
                    auto it = grid.find(cellKey(cx + dx, cy + dy));
                    if (it == grid.end()) continue;
                    for (uint32 j : it->second)
                    {
                        Pt const& q = pts[begin + j];
                        const float ex = p.x - q.x, ey = p.y - q.y;
                        if (ex * ex + ey * ey <= kLinkSq)
                            uf.Unite(i, j);
                    }
                }
            grid[cellKey(cx, cy)].push_back(i);
        }

        std::unordered_map<uint32, std::vector<uint32>> members;
        for (uint32 i = 0; i < n; ++i)
            members[uf.Find(i)].push_back(i);

        for (auto const& [root, idx] : members)
        {
            ObjectiveCluster c = MakeCluster(pts, begin, idx);
            // An AreaTrigger's own radius is not a chain of spawns: never split.
            if (c.radius <= kMaxClusterRadius || c.kind == ObjectiveClusterKind::AreaTrigger)
            {
                out.push_back(c);
                continue;
            }
            std::unordered_map<int64, std::vector<uint32>> cells;
            for (uint32 i : idx)
            {
                Pt const& p = pts[begin + i];
                cells[cellKey(int(std::floor(p.x / kSplitCell)), int(std::floor(p.y / kSplitCell)))].push_back(i);
            }
            for (auto const& [key, cell] : cells)
                out.push_back(MakeCluster(pts, begin, cell));
        }
        begin = end;
    }

    std::sort(out.begin(), out.end(),
        [](ObjectiveCluster const& a, ObjectiveCluster const& b) { return a.weight > b.weight; });
    return out;
}

void IndexClustersByMap(ObjectiveLocationSet& set)
{
    set.clusters_by_map.clear();
    for (uint32 i = 0; i < set.clusters.size(); ++i)
        set.clusters_by_map[set.clusters[i].map_id].push_back(i);
}

// ---------------------------------------------------------------------------
// Build-time context: spawn indices and the loot / quest-item / spell-target
// source tables. Lives only for the duration of the build.
// ---------------------------------------------------------------------------
struct BuildCtx
{
    std::unordered_map<uint32, std::vector<::CreatureData const*>>   creature_spawns;   // entry -> spawns
    std::unordered_map<uint32, std::vector<::GameObjectData const*>> go_spawns;         // entry -> spawns

    std::unordered_map<uint32, WeightedEntries> item_creatures_qi;    // item -> creature (questitem table)
    std::unordered_map<uint32, WeightedEntries> item_gos_qi;          // item -> go (questitem table)
    std::unordered_map<uint32, WeightedEntries> item_creatures_loot;  // item -> creature (loot)
    std::unordered_map<uint32, WeightedEntries> item_gos_loot;        // item -> go (loot)
    // The entries of each item's QuestRequired=1 loot rows (dropped only for a holder of the quest).
    std::unordered_map<uint32, std::unordered_set<uint32>> item_creatures_loot_qr;
    std::unordered_map<uint32, std::unordered_set<uint32>> item_gos_loot_qr;
    // Items that are ordinary loot of too many sources to be a quest location (trade goods).
    std::unordered_set<uint32> common_loot_items;

    std::unordered_map<uint32, std::vector<uint32>> spell_focus_gos;     // focus type -> GO entries
    std::unordered_map<uint32, std::vector<uint32>> spell_target_cre;    // spell -> creature entries
    std::unordered_map<uint32, std::vector<uint32>> spell_target_go;     // spell -> GO entries

    std::unordered_map<uint32, std::vector<uint32>> quest_areatriggers;  // quest -> AreaTrigger ids

    // Memo: identical source sets share one built set (a mob named by many
    // quests, an item collected by several quests).
    std::unordered_map<uint64, SetPtr> memo;
};

void LoadSpawnIndices(BuildCtx& ctx)
{
    for (auto const& [spawn_id, cd] : sObjectMgr->GetAllCreatureData())
        ctx.creature_spawns[cd.id].push_back(&cd);
    for (auto const& [spawn_id, gd] : sObjectMgr->GetAllGameObjectData())
        ctx.go_spawns[gd.id].push_back(&gd);
}

void LoadQuestItemTables(BuildCtx& ctx)
{
    if (QueryResult r = WorldDatabase.Query(
            "SELECT CAST(ItemId AS UNSIGNED), CAST(CreatureEntry AS UNSIGNED) FROM creature_questitem"))
        do
        {
            Field* f = r->Fetch();
            AddWeighted(ctx.item_creatures_qi[uint32(f[0].GetUInt64())], uint32(f[1].GetUInt64()), 1.0f);
        } while (r->NextRow());
    if (QueryResult r = WorldDatabase.Query(
            "SELECT CAST(ItemId AS UNSIGNED), CAST(GameObjectEntry AS UNSIGNED) FROM gameobject_questitem"))
        do
        {
            Field* f = r->Fetch();
            AddWeighted(ctx.item_gos_qi[uint32(f[0].GetUInt64())], uint32(f[1].GetUInt64()), 1.0f);
        } while (r->NextRow());
}

// Drop chance -> weight. Chance 0 inside a group is an equal-chanced group
// member: unknown share, use a conservative 0.3.
inline float LootWeight(float chance, uint32 group_id)
{
    if (chance > 0.f) return std::min(chance, 100.f) / 100.f;
    return group_id ? 0.3f : 1.0f;
}

// One loot table (creature_ or gameobject_loot_template): direct item rows and
// one reference level (reference_loot_template), restricted in SQL to items
// that are some quest's ITEM objective. Loot ids are mapped to the template
// entries that use them.
void LoadLootTable(char const* table,
    std::unordered_map<uint32, std::vector<uint32>> const& lootid_to_entries,
    std::unordered_map<uint32, WeightedEntries>& out,
    std::unordered_map<uint32, std::unordered_set<uint32>>& out_quest_required)
{
    auto add = [&](uint32 loot_id, uint32 item, float w, bool quest_required)
    {
        auto it = lootid_to_entries.find(loot_id);
        if (it == lootid_to_entries.end()) return;
        auto& m = out[item];
        for (uint32 entry : it->second)
        {
            AddWeighted(m, entry, w);
            if (quest_required)
                out_quest_required[item].insert(entry);
        }
    };

    if (QueryResult r = WorldDatabase.PQuery(
            "SELECT CAST(l.Entry AS UNSIGNED), CAST(l.Item AS UNSIGNED), l.Chance, CAST(l.GroupId AS UNSIGNED), "
            "CAST(l.QuestRequired AS UNSIGNED) "
            "FROM {} l WHERE l.ItemType = 0 AND l.Item IN (SELECT ObjectID FROM quest_objectives WHERE Type = 1)",
            table))
        do
        {
            Field* f = r->Fetch();
            add(uint32(f[0].GetUInt64()), uint32(f[1].GetUInt64()),
                LootWeight(f[2].GetFloat(), uint32(f[3].GetUInt64())), f[4].GetUInt64() != 0);
        } while (r->NextRow());

    if (QueryResult r = WorldDatabase.PQuery(
            "SELECT CAST(l.Entry AS UNSIGNED), CAST(rl.Item AS UNSIGNED), l.Chance AS ref_chance, rl.Chance AS item_chance, "
            "CAST(rl.GroupId AS UNSIGNED), "
            "CAST(rl.QuestRequired AS UNSIGNED) "
            "FROM {} l JOIN reference_loot_template rl ON rl.Entry = l.Item AND rl.ItemType = 0 "
            "WHERE l.ItemType = 1 AND rl.Item IN (SELECT ObjectID FROM quest_objectives WHERE Type = 1)",
            table))
        do
        {
            Field* f = r->Fetch();
            const float ref_chance = f[2].GetFloat();
            const float ref_w = ref_chance > 0.f ? std::min(ref_chance, 100.f) / 100.f : 1.0f;
            add(uint32(f[0].GetUInt64()), uint32(f[1].GetUInt64()),
                ref_w * LootWeight(f[3].GetFloat(), uint32(f[4].GetUInt64())), f[5].GetUInt64() != 0);
        } while (r->NextRow());
}

void LoadLoot(BuildCtx& ctx)
{
    std::unordered_map<uint32, std::vector<uint32>> creature_loot_ids;
    for (auto const& [entry, ct] : sObjectMgr->GetCreatureTemplates())
        for (auto const& [diff, cd] : ct.difficultyStore)
            if (cd.LootID)
            {
                auto& v = creature_loot_ids[cd.LootID];
                if (std::find(v.begin(), v.end(), entry) == v.end())
                    v.push_back(entry);
            }

    std::unordered_map<uint32, std::vector<uint32>> go_loot_ids;
    for (auto const& [entry, gt] : sObjectMgr->GetGameObjectTemplates())
        if (uint32 loot = gt.GetLootId())
            go_loot_ids[loot].push_back(entry);

    LoadLootTable("creature_loot_template",   creature_loot_ids, ctx.item_creatures_loot, ctx.item_creatures_loot_qr);
    LoadLootTable("gameobject_loot_template", go_loot_ids,       ctx.item_gos_loot,       ctx.item_gos_loot_qr);

    // A QuestRequired loot row is the quest's own drop (only a holder of the
    // quest sees it): when an item has any, those are its sources and the
    // incidental droppers of the same item are not.
    auto prefer_quest_required = [](std::unordered_map<uint32, WeightedEntries>& m,
        std::unordered_map<uint32, std::unordered_set<uint32>> const& qr)
    {
        for (auto& [item, entries] : m)
        {
            auto q = qr.find(item);
            if (q == qr.end() || q->second.empty())
                continue;
            for (auto it = entries.begin(); it != entries.end();)
                it = q->second.contains(it->first) ? std::next(it) : entries.erase(it);
        }
    };
    prefer_quest_required(ctx.item_creatures_loot, ctx.item_creatures_loot_qr);
    prefer_quest_required(ctx.item_gos_loot, ctx.item_gos_loot_qr);

    // ike3 rule (QuestValues EntryQuestRelationMap): a source only counts when
    // the drop is real farming material - quest-class item or > 5% chance. We
    // drop thin sources only when the item ALSO has a decent source, so an
    // item whose every source is rare still resolves somewhere.
    auto prune = [&ctx](std::unordered_map<uint32, WeightedEntries>& m,
        std::unordered_map<uint32, std::unordered_set<uint32>> const& qr)
    {
        for (auto& [item, entries] : m)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item);
            const bool quest_item = (proto && proto->GetClass() == ITEM_CLASS_QUEST) || qr.contains(item);
            if (!quest_item)
            {
                bool any_good = false;
                for (auto const& [e, w] : entries)
                    if (w >= 0.05f) { any_good = true; break; }
                if (any_good)
                    for (auto it = entries.begin(); it != entries.end();)
                        it = (it->second < 0.05f) ? entries.erase(it) : std::next(it);
            }
            // Ordinary loot of a huge set of creatures (Linen Cloth from every
            // humanoid, meat, hides): trade goods a quest happens to ask for,
            // not a place. Every dropper as a quest location / kill target would
            // send the bot anywhere and make the snapshot walk thousands of
            // clusters. Quest items and QuestRequired drops are never common.
            if (!quest_item && entries.size() > kMaxLootSourceEntries)
                ctx.common_loot_items.insert(item);
        }
    };
    prune(ctx.item_creatures_loot, ctx.item_creatures_loot_qr);
    prune(ctx.item_gos_loot, ctx.item_gos_loot_qr);
}

void LoadSpellTargets(BuildCtx& ctx)
{
    for (auto const& [entry, gt] : sObjectMgr->GetGameObjectTemplates())
    {
        if (gt.type != GAMEOBJECT_TYPE_SPELL_FOCUS) continue;
        for (uint32 focus : { gt.spellFocus.spellFocusType, gt.spellFocus.spellFocusType2,
                              gt.spellFocus.spellFocusType3, gt.spellFocus.spellFocusType4 })
            if (focus)
                ctx.spell_focus_gos[focus].push_back(entry);
    }

    // Spell target restrictions from `conditions`: implicit-target object
    // entry (source 13 / OBJECT_ENTRY_GUID) and "must be near" (source 17 /
    // NEAR_CREATURE, NEAR_GAMEOBJECT). Negative conditions describe what the
    // spell must NOT hit, so they are not locations.
    if (QueryResult r = WorldDatabase.PQuery(
            "SELECT CAST(SourceTypeOrReferenceId AS SIGNED), CAST(SourceEntry AS UNSIGNED), "
            "CAST(ConditionTypeOrReference AS SIGNED), CAST(ConditionValue1 AS UNSIGNED), CAST(ConditionValue2 AS UNSIGNED) "
            "FROM conditions WHERE NegativeCondition = 0 AND "
            "((SourceTypeOrReferenceId = {} AND ConditionTypeOrReference = {}) OR "
            " (SourceTypeOrReferenceId = {} AND ConditionTypeOrReference IN ({}, {})))",
            int32(CONDITION_SOURCE_TYPE_SPELL_IMPLICIT_TARGET), int32(CONDITION_OBJECT_ENTRY_GUID),
            int32(CONDITION_SOURCE_TYPE_SPELL), int32(CONDITION_NEAR_CREATURE), int32(CONDITION_NEAR_GAMEOBJECT)))
        do
        {
            Field* f = r->Fetch();
            const int64  src   = f[0].GetInt64();
            const uint32 spell = uint32(f[1].GetUInt64());
            const int64  cond  = f[2].GetInt64();
            const uint32 v1    = uint32(f[3].GetUInt64());
            const uint32 v2    = uint32(f[4].GetUInt64());
            if (src == CONDITION_SOURCE_TYPE_SPELL_IMPLICIT_TARGET && cond == CONDITION_OBJECT_ENTRY_GUID)
            {
                if (v1 == TYPEID_UNIT && v2)        ctx.spell_target_cre[spell].push_back(v2);
                else if (v1 == TYPEID_GAMEOBJECT && v2) ctx.spell_target_go[spell].push_back(v2);
            }
            else if (cond == CONDITION_NEAR_CREATURE && v1)
                ctx.spell_target_cre[spell].push_back(v1);
            else if (cond == CONDITION_NEAR_GAMEOBJECT && v1)
                ctx.spell_target_go[spell].push_back(v1);
        } while (r->NextRow());
}

void LoadQuestAreaTriggers(BuildCtx& ctx)
{
    // areatrigger_involvedrelation (+ objectives with a real ObjectID) as
    // loaded by ObjectMgr: trigger -> quests. Reverse it per quest.
    for (AreaTriggerEntry const* at : sAreaTriggerStore)
    {
        if (!at) continue;
        if (std::unordered_set<uint32> const* quests = sObjectMgr->GetQuestsForAreaTrigger(at->ID))
            for (uint32 q : *quests)
                ctx.quest_areatriggers[q].push_back(at->ID);
    }
}

// Gather spawn points of weighted creature / GO entries.
void GatherSpawns(BuildCtx const& ctx, WeightedEntries const& cre, WeightedEntries const& gos, std::vector<Pt>& pts)
{
    for (auto const& [entry, w] : cre)
    {
        auto it = ctx.creature_spawns.find(entry);
        if (it == ctx.creature_spawns.end()) continue;
        for (::CreatureData const* cd : it->second)
            pts.push_back({ cd->mapId, cd->spawnPoint.GetPositionX(), cd->spawnPoint.GetPositionY(),
                            cd->spawnPoint.GetPositionZ(), w, 0.f, entry, ObjectiveClusterKind::Creature });
    }
    for (auto const& [entry, w] : gos)
    {
        auto it = ctx.go_spawns.find(entry);
        if (it == ctx.go_spawns.end()) continue;
        for (::GameObjectData const* gd : it->second)
            pts.push_back({ gd->mapId, gd->spawnPoint.GetPositionX(), gd->spawnPoint.GetPositionY(),
                            gd->spawnPoint.GetPositionZ(), w, 0.f, entry, ObjectiveClusterKind::GameObject });
    }
}

std::vector<uint32> SortedKeys(WeightedEntries const& m)
{
    std::vector<uint32> v;
    v.reserve(m.size());
    for (auto const& [e, w] : m) v.push_back(e);
    std::sort(v.begin(), v.end());
    return v;
}

SetPtr MakeSet(BuildCtx const& ctx, ObjectiveLocSource source, WeightedEntries const& cre, WeightedEntries const& gos)
{
    auto set = std::make_shared<ObjectiveLocationSet>();
    set->source           = source;
    set->creature_sources = SortedKeys(cre);
    set->go_sources       = SortedKeys(gos);
    std::vector<Pt> pts;
    GatherSpawns(ctx, cre, gos, pts);
    set->clusters = ClusterPoints(pts);
    IndexClustersByMap(*set);
    return set;
}

// Spell-target sources of a quest's start item (use-item-at/on quests).
bool StartItemSpellTargets(BuildCtx const& ctx, Quest const* quest, WeightedEntries& cre, WeightedEntries& gos)
{
    const uint32 src_item = quest->GetSrcItemId();
    if (!src_item) return false;
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(src_item);
    if (!proto) return false;
    for (ItemEffectEntry const* eff : proto->Effects)
    {
        if (!eff || eff->SpellID <= 0) continue;
        if (eff->TriggerType != /*ITEM_SPELLTRIGGER_ON_USE*/ 0) continue;
        const uint32 spell_id = uint32(eff->SpellID);
        if (SpellInfo const* si = sSpellMgr->GetSpellInfo(spell_id, DIFFICULTY_NONE))
            if (si->RequiresSpellFocus)
                if (auto it = ctx.spell_focus_gos.find(si->RequiresSpellFocus); it != ctx.spell_focus_gos.end())
                    for (uint32 g : it->second) AddWeighted(gos, g, 1.0f);
        if (auto it = ctx.spell_target_cre.find(spell_id); it != ctx.spell_target_cre.end())
            for (uint32 c : it->second) AddWeighted(cre, c, 1.0f);
        if (auto it = ctx.spell_target_go.find(spell_id); it != ctx.spell_target_go.end())
            for (uint32 g : it->second) AddWeighted(gos, g, 1.0f);
    }
    return !cre.empty() || !gos.empty();
}

enum MemoKind : uint64 { MemoCreature = 1, MemoLabel = 2, MemoGo = 3, MemoItem = 4 };
inline uint64 MemoKey(MemoKind k, uint32 id) { return (uint64(k) << 32) | id; }

SetPtr BuildForObjective(BuildCtx& ctx, Quest const* quest, QuestObjective const& obj)
{
    switch (obj.Type)
    {
        case QUEST_OBJECTIVE_MONSTER:
        case QUEST_OBJECTIVE_TALKTO:
        {
            if (obj.ObjectID <= 0) return nullptr;
            const uint32 entry = uint32(obj.ObjectID);
            auto& slot = ctx.memo[MemoKey(MemoCreature, entry)];
            if (!slot)
            {
                WeightedEntries cre, gos;
                AddWeighted(cre, entry, 1.0f);
                for (uint32 a : KillCreditAliasesFor(entry))
                    AddWeighted(cre, a, 1.0f);
                slot = MakeSet(ctx, ObjectiveLocSource::Spawn, cre, gos);
            }
            return slot;
        }
        case QUEST_OBJECTIVE_KILL_WITH_LABEL:
        {
            auto& slot = ctx.memo[MemoKey(MemoLabel, uint32(obj.ObjectID))];
            if (!slot)
            {
                WeightedEntries cre, gos;
                for (uint32 e : CreaturesWithLabel(obj.ObjectID))
                    AddWeighted(cre, e, 1.0f);
                slot = MakeSet(ctx, ObjectiveLocSource::Spawn, cre, gos);
            }
            return slot;
        }
        case QUEST_OBJECTIVE_GAMEOBJECT:
        {
            if (obj.ObjectID <= 0) return nullptr;
            auto& slot = ctx.memo[MemoKey(MemoGo, uint32(obj.ObjectID))];
            if (!slot)
            {
                WeightedEntries cre, gos;
                AddWeighted(gos, uint32(obj.ObjectID), 1.0f);
                slot = MakeSet(ctx, ObjectiveLocSource::Spawn, cre, gos);
            }
            return slot;
        }
        case QUEST_OBJECTIVE_ITEM:
        {
            if (obj.ObjectID <= 0) return nullptr;
            const uint32 item = uint32(obj.ObjectID);
            auto& slot = ctx.memo[MemoKey(MemoItem, item)];
            if (!slot)
            {
                WeightedEntries cre, gos;
                ObjectiveLocSource src = ObjectiveLocSource::None;
                if (auto it = ctx.item_creatures_qi.find(item); it != ctx.item_creatures_qi.end())
                    for (auto const& [e, w] : it->second) { AddWeighted(cre, e, w); src = ObjectiveLocSource::QuestItem; }
                if (auto it = ctx.item_gos_qi.find(item); it != ctx.item_gos_qi.end())
                    for (auto const& [e, w] : it->second) { AddWeighted(gos, e, w); src = ObjectiveLocSource::QuestItem; }
                const bool had_qi = !cre.empty() || !gos.empty();
                // Common trade goods: no loot location at all (the questitem rows,
                // when there are any, still are one).
                const bool common = ctx.common_loot_items.contains(item);
                if (!common)
                {
                    if (auto it = ctx.item_creatures_loot.find(item); it != ctx.item_creatures_loot.end())
                        for (auto const& [e, w] : it->second)
                            AddWeighted(cre, e, had_qi && cre.count(e) ? 1.0f : w);
                    if (auto it = ctx.item_gos_loot.find(item); it != ctx.item_gos_loot.end())
                        for (auto const& [e, w] : it->second)
                            AddWeighted(gos, e, had_qi && gos.count(e) ? 1.0f : w);
                }
                if (!had_qi && (!cre.empty() || !gos.empty()))
                    src = ObjectiveLocSource::Loot;
                slot = MakeSet(ctx, src, cre, gos);
                // Spread over the whole world even under the entry cap (a few
                // creature types with hundreds of camps): the same, unresolved.
                if (src == ObjectiveLocSource::Loot && slot->clusters.size() > kMaxLootClusters)
                {
                    ctx.common_loot_items.insert(item);
                    slot = std::make_shared<ObjectiveLocationSet>();
                }
            }
            // Nothing drops it (a created item: "fill the phial at the pool"):
            // the location is where the quest's start item is used.
            if (slot->clusters.empty())
            {
                WeightedEntries cre, gos;
                if (StartItemSpellTargets(ctx, quest, cre, gos))
                {
                    SetPtr spell_set = MakeSet(ctx, ObjectiveLocSource::SpellTarget, cre, gos);
                    if (!spell_set->clusters.empty())
                    {
                        // Location = the spell target; the source lists stay the
                        // item's own droppers only (a spell-focus GO or a "use near
                        // NPC X" creature is a place, never a kill / loot target).
                        auto merged = std::make_shared<ObjectiveLocationSet>(*spell_set);
                        merged->creature_sources = slot->creature_sources;
                        merged->go_sources       = slot->go_sources;
                        return merged;
                    }
                }
            }
            return slot;
        }
        case QUEST_OBJECTIVE_AREATRIGGER:
        {
            std::vector<uint32> ats;
            if (obj.ObjectID > 0)
                ats.push_back(uint32(obj.ObjectID));
            else if (auto it = ctx.quest_areatriggers.find(quest->GetQuestId()); it != ctx.quest_areatriggers.end())
                ats = it->second;
            std::vector<Pt> pts;
            for (uint32 id : ats)
                if (AreaTriggerEntry const* at = sAreaTriggerStore.LookupEntry(id))
                {
                    float r = at->Radius;
                    r = std::max({ r, at->BoxLength, at->BoxWidth });
                    if (r < 1.0f) r = 5.0f;
                    pts.push_back({ uint32(at->ContinentID), at->Pos.X, at->Pos.Y, at->Pos.Z, 1.0f, r, at->ID,
                                    ObjectiveClusterKind::AreaTrigger });
                }
            if (pts.empty()) return nullptr;
            auto set = std::make_shared<ObjectiveLocationSet>();
            set->source   = ObjectiveLocSource::AreaTrigger;
            set->clusters = ClusterPoints(pts);
            IndexClustersByMap(*set);
            return set;
        }
        default:
            return nullptr;
    }
}

bool PoiMatchesObjective(QuestPOIData const* poi, QuestObjective const& obj)
{
    if (!poi) return false;
    for (QuestPOIBlobData const& b : poi->Blobs)
    {
        if (b.Points.empty()) continue;
        if ((obj.ID != 0 && b.QuestObjectiveID == int32(obj.ID)) ||
            b.QuestObjectID == obj.ObjectID ||
            b.ObjectiveIndex == int32(obj.StorageIndex))
            return true;
    }
    return false;
}

void BuildOnce()
{
    const uint32 start_ms = getMSTime();
    BuildCtx ctx;
    LoadSpawnIndices(ctx);
    LoadQuestItemTables(ctx);
    LoadLoot(ctx);
    LoadSpellTargets(ctx);
    LoadQuestAreaTriggers(ctx);

    // Drop lookup sets used by the item-objective kill / gather rules.
    for (auto const* m : { &ctx.item_creatures_qi, &ctx.item_creatures_loot })
        for (auto const& [item, entries] : *m)
            for (auto const& [e, w] : entries)
                g_item_from_creature.insert(PairKey(item, e));
    for (auto const* m : { &ctx.item_gos_qi, &ctx.item_gos_loot })
        for (auto const& [item, entries] : *m)
            for (auto const& [e, w] : entries)
                g_item_from_go.insert(PairKey(item, e));

    uint32 n_poi = 0, n_spawn = 0, n_qi = 0, n_loot = 0, n_at = 0, n_spell = 0, n_unresolved = 0;
    auto tally = [&](bool has_poi, SetPtr const& set)
    {
        if (has_poi) { ++n_poi; return; }
        if (!set || set->clusters.empty()) { ++n_unresolved; return; }
        switch (set->source)
        {
            case ObjectiveLocSource::Spawn:       ++n_spawn; break;
            case ObjectiveLocSource::QuestItem:   ++n_qi;    break;
            case ObjectiveLocSource::Loot:        ++n_loot;  break;
            case ObjectiveLocSource::AreaTrigger: ++n_at;    break;
            case ObjectiveLocSource::SpellTarget: ++n_spell; break;
            default:                              ++n_unresolved; break;
        }
    };

    for (auto const& [quest_id, qptr] : sObjectMgr->GetQuestTemplates())
    {
        Quest const* quest = qptr.get();
        if (!quest) continue;
        QuestPOIData const* poi = sObjectMgr->GetQuestPOIData(int32(quest_id));

        // Objective-less quest with a start item: the synthesized use-item
        // objective (id 0) - location = where the item's spell must be used.
        if (quest->GetObjectives().empty() && quest->GetSrcItemId())
        {
            WeightedEntries cre, gos;
            SetPtr set;
            if (StartItemSpellTargets(ctx, quest, cre, gos))
            {
                // Spell-target entries are a place, not kill / loot targets.
                set = MakeSet(ctx, ObjectiveLocSource::SpellTarget, cre, gos);
                if (!set->clusters.empty())
                {
                    auto place = std::make_shared<ObjectiveLocationSet>(*set);
                    place->creature_sources.clear();
                    place->go_sources.clear();
                    g_by_objective.emplace(ObjKey(quest_id, 0), place);
                }
            }
            tally(poi && !poi->Blobs.empty(), set);
            continue;
        }

        for (QuestObjective const& obj : quest->GetObjectives())
        {
            switch (obj.Type)
            {
                case QUEST_OBJECTIVE_MONSTER:
                case QUEST_OBJECTIVE_ITEM:
                case QUEST_OBJECTIVE_GAMEOBJECT:
                case QUEST_OBJECTIVE_TALKTO:
                case QUEST_OBJECTIVE_AREATRIGGER:
                case QUEST_OBJECTIVE_KILL_WITH_LABEL:
                    break;
                default:
                    continue;
            }
            SetPtr set = BuildForObjective(ctx, quest, obj);
            if (set && (!set->clusters.empty() || !set->creature_sources.empty() || !set->go_sources.empty()))
                g_by_objective.emplace(ObjKey(quest_id, obj.ID), set);
            tally(PoiMatchesObjective(poi, obj), set);
        }
    }

    TC_LOG_INFO("server.loading",
        ">> Playerbot quest objective locations: {} objectives indexed in {} ms - by POI {}, by spawns {}, "
        "by questitem {}, by loot {}, by areatrigger {}, by spell target {}, unresolved {} "
        "({} items treated as common loot, not a location)",
        uint32(g_by_objective.size()), GetMSTimeDiffToNow(start_ms),
        n_poi, n_spawn, n_qi, n_loot, n_at, n_spell, n_unresolved, uint32(ctx.common_loot_items.size()));
}

} // namespace

void EnsureQuestObjectiveLocationsBuilt()
{
    std::call_once(g_built, BuildOnce);
}

ObjectiveLocationSet const* FindQuestObjectiveLocations(uint32 quest_id, uint32 objective_id)
{
    EnsureQuestObjectiveLocationsBuilt();
    auto it = g_by_objective.find(ObjKey(quest_id, objective_id));
    return it == g_by_objective.end() ? nullptr : it->second.get();
}

bool PickQuestObjectiveCluster(uint32 quest_id, uint32 objective_id,
    uint32 bot_map, float bx, float by, ObjectiveCluster& out)
{
    ObjectiveLocationSet const* set = FindQuestObjectiveLocations(quest_id, objective_id);
    if (!set || set->clusters.empty())
        return false;

    if (auto here = set->clusters_by_map.find(bot_map); here != set->clusters_by_map.end() && !here->second.empty())
    {
        // Per-map lists are heaviest first.
        const float max_w = set->clusters[here->second.front()].weight;
        ObjectiveCluster const* best = nullptr;
        float best_cost = std::numeric_limits<float>::max();
        for (uint32 i : here->second)
        {
            ObjectiveCluster const& c = set->clusters[i];
            const float dx = c.x - bx, dy = c.y - by;
            float cost = std::max(0.f, std::sqrt(dx * dx + dy * dy) - c.radius);
            if (c.weight < 0.25f * max_w)
                cost *= 1.5f;   // thin straggler: a real camp a little further wins
            if (cost < best_cost) { best_cost = cost; best = &c; }
        }
        if (best) { out = *best; return true; }
    }

    // No cluster on the bot's map: the heaviest on an open map (the travel
    // pipeline routes there). Never an instance map: a dungeon's mobs are not
    // a solo bot's waypoint, and the travel graph cannot path into one.
    for (ObjectiveCluster const& c : set->clusters)
    {
        ::MapEntry const* me = sMapStore.LookupEntry(c.map_id);
        if (me && !me->Instanceable()) { out = c; return true; }
    }
    return false;
}

std::optional<bool> QuestObjectiveHasClusterOnMap(uint32 quest_id, uint32 objective_id, uint32 map_id)
{
    ObjectiveLocationSet const* set = FindQuestObjectiveLocations(quest_id, objective_id);
    if (!set || set->clusters.empty())
        return std::nullopt;
    return set->clusters_by_map.contains(map_id);
}

bool QuestItemDropsFromCreature(uint32 item_entry, uint32 creature_entry)
{
    EnsureQuestObjectiveLocationsBuilt();
    return g_item_from_creature.count(PairKey(item_entry, creature_entry)) > 0;
}

bool QuestItemDropsFromGameObject(uint32 item_entry, uint32 go_entry)
{
    EnsureQuestObjectiveLocationsBuilt();
    return g_item_from_go.count(PairKey(item_entry, go_entry)) > 0;
}

} // namespace Playerbot
