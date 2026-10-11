#include "PortalIndex.h"
#include "RegionMapper.h"
#include "../Bot/ClassTables.h"   // IsVanillaRealm
#include "CreatureData.h"
#include "ConditionMgr.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "DBCEnums.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "GameObjectData.h"
#include "Player.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "SharedDefines.h"
#include "TransportMgr.h"
#include <G3D/Matrix3.h>
#include <G3D/Quat.h>
#include <G3D/Vector3.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_set>

namespace Playerbot::V2::Travel {

// Resolve a SPELLCASTER portal's teleport destination, following the
// FORCE_CAST / TRIGGER_SPELL indirection that modern portals use. The GO
// casts a "portal effect" spell whose own effect is a force-cast or trigger
// (not a teleport); that spell in turn casts the real TELEPORT_UNITS spell
// whose spell_target_position holds the landing. Example: Orgrimmar's
// "Portal to Undercity" GO (293684) casts 17611 (FORCE_CAST) -> 121862
// (TELEPORT_UNITS -> map 0, Undercity). The older direct-spell-only lookup
// missed every such indirect portal. Bounded recursion guards trigger cycles.
// Declared in PortalIndex.h; shared with the snapshot builder.
bool ResolvePortalSpellDest(uint32 spell_id, uint32& dest_map,
                            float& dest_x, float& dest_y, float& dest_z, int depth)
{
    if (depth > 4) return false;
    SpellInfo const* si = sSpellMgr->GetSpellInfo(spell_id, DIFFICULTY_NONE);
    if (!si) return false;

    // Direct teleport effect on this spell.
    for (SpellEffectInfo const& eff : si->GetEffects())
    {
        if (eff.Effect != SPELL_EFFECT_TELEPORT_UNITS &&
            eff.Effect != SPELL_EFFECT_TELEPORT_WITH_SPELL_VISUAL_KIT_LOADING_SCREEN)
            continue;
        if (SpellTargetPosition const* tp =
                sSpellMgr->GetSpellTargetPosition(spell_id, eff.EffectIndex))
        {
            dest_map = tp->GetMapId();
            dest_x   = tp->GetPositionX();
            dest_y   = tp->GetPositionY();
            dest_z   = tp->GetPositionZ();
            return true;
        }
    }

    // Indirect: the spell force-casts / triggers another spell that teleports.
    for (SpellEffectInfo const& eff : si->GetEffects())
    {
        switch (eff.Effect)
        {
            case SPELL_EFFECT_TRIGGER_SPELL:
            case SPELL_EFFECT_TRIGGER_SPELL_WITH_VALUE:
            case SPELL_EFFECT_FORCE_CAST:
            case SPELL_EFFECT_FORCE_CAST_WITH_VALUE:
            case SPELL_EFFECT_FORCE_CAST_2:
                if (uint32 trig = eff.TriggerSpell)
                    if (ResolvePortalSpellDest(trig, dest_map, dest_x, dest_y, dest_z, depth + 1))
                        return true;
                break;
            default:
                break;
        }
    }
    return false;
}

bool PortalIndex::Initialize()
{
    _anchors.clear();
    _anchors.reserve(256);
    _route_leader.clear();
    std::vector<ShuttleSpawn> shuttle_spawns;

    auto const& spawns = sObjectMgr->GetAllGameObjectData();
    for (auto const& [spawn_id, data] : spawns)
    {
        GameObjectTemplate const* tmpl = sObjectMgr->GetGameObjectTemplate(data.id);
        if (!tmpl) continue;

        if (tmpl->type == GAMEOBJECT_TYPE_SPELLCASTER)
        {
            // Portal: GO triggers a spell whose effect[0] is a teleport.
            // The destination is in spell_target_position. Most static
            // SPELLCASTER GOs are altar / event triggers without a
            // teleport effect — they resolve to nullptr here and we skip.
            const uint32 spell_id = tmpl->spellCaster.spell;
            if (!spell_id) continue;
            uint32 dest_map = 0;
            float  dest_x = 0.f, dest_y = 0.f, dest_z = 0.f;
            // Resolve the teleport destination, following FORCE_CAST /
            // TRIGGER_SPELL indirection (modern portals cast a force-cast
            // spell that in turn casts the real teleport). dest_x/dest_y are
            // the arrival point so BuildPortalEdges can pick the landing node
            // nearest the goal rather than the first best-rank.
            if (!ResolvePortalSpellDest(spell_id, dest_map, dest_x, dest_y, dest_z))
                continue;
            // Do NOT test `!dest_map`: map 0 (Eastern Kingdoms) is a valid
            // destination, and treating 0 as "unset" silently dropped every
            // portal that lands in EK (Undercity, Twilight Highlands, ...).
            // Only same-map "portals" (altars / effect casters) are skipped.
            if (dest_map == data.mapId) continue;
            PortalAnchor a;
            a.source_map = data.mapId;
            a.dest_map   = dest_map;
            a.x = data.spawnPoint.GetPositionX();
            a.y = data.spawnPoint.GetPositionY();
            a.z = data.spawnPoint.GetPositionZ();
            a.dest_x = dest_x;
            a.dest_y = dest_y;
            a.go_entry     = data.id;
            a.kind         = PortalAnchor::Kind::Portal;
            a.condition_id = tmpl->spellCaster.conditionID1;
            _anchors.push_back(a);
        }
        else if (tmpl->type == GAMEOBJECT_TYPE_TRANSPORT)
        {
            // Type 11 — local transports (elevators, lifts, tram cars). These
            // ARE in `gameobject` so their spawnPoint is meaningful. Vertical
            // lifts are ElevatorIndex's (Elevator edges). A car that travels
            // HORIZONTALLY between far-apart stations (the Deeprun Tram) is a
            // same-map ride; AddShuttleAnchors below picks those out of the
            // animation data.
            shuttle_spawns.push_back({ data.mapId, data.id, data.spawnId,
                data.spawnPoint.GetPositionX(), data.spawnPoint.GetPositionY(),
                data.spawnPoint.GetPositionZ() });
            continue;
        }
    }

    // ---- Continental transports (ships / zeppelins, GO type 15) ----
    // These are NOT in `gameobject`; their spawns live in the separate
    // `transports` SQL table and they only have positions while running
    // their path animation. Static dock waypoints come from the path's
    // TaxiPathNode entries with TAXI_PATH_NODE_FLAG_STOP — those are
    // the rendezvous coordinates the bot needs to walk to in order to
    // board.
    //
    // We iterate every gameobject_template with type=15, look up its
    // taxi path, walk the path nodes, and for every (STOP_node A, STOP_
    // node B) pair add a (source=A.continent, dest=B.continent, x/y/z=A.Loc)
    // anchor. Pairs on the SAME continent are same-map rides (Classic: the
    // Moonspray Auberdine <-> Rut'theran, the Feathermoon ferry, the Undercity
    // <-> Grom'gol zeppelin): those anchors also carry ride_seconds, and the
    // travel graph lands them on the reverse anchor's dock.
    {
        size_t pre_continental = _anchors.size();
        // Only ACTUALLY-SPAWNED transports (rows in the `transports` table) run
        // in the world. The gameobject_template list still carries decommissioned
        // routes — e.g. the Undercity zeppelin 164871 "The Thundercaller", whose
        // Org→Tirisfal route was removed when Undercity became a post-BfA ruin.
        // Building an anchor for it sent a Tirisfal-bound bot (Somi) to the dead
        // tower to wait forever for a zeppelin that never spawns (verified live:
        // 164871 absent from `transports`; 0 sightings over minutes while the
        // running 175080/186238 cycled normally). Gate on the spawn table so the
        // travel graph only routes through transports that physically exist.
        std::unordered_set<uint32> spawned_transport_entries;
        if (QueryResult res = WorldDatabase.Query("SELECT DISTINCT entry FROM transports"))
        {
            do { spawned_transport_entries.insert((*res)[0].GetUInt32()); } while (res->NextRow());
        }
        TC_LOG_INFO("server.loading",
            "[PlayerbotV2 PortalIndex] {} spawned transports gate continental anchors.",
            spawned_transport_entries.size());
        // Vanilla: NPC spawns (any npcflag) by map, for the zeppelin-deck
        // wait spot below. An NPC 5-30y BELOW a transport stop and within 40y
        // of it is standing on that stop's boarding deck (the zeppelin master
        // on the tower top); a sea-level ship stop has no such NPC under it
        // (STOP z ~0, nothing with an npcflag stands 5-30y under water), so
        // boats keep their raw stop.
        const bool vanilla = ::Playerbot::IsVanillaRealm();
        struct StandSpot { float x, y, z; };
        std::unordered_map<uint32, std::vector<StandSpot>> npc_spots;
        if (vanilla)
        {
            for (auto const& [spawn_id, cdata] : sObjectMgr->GetAllCreatureData())
            {
                CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(cdata.id);
                if (!ct) continue;
                const uint64 flags = cdata.npcflag ? *cdata.npcflag : ct->npcflag;
                if (flags == 0) continue;
                npc_spots[cdata.mapId].push_back({ cdata.spawnPoint.GetPositionX(),
                    cdata.spawnPoint.GetPositionY(), cdata.spawnPoint.GetPositionZ() });
            }
        }
        auto ElevatedStandSpotNear = [&npc_spots](uint32 map, float sx, float sy, float sz) -> StandSpot const*
        {
            auto it = npc_spots.find(map);
            if (it == npc_spots.end()) return nullptr;
            // The zeppelin master stands on the boarding deck, ~17y under the
            // STOP node; other NPCs of the tower (a flight master or guard at its
            // foot) are further down. Prefer the height gap nearest the deck's,
            // then the planar distance.
            constexpr float kDeckGap = 17.0f;
            StandSpot const* best = nullptr;
            float best_gap = std::numeric_limits<float>::max();
            float best_dsq = 40.0f * 40.0f;
            for (StandSpot const& n : it->second)
            {
                const float dz = sz - n.z;
                if (dz < 5.0f || dz > 30.0f) continue;
                const float dx = n.x - sx, dy = n.y - sy;
                const float dsq = dx * dx + dy * dy;
                if (dsq > 40.0f * 40.0f) continue;
                const float gap = std::fabs(dz - kDeckGap);
                if (gap < best_gap || (gap == best_gap && dsq < best_dsq))
                {
                    best_gap = gap;
                    best_dsq = dsq;
                    best = &n;
                }
            }
            return best;
        };
        auto const& templates = sObjectMgr->GetGameObjectTemplates();
        for (auto const& [entry, tmpl] : templates)
        {
            if (tmpl.type != GAMEOBJECT_TYPE_MAP_OBJ_TRANSPORT) continue;
            if (!spawned_transport_entries.count(entry)) continue;  // decommissioned / never spawned
            const uint32 path_id = tmpl.moTransport.taxiPathID;
            if (path_id == 0 || path_id >= sTaxiPathNodesByPath.size()) continue;
            auto const& nodes = sTaxiPathNodesByPath[path_id];
            // Collect STOP-flagged nodes (the docks where the transport
            // pauses for boarding/disembark). Continental ships have
            // 2-4 stops; a single per-map STOP is the bot's rendezvous.
            std::vector<TaxiPathNodeEntry const*> stops;
            stops.reserve(8);
            for (TaxiPathNodeEntry const* node : nodes)
            {
                if (!node) continue;
                if ((node->Flags & TAXI_PATH_NODE_FLAG_STOP) == 0) continue;
                stops.push_back(node);
            }
            if (stops.size() < 2) continue;
            // Server-side timing of each dock pause (TransportTemplate, built by
            // TransportMgr from this same taxi path), for the same-map ride cost.
            // A pause is matched to its STOP node by 2D position on the same map.
            struct Pause { uint32 arrive_ms, delay_ms, map; float x, y; };
            std::vector<Pause> pauses;
            uint32 period_ms = 0;
            if (TransportTemplate const* ttpl = sTransportMgr->GetTransportTemplate(entry))
            {
                period_ms = ttpl->TotalPathTime;
                if (period_ms > 0)
                    for (TransportPathLeg const& leg : ttpl->PathLegs)
                        for (TransportPathSegment const& seg : leg.Segments)
                        {
                            if (seg.Delay == 0) continue;
                            if (Optional<Position> pos = ttpl->ComputePosition(seg.SegmentEndArrivalTimestamp, nullptr, nullptr))
                                pauses.push_back({ seg.SegmentEndArrivalTimestamp, seg.Delay, leg.MapId,
                                                   pos->GetPositionX(), pos->GetPositionY() });
                        }
            }
            auto PauseFor = [&pauses](TaxiPathNodeEntry const* n) -> Pause const*
            {
                Pause const* best = nullptr;
                float best_dsq = 50.0f * 50.0f;
                for (Pause const& pz : pauses)
                {
                    if (pz.map != n->ContinentID) continue;
                    const float dx = pz.x - n->Loc.X, dy = pz.y - n->Loc.Y;
                    const float dsq = dx * dx + dy * dy;
                    if (dsq < best_dsq) { best_dsq = dsq; best = &pz; }
                }
                return best;
            };
            // DIAG (2026-06-02): dump the REAL stop-waypoint positions per live
            // 12.0 transport. These are the coords bots are sent to in order to
            // board — we need to see which sit in water (z near sea level ~0) vs
            // on a pier/platform, to design a general dock-approach that doesn't
            // strand bots swimming. One line per stop; reads from Server.log.
            for (TaxiPathNodeEntry const* sn : stops)
                TC_LOG_INFO("server.loading",
                    "[dock_dump] entry={} '{}' path={} stop map={} pos=({:.1f},{:.1f},{:.1f})",
                    entry, tmpl.name, path_id, sn->ContinentID,
                    sn->Loc.X, sn->Loc.Y, sn->Loc.Z);
            for (TaxiPathNodeEntry const* src_node : stops)
            {
                for (TaxiPathNodeEntry const* dst_node : stops)
                {
                    if (src_node == dst_node) continue;
                    const bool same_map = (src_node->ContinentID == dst_node->ContinentID);
                    PortalAnchor a;
                    a.source_map = src_node->ContinentID;
                    a.dest_map   = dst_node->ContinentID;
                    a.x = src_node->Loc.X;
                    a.y = src_node->Loc.Y;
                    a.z = src_node->Loc.Z;
                    a.stop_x = src_node->Loc.X;
                    a.stop_y = src_node->Loc.Y;
                    // Curated pier WAIT override (operator in-client ground
                    // truth). Boat STOP waypoints sit on the open water surface
                    // (z~0), so a bot walking to the raw anchor ends up standing
                    // ON the water instead of on the pier where players board.
                    // Snap the WAIT position to the real pier; dest_x/y (arrival
                    // on the far map) stays the STOP, and the docked ship's
                    // board-scan still reaches the bot on the pier (State_Idle
                    // boards within a generous transport range). Matched to the
                    // STOP by 2D proximity on the same map.
                    {
                        // Operator-provided pier WAIT spots, matched to a boat STOP
                        // by 2D proximity (60y) on the same map.
                        struct PierOverride { uint32 map; float near_x, near_y, wx, wy, wz; };
                        static constexpr PierOverride kPiers[] = {
                            // Stormwind Harbor — The Bravery (Boralus / Dragon Isles).
                            { 0,   -8650.0f, 1346.0f, -8644.78f, 1328.72f, 5.54f },
                            // Stormwind Harbor — Stormwind's Pride (Valiance Keep / NR).
                            { 0,   -8288.0f, 1424.0f, -8291.01f, 1408.10f, 4.70f },
                            // Valiance Keep (Northrend) — Stormwind boat dock.
                            { 571,  2218.0f, 5119.0f,  2229.76f, 5130.87f, 5.34f },
                            // Ratchet (Kalimdor) — Booty Bay boat dock.
                            { 1,    -994.0f, -3827.0f,  -995.53f, -3829.46f, 5.61f },
                            // Booty Bay (Eastern Kingdoms) — Ratchet boat dock.
                            { 0,  -14281.0f,  556.0f, -14282.50f,  563.47f, 7.84f },
                            // Waking Shores (Dragon Isles) — Stormwind boat dock.
                            { 2444, 3736.0f, -1901.0f,  3736.29f, -1901.36f, 5.88f },
                            // Boralus Harbor (Kul Tiras) — Stormwind boat dock.
                            { 1643, 1022.0f,  -657.0f,  1022.74f,  -657.06f, 6.46f },
                            // Orgrimmar has TWO zeppelin towers (~64y apart); raw
                            // STOPs are the unreachable z152 float points, wait on
                            // the walkable upper platforms at z135.
                            // Tower 1 -> Tirisfal/Undercity.
                            { 1,    1833.0f, -4391.0f,  1841.36f, -4392.05f, 135.23f },
                            // Tower 2 -> Grom'gol/Stranglethorn.
                            { 1,    1880.0f, -4435.0f,  1868.68f, -4418.04f, 135.23f },
                        };
                        // Tight match radius (30y): boat STOPs are far apart, but the
                        // two Org zeppelin STOPs are only ~64y apart, so a wide radius
                        // would route both towers' bots to one platform.
                        bool curated = false;
                        for (auto const& po : kPiers)
                        {
                            if (a.source_map != po.map) continue;
                            const float pdx = a.x - po.near_x, pdy = a.y - po.near_y;
                            if (pdx * pdx + pdy * pdy <= 30.0f * 30.0f)
                            { a.x = po.wx; a.y = po.wy; a.z = po.wz; curated = true; break; }
                        }
                        // Vanilla realm: the curated piers above are retail ground
                        // truth (Stormwind Harbor, the Cata Orgrimmar towers); the
                        // 1.12 zeppelin towers (Durotar, Tirisfal, Grom'gol) have
                        // none, and a zeppelin STOP is the hull's hover point ~17y
                        // above the tower deck (ike3 TravelNode: zeppelin exit =
                        // stop z - 17), which no bot can stand on. Wait where the
                        // tower's zeppelin master stands instead: the NPC spawn is
                        // walkable ground on the boarding deck by construction.
                        if (!curated && vanilla)
                            if (StandSpot const* spot = ElevatedStandSpotNear(a.source_map, a.x, a.y, a.z))
                            {
                                TC_LOG_INFO("server.loading",
                                    "[PlayerbotV2 PortalIndex] Classic: transport {} stop ({:.1f},{:.1f},{:.1f}) map {} "
                                    "waits on the deck at ({:.1f},{:.1f},{:.1f})",
                                    entry, a.x, a.y, a.z, a.source_map, spot->x, spot->y, spot->z);
                                a.x = spot->x; a.y = spot->y; a.z = spot->z;
                            }
                    }
                    // T-P2b: arrival dock coords on the destination map.
                    a.dest_x = dst_node->Loc.X;
                    a.dest_y = dst_node->Loc.Y;
                    a.go_entry     = entry;
                    a.kind         = PortalAnchor::Kind::Transport;
                    a.condition_id = 0;
                    if (same_map)
                    {
                        // Ride cost: average wait (half the period) + time from
                        // leaving this dock to arriving at the other one.
                        Pause const* ps = PauseFor(src_node);
                        Pause const* pd = PauseFor(dst_node);
                        if (ps && pd && period_ms > 0)
                        {
                            const uint32 depart = (ps->arrive_ms + ps->delay_ms) % period_ms;
                            const uint32 ride   = (pd->arrive_ms + period_ms - depart) % period_ms;
                            a.ride_seconds = (float(period_ms) * 0.5f + float(ride)) / 1000.0f;
                        }
                        else
                            TC_LOG_ERROR("server.loading",
                                "[PlayerbotV2 PortalIndex] same-map transport {} '{}': no TransportTemplate pause at "
                                "stop ({:.1f},{:.1f}) or ({:.1f},{:.1f}); the ride uses the flat ship cost",
                                entry, tmpl.name, src_node->Loc.X, src_node->Loc.Y, dst_node->Loc.X, dst_node->Loc.Y);
                        const float sdx = src_node->Loc.X - dst_node->Loc.X, sdy = src_node->Loc.Y - dst_node->Loc.Y;
                        TC_LOG_INFO("server.loading",
                            "[PlayerbotV2 PortalIndex] same-map transport {} '{}' map {}: dock ({:.1f},{:.1f},{:.1f}) -> "
                            "({:.1f},{:.1f}) {:.0f}y apart, ride cost {:.0f}s",
                            entry, tmpl.name, a.source_map, a.x, a.y, a.z, a.dest_x, a.dest_y,
                            std::sqrt(sdx * sdx + sdy * sdy), a.ride_seconds);
                        _route_leader[entry] = entry;
                    }
                    _anchors.push_back(a);
                }
            }
        }
        TC_LOG_INFO("server.loading",
            "[PlayerbotV2 PortalIndex] Added {} continental-transport dock anchors.",
            _anchors.size() - pre_continental);
    }

    AddShuttleAnchors(shuttle_spawns);

    // Tag every anchor with its source/destination REGION (RegionMapper), so
    // FindNearest can route to the correct landmass on physically-split maps
    // (530 Outland/Azuremyst/Eversong; 1 Kalimdor/Teldrassil). dest_region is
    // left 0 when the landing coords are unknown (dest_x==0&&dest_y==0) — the
    // FindNearest fallback treats those as "any region" so a route is never lost.
    {
        uint32 region_tagged = 0;
        for (PortalAnchor& a : _anchors)
        {
            a.source_region = RegionForPosition(a.source_map, a.x, a.y);
            a.dest_region   = (a.dest_x != 0.f || a.dest_y != 0.f)
                ? RegionForPosition(a.dest_map, a.dest_x, a.dest_y)
                : 0u;
            if (a.source_region != 0 || a.dest_region != 0) ++region_tagged;
        }
        TC_LOG_INFO("server.loading",
            "[PlayerbotV2 PortalIndex] Region-tagged anchors: {} in a non-default region.",
            region_tagged);
    }

    _anchors.shrink_to_fit();
    BuildGraph();
    _initialized = true;
    TC_LOG_INFO("server.loading",
        "[PlayerbotV2 PortalIndex] Built {} anchors from {} gameobject spawns; "
        "graph has {} source maps.",
        _anchors.size(), spawns.size(), _adjacency.size());
    return true;
}

void PortalIndex::AddShuttleAnchors(std::vector<ShuttleSpawn> const& spawns)
{
    // Horizontal shuttles: type-11 GOs whose client TransportAnimation carries
    // the car between stations far apart on one map (the Deeprun Tram, map 369).
    // A station is where the animation DWELLS: two consecutive frames at the
    // same position (the rule Karrz's ike3 TravelNode.cpp generateTransportNodes
    // uses to place its tram nodes). A vertical lift also dwells, but its stops
    // share X/Y; requiring the stations to be kMinStationSpacing apart
    // horizontally leaves lifts to ElevatorIndex.
    //
    // World position of a frame = frame * ParentRotation + spawn position, the
    // same transform GameObject.cpp's Transport::Update applies to move the car.
    //
    // Riders stand on the car floor below the animated pivot. The only model
    // with a known floor offset is the Deeprun Tram car "Subway" (displayId
    // 3831): ike3 generateTransportNodes puts its exit 10y below the pivot.
    // Unknown models keep the pivot height and are logged.
    constexpr uint32 kSubwayDisplayId   = 3831;
    constexpr float  kSubwayFloorOffset = -10.0f;
    constexpr float  kMinStationSpacing = 100.0f;
    constexpr float  kSameStationYds    = 5.0f;

    struct Station { float x, y, z; uint32 arrive_ms, depart_ms; };
    struct Ride
    {
        uint32 map = 0, entry = 0, period_ms = 0;
        float  ax = 0.f, ay = 0.f, az = 0.f, bx = 0.f, by = 0.f, bz = 0.f;
        uint32 ride_ms = 0;
        float  floor_offset = 0.f;
    };
    std::vector<Ride> rides;

    for (ShuttleSpawn const& sp : spawns)
    {
        GameObjectTemplate const* tmpl = sObjectMgr->GetGameObjectTemplate(sp.entry);
        if (!tmpl || tmpl->type != GAMEOBJECT_TYPE_TRANSPORT) continue;
        TransportAnimation const* anim = sTransportMgr->GetTransportAnimInfo(sp.entry);
        if (!anim || anim->Path.size() < 2 || anim->TotalTime == 0) continue;

        QuaternionData rot;
        if (GameObjectAddon const* addon = sObjectMgr->GetGameObjectAddon(sp.spawn_id))
            rot = addon->ParentRotation;
        const G3D::Matrix3 path_rotation = G3D::Quat(rot.x, rot.y, rot.z, rot.w).toRotationMatrix();

        std::vector<Station> stations;
        bool   have_prev = false;
        G3D::Vector3 prev;
        uint32 prev_t = 0;
        for (auto const& [t, frame] : anim->Path)
        {
            if (!frame) continue;
            G3D::Vector3 w = G3D::Vector3(frame->Pos.X, frame->Pos.Y, frame->Pos.Z) * path_rotation;
            w += G3D::Vector3(sp.x, sp.y, sp.z);
            if (have_prev && (w - prev).length() < 0.1f)
            {
                if (!stations.empty() && stations.back().depart_ms == prev_t &&
                    std::fabs(stations.back().x - w.x) < kSameStationYds &&
                    std::fabs(stations.back().y - w.y) < kSameStationYds)
                    stations.back().depart_ms = t;              // dwell continues
                else
                    stations.push_back({ w.x, w.y, w.z, prev_t, t });
            }
            prev = w; prev_t = t; have_prev = true;
        }
        // A dwell that runs across the end of the cycle shows up twice (at the
        // last frames and at the first): fold it into one.
        if (stations.size() >= 2)
        {
            Station const& f = stations.front();
            Station const& l = stations.back();
            if (std::fabs(f.x - l.x) < kSameStationYds && std::fabs(f.y - l.y) < kSameStationYds)
            {
                stations.front().arrive_ms = l.arrive_ms;
                stations.pop_back();
            }
        }
        if (stations.size() < 2) continue;

        float floor_offset = 0.f;
        if (tmpl->displayId == kSubwayDisplayId)
            floor_offset = kSubwayFloorOffset;

        const uint32 period = anim->TotalTime;
        for (size_t i = 0; i < stations.size(); ++i)
        {
            Station const& a = stations[i];
            Station const& b = stations[(i + 1) % stations.size()];
            const float dx = a.x - b.x, dy = a.y - b.y;
            if (dx * dx + dy * dy < kMinStationSpacing * kMinStationSpacing) continue;
            Ride r;
            r.map = sp.map; r.entry = sp.entry; r.period_ms = period;
            r.ax = a.x; r.ay = a.y; r.az = a.z;
            r.bx = b.x; r.by = b.y; r.bz = b.z;
            r.ride_ms = (b.arrive_ms + period - (a.depart_ms % period)) % period;
            r.floor_offset = floor_offset;
            rides.push_back(r);
        }
    }

    // Cars that run between the same two stations (one track) form one route:
    // a rider boards whichever of them is in the station. Group by station pair
    // (either direction); the leader is the lowest car entry of the group.
    auto SameSpot = [&](float x1, float y1, float x2, float y2)
    { return std::fabs(x1 - x2) < kSameStationYds && std::fabs(y1 - y2) < kSameStationYds; };
    auto SamePair = [&](Ride const& r, Ride const& o)
    {
        return r.map == o.map &&
            ((SameSpot(r.ax, r.ay, o.ax, o.ay) && SameSpot(r.bx, r.by, o.bx, o.by)) ||
             (SameSpot(r.ax, r.ay, o.bx, o.by) && SameSpot(r.bx, r.by, o.ax, o.ay)));
    };
    std::vector<bool> done(rides.size(), false);
    size_t added = 0;
    for (size_t i = 0; i < rides.size(); ++i)
    {
        if (done[i]) continue;
        std::vector<size_t> group;
        for (size_t j = i; j < rides.size(); ++j)
            if (!done[j] && SamePair(rides[i], rides[j]))
            {
                group.push_back(j);
                done[j] = true;
            }
        uint32 leader = rides[i].entry;
        std::unordered_set<uint32> cars;
        for (size_t j : group)
        {
            leader = std::min(leader, rides[j].entry);
            cars.insert(rides[j].entry);
        }
        for (uint32 e : cars)
            _route_leader[e] = leader;

        // One anchor per direction. The fastest car sets the ride time; the
        // wait is the period shared out over the cars on the track.
        for (int dir = 0; dir < 2; ++dir)
        {
            Ride const* best = nullptr;
            for (size_t j : group)
            {
                Ride const& r = rides[j];
                const bool fwd = SameSpot(r.ax, r.ay, rides[i].ax, rides[i].ay);
                if ((dir == 0) != fwd) continue;
                if (!best || r.ride_ms < best->ride_ms) best = &r;
            }
            if (!best) continue;
            PortalAnchor a;
            a.source_map = best->map;
            a.dest_map   = best->map;
            a.x = best->ax; a.y = best->ay; a.z = best->az + best->floor_offset;
            a.stop_x = best->ax; a.stop_y = best->ay;
            a.dest_x = best->bx; a.dest_y = best->by;
            a.go_entry     = leader;
            a.kind         = PortalAnchor::Kind::Transport;
            a.condition_id = 0;
            a.ride_seconds = (float(best->period_ms) * 0.5f / float(cars.size()) + float(best->ride_ms)) / 1000.0f;
            _anchors.push_back(a);
            ++added;
            TC_LOG_INFO("server.loading",
                "[PlayerbotV2 PortalIndex] shuttle route map {} (car {} + {} more): station ({:.1f},{:.1f},{:.1f}) -> "
                "({:.1f},{:.1f}), ride cost {:.0f}s{}",
                a.source_map, leader, cars.size() - 1, a.x, a.y, a.z, a.dest_x, a.dest_y, a.ride_seconds,
                best->floor_offset == 0.f ? " (car floor height unknown: rider stands at the pivot height)" : "");
        }
    }
    TC_LOG_INFO("server.loading",
        "[PlayerbotV2 PortalIndex] Added {} shuttle (tram) station anchors from {} type-11 spawns.",
        added, spawns.size());
}

bool PortalIndex::IsSameMapTransport(uint32 go_entry) const
{
    return _route_leader.count(go_entry) != 0;
}

bool PortalIndex::ServesSameRoute(uint32 route_entry, uint32 candidate) const
{
    if (route_entry == candidate) return true;
    auto a = _route_leader.find(route_entry);
    auto b = _route_leader.find(candidate);
    return a != _route_leader.end() && b != _route_leader.end() && a->second == b->second;
}

void PortalIndex::BuildGraph()
{
    _adjacency.clear();
    // Memoization is keyed off the adjacency table; rebuilding the
    // graph invalidates every cached lookup.
    _next_hop_cache.clear();
    // Each anchor contributes one directed edge source_map → dest_map.
    // De-dup destinations per source so BFS visits each adjacent map
    // once. Static portals tend to be one-way (Stormwind → Dalaran is a
    // separate portal from Dalaran → Stormwind), so we don't synthesize
    // a reverse edge — if the world has both directions wired the
    // adjacency captures both naturally.
    for (PortalAnchor const& a : _anchors)
    {
        if (a.source_map == a.dest_map) continue;
        auto& adj = _adjacency[a.source_map];
        bool seen = false;
        for (uint32 m : adj) if (m == a.dest_map) { seen = true; break; }
        if (!seen) adj.push_back(a.dest_map);
    }
}

uint32 PortalIndex::NextHopMap(uint32 from_map, uint32 to_map) const
{
    if (from_map == to_map) return kNoMap;
    if (_adjacency.empty()) return kNoMap;

    // Memoization cache. The adjacency graph is static after Initialize() so any
    // (from, to) → next_hop pair is permanent. Without this, the BFS allocated
    // three containers per call — at 2000 bots with off-map quest goals common,
    // hundreds of heap allocs per snapshot tick. Cache hit avoids all of that.
    //
    // CONCURRENCY: NextHopMap is called from the Phase 4 parallel snapshot-build
    // workers (BotSnapshotBuilder::Build on up to 24 threads). The cache must be
    // synchronised — an unsynchronised emplace from two workers corrupts the
    // bucket list and spins emplace forever. shared_lock for the hit fast path
    // (full read parallelism); the BFS for a miss runs OUTSIDE the lock (reads
    // only the immutable _adjacency); a unique_lock publishes the result.
    const uint64 cache_key = (uint64(from_map) << 32) | uint64(to_map);
    {
        std::shared_lock<std::shared_mutex> rlk(_next_hop_mtx);
        if (auto cit = _next_hop_cache.find(cache_key); cit != _next_hop_cache.end())
            return cit->second;
    }

    const uint32 result = ComputeNextHopUncached(from_map, to_map);

    // Publish. emplace is a no-op if another worker computed the same key in the
    // race window between our shared-lock miss and this unique-lock — the value
    // is deterministic for a fixed graph, so either insertion is correct.
    {
        std::unique_lock<std::shared_mutex> wlk(_next_hop_mtx);
        _next_hop_cache.emplace(cache_key, result);
    }
    return result;
}

uint32 PortalIndex::ComputeNextHopUncached(uint32 from_map, uint32 to_map) const
{
    // Direct edge fast-path: if there is any anchor from→to, the greedy rule
    // already finds it; report to_map so the caller's logic stays consistent
    // ("walk toward an anchor whose dest_map == NextHopMap result"). Avoids a
    // BFS sweep when not needed.
    auto it = _adjacency.find(from_map);
    if (it == _adjacency.end())
        return kNoMap;
    for (uint32 m : it->second)
        if (m == to_map)
            return to_map;

    // BFS from from_map; track the FIRST hop taken on each path so we can return
    // it when the goal is reached. Cap traversal as a safety bound; real WoW
    // topology is at most ~3-4 hops between any two maps.
    std::unordered_map<uint32, uint32> first_hop;  // map_visited → first_hop_taken
    std::unordered_set<uint32> visited;
    std::queue<uint32> queue;
    visited.insert(from_map);
    for (uint32 hop : it->second)
    {
        if (visited.count(hop)) continue;
        visited.insert(hop);
        first_hop[hop] = hop;
        queue.push(hop);
    }
    constexpr size_t kMaxIterations = 1024;
    size_t iter = 0;
    while (!queue.empty() && iter < kMaxIterations)
    {
        ++iter;
        uint32 cur = queue.front();
        queue.pop();
        if (cur == to_map)
            return first_hop[cur];
        auto it2 = _adjacency.find(cur);
        if (it2 == _adjacency.end()) continue;
        for (uint32 nxt : it2->second)
        {
            if (visited.count(nxt)) continue;
            visited.insert(nxt);
            first_hop[nxt] = first_hop[cur];   // inherit the first-hop tag
            queue.push(nxt);
        }
    }
    return kNoMap;  // no route
}

PortalAnchor const* PortalIndex::FindNearest(
    Player const* bot,
    uint32 source_map, uint32 dest_map,
    float bot_x, float bot_y,
    uint32 dest_region,
    float goal_x, float goal_y) const
{
    // Region filter only matters on a map that is physically split into
    // multiple unreachable landmasses (530, 1, ...). On normal maps every
    // anchor's dest_region is 0 and the filter is a no-op.
    const bool region_filter =
        dest_region != kRegionAny && MapHasMultipleRegions(dest_map);
    // When a goal position is known, ALSO track the dock whose ARRIVAL is
    // nearest the goal. Eastern Kingdoms (map 0) is one connected continent so
    // the region filter above is a no-op there, yet the Org tower's two map-0
    // zeppelins land a continent apart (Tirisfal vs Stranglethorn). Picking by
    // board-distance alone sent a Tirisfal bot to whichever was nearest to board
    // (Grom'gol). Preferring the nearest-arrival dock fixes that without
    // affecting single-dock destinations.
    const bool use_goal = (goal_x != 0.f || goal_y != 0.f);
    // Source landmass: an anchor the bot can walk to stands in the bot's own
    // region of the source map. On Teldrassil every Kalimdor dock (Auberdine,
    // Theramore, Ratchet) is across the sea; offering one sends the bot walking
    // into the water. Only same-region anchors are offered; getting off the
    // island first is a taxi/teleport hop the travel graph (UnifiedTravelGraph)
    // plans, e.g. Rut'theran -> Auberdine, after which the dock is in reach.
    const bool source_split = MapHasMultipleRegions(source_map);
    const uint32 bot_region = source_split ? RegionForPosition(source_map, bot_x, bot_y) : 0u;
    PortalAnchor const* best = nullptr;
    float best_distSq = std::numeric_limits<float>::max();
    PortalAnchor const* best_by_goal = nullptr;
    float best_goalSq = std::numeric_limits<float>::max();
    for (PortalAnchor const& a : _anchors)
    {
        if (a.source_map != source_map) continue;
        if (a.dest_map   != dest_map)   continue;
        if (source_split && a.source_region != bot_region) continue;
        // Region gate: on a split dest map, the anchor must LAND in the goal's
        // region. Anchors with unknown landing coords (dest_x==0&&dest_y==0,
        // so dest_region couldn't be resolved) are accepted as a fallback so we
        // never drop the only available route into a region.
        if (region_filter && (a.dest_x != 0.f || a.dest_y != 0.f) &&
            a.dest_region != dest_region)
            continue;
        // Faction / level / rep gate. condition_id == 0 means anyone
        // can use; otherwise the bot must satisfy the PlayerCondition.
        // ConditionMgr::IsPlayerMeetingCondition looks up the entry,
        // returns true on missing IDs (logs a warning), so unknown
        // condition rows are treated as "no filter" — matches server
        // behaviour at GO use time. Skipping this check would route a
        // Horde bot toward an Alliance-only Stormwind portal; the bot
        // would arrive but the GO would be invisible (or use would
        // bounce server-side).
        if (a.condition_id != 0 && bot != nullptr &&
            !ConditionMgr::IsPlayerMeetingCondition(bot, a.condition_id))
            continue;
        const float dx = a.x - bot_x;
        const float dy = a.y - bot_y;
        const float dsq = dx * dx + dy * dy;
        if (dsq < best_distSq)
        {
            best_distSq = dsq;
            best = &a;
        }
        if (use_goal && (a.dest_x != 0.f || a.dest_y != 0.f))
        {
            const float gdx = a.dest_x - goal_x;
            const float gdy = a.dest_y - goal_y;
            const float gdsq = gdx * gdx + gdy * gdy;
            if (gdsq < best_goalSq)
            {
                best_goalSq = gdsq;
                best_by_goal = &a;
            }
        }
    }
    // Prefer the dock that LANDS nearest the goal (when arrivals are known);
    // otherwise the dock nearest to BOARD. For a single-dock destination these
    // resolve to the same anchor, so existing routes are unchanged.
    return best_by_goal ? best_by_goal : best;
}

} // namespace Playerbot::V2::Travel
