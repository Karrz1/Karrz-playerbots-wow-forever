// QuestObjectiveLocations - where in the world a quest objective can be done,
// derived from world data when the quest has no (matching) QuestPOI blob.
//
// Vanilla (Classic 1.60) quests mostly carry no client quest_poi, so the
// snapshot builder had no waypoint for "kill 10 X" / "collect 8 Y" / "explore
// Z". This index ports ike3's TravelMgr approach (quest objective destinations
// built from creature/gameobject spawn points, item objectives resolved through
// the loot templates that drop the item) onto PlayerbotV2:
//
//   MONSTER / TALKTO      -> spawns of the target entry + its KillCredit aliases
//   KILL_WITH_LABEL       -> spawns of every creature carrying the label
//   GAMEOBJECT            -> spawns of the GO entry
//   ITEM                  -> creature_questitem / gameobject_questitem sources,
//                            then creature_loot_template / gameobject_loot_template
//                            (direct rows and one reference level), each spawn
//                            weighted by the drop chance; when nothing drops it,
//                            the quest's start item spell target (spell focus GO
//                            or implicit-target condition entries)
//   AREATRIGGER           -> the AreaTrigger.db2 position (ObjectID, or the
//                            areatrigger_involvedrelation rows when ObjectID=-1)
//   use-start-item quest  -> (objective id 0) the start item's spell target
//
// Spawn points are clustered (50y single-linkage, per map; a chain wider than
// 150y is split into 100y cells) into areas with a centroid-nearest real spawn
// as anchor (correct floor/Z), a radius and a weight (sum of drop chances).
//
// Loot sources prefer QuestRequired loot rows and quest-class items. An item
// that is ordinary loot of a huge set of creatures (Linen / Wool / Runecloth,
// meat, hides: more than 64 droppers or 200 clusters) is common trade goods,
// not a quest location: its loot sources are dropped from the set, which then
// reads as unresolved, and no dropper becomes a quest kill target. Built ONCE on the world thread at module start
// (first world tick); read-only afterwards and safe to read from any thread.
// No DB access after the build.
//
// QuestPOI always wins: the builder only consults this index when no POI blob
// matched the objective.

#pragma once

#include "Define.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Playerbot {

enum class ObjectiveLocSource : uint8
{
    None        = 0,
    Spawn       = 1,   // direct creature/GO spawns of the objective target
    QuestItem   = 2,   // creature_questitem / gameobject_questitem (sniffed)
    Loot        = 3,   // creature/gameobject loot templates
    AreaTrigger = 4,   // AreaTrigger.db2 position
    SpellTarget = 5,   // start-item spell focus / implicit-target entries
};

enum class ObjectiveClusterKind : uint8
{
    Creature    = 0,
    GameObject  = 1,
    AreaTrigger = 2,
};

struct ObjectiveCluster
{
    uint32 map_id       = 0;
    float  x            = 0.f;   // anchor: the real spawn nearest the weighted centroid
    float  y            = 0.f;
    float  z            = 0.f;
    float  radius       = 0.f;   // max planar distance from the anchor to a member spawn
    float  weight       = 0.f;   // sum of member weights (drop chance 0..1, 1 for direct)
    uint32 count        = 0;     // member spawns
    uint32 anchor_entry = 0;     // creature/GO entry of the anchor, or AreaTrigger id
    ObjectiveClusterKind kind = ObjectiveClusterKind::Creature;
};

struct ObjectiveLocationSet
{
    ObjectiveLocSource            source = ObjectiveLocSource::None;
    std::vector<ObjectiveCluster> clusters;           // sorted heaviest first
    // map id -> indices into `clusters` (heaviest first), so a lookup scans the
    // bot's map only.
    std::unordered_map<uint32, std::vector<uint32>> clusters_by_map;
    // Entries that ARE the objective's target / item source (incl. unspawned):
    // kill / talk-to creatures, used / looted GOs. Never the spell-target
    // place entries of a use-item location.
    std::vector<uint32>           creature_sources;
    std::vector<uint32>           go_sources;
};

// Build the index. Idempotent (call_once). Called on the first world tick;
// the accessors call it too so a caller can never see a half-built index.
void EnsureQuestObjectiveLocationsBuilt();

// The location set of one objective. objective_id is QuestObjective::ID;
// objective_id 0 is the synthesized "use the quest's start item" objective.
// nullptr when the index has nothing for it.
ObjectiveLocationSet const* FindQuestObjectiveLocations(uint32 quest_id, uint32 objective_id);

// Best cluster for a bot at (bx,by) on bot_map: the nearest (planar, to the
// cluster edge) cluster on the bot's map, with thin clusters (under a quarter
// of the map's heaviest) costed 1.5x so one stray spawn does not beat a real
// camp a little further on. With no cluster on the bot's map: the heaviest
// cluster on an open (non-instance) map, so the travel pipeline can route
// cross-map. Never a cluster on an instance map (a dungeon is not a solo
// waypoint). False when the index has no such cluster for the objective.
bool PickQuestObjectiveCluster(uint32 quest_id, uint32 objective_id,
    uint32 bot_map, float bx, float by, ObjectiveCluster& out);

// Does the objective have any cluster on map_id? nullopt when the index has
// no clusters for the objective at all (dynamic / script spawns: unknown).
std::optional<bool> QuestObjectiveHasClusterOnMap(uint32 quest_id, uint32 objective_id, uint32 map_id);

// Does this creature / gameobject entry yield the quest objective item (by
// creature_questitem / gameobject_questitem OR by its loot template)? Only
// covers items that are some quest's ITEM objective.
bool QuestItemDropsFromCreature(uint32 item_entry, uint32 creature_entry);
bool QuestItemDropsFromGameObject(uint32 item_entry, uint32 go_entry);

} // namespace Playerbot
