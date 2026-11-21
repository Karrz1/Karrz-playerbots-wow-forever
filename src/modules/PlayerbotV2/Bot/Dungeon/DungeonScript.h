// DungeonScript — per-dungeon override hooks for the autonomous run
// system. Each registered script is keyed by `map_id` + (optional)
// difficulty; the Idle dungeon rules consult `DungeonScriptMgr` once
// per tick for advice on which adds to focus, which casts to always
// interrupt, which trash to CC, and which auras to step out from.
//
// Generic dungeon logic (tank pulls IsDungeonBoss, DPS assists tank,
// healer keeps tank up, all interrupt + CC marked targets) clears
// 80%+ of dungeons without any per-dungeon override. Scripts are
// pure overrides for the bespoke 20% — encounters where the optimal
// play is non-obvious from the snapshot alone (Cobrahn snake-form
// shift in WC, Greenskin's healer adds in Deadmines, etc.).

#pragma once

#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace Playerbot {

class BotSnapshotView;

// Advice bundle returned by a DungeonScript per tick. Empty fields
// mean "no opinion" — the bot logic uses generic logic. The advice struct
// is intentionally small and value-typed so the registry can hand
// it out by-value to the bot worker without locking.
struct DungeonAdvice
{
    // Creature_template entries the bot should pull first / focus
    // first when multiple targets are nearby. Higher index = higher
    // priority (front of vector = top priority).
    std::vector<uint32_t> high_priority_kill_entries;

    // Spell ids that MUST be interrupted whenever they're seen mid-
    // cast — overrides the generic "any interruptible cast" logic
    // so the bot doesn't waste interrupts on weak abilities while a
    // wipe-causing one goes off.
    std::vector<uint32_t> mandatory_interrupt_spells;

    // Creature entries that should be CCed (auto-marked moon) on
    // sight when the tank's first pull starts. The CC rule still
    // gates on class/spec compatibility (Mage poly = humanoid only,
    // Hunter trap = beast/humanoid, etc).
    std::vector<uint32_t> cc_priority_entries;

    // Aura spell ids the bot should step away from when applied to
    // self — typically boss ground-effect debuffs (Volcano, Death
    // and Decay, Whirling Blades) where staying in melee = death.
    std::vector<uint32_t> dangerous_auras;

    // ---- M+ affix / advanced raid primitives (added 2026-05-13) ----
    // These extend the script vocabulary so a single per-encounter or
    // per-affix advice block can express the full set of common
    // mechanics. Empty = "no opinion". Idle/combat rules consume only
    // the fields they understand; unknown fields are harmless.

    // Creature entries the DPS bots should kite at distance (M+ Spiteful
    // shade, certain trash adds, raid adds with high melee threat).
    std::vector<uint32_t> kite_creature_entries;
    // Aura spell ids that, when applied to SELF, require the bot to
    // move ≥ N yards from allies (M+ Volcanic / Quaking aftershock,
    // raid debuffs that explode for AoE).
    std::vector<uint32_t> spread_on_self_auras;
    // Boss spell ids that, when seen mid-cast, require all bots to
    // stack on the boss / a designated target (raid stack-soaks).
    std::vector<uint32_t> stack_on_cast_spells;
    // Dispellable debuffs that should be prioritized over normal
    // dispel order (M+ Bursting tick, raid one-shot debuffs).
    std::vector<uint32_t> dispel_priority_spells;
    // Creature entries that gain a power-up when their allies die nearby
    // (M+ Bolstering). Tanks should pull these one-at-a-time; DPS should
    // even out health bars before any single kill.
    std::vector<uint32_t> pull_separately_entries;
    // Enemy buff aura ids that flag a mob as "pull-separately" regardless
    // of its creature entry. Used for M+ Bolstering (the buff is applied
    // by the affix at runtime; we can't enumerate every possible carrier
    // creature). Tank-pull rule scans nearby_enemies[].auras for these
    // ids and applies the same single-pull constraint.
    std::vector<uint32_t> pull_separately_auras;
    // Enemy buff aura ids that should be dispelled / soothed off the
    // carrier (M+ Raging 228318, raid enrage buffs, demonic empower
    // buffs). Consumed by class-aware enrage-dispel hooks (Hunter
    // Tranq Shot 19801, Druid Soothe 2908, Mage Spellsteal 30449).
    // Builder samples mob auras into NearbyUnit.affix_buffs; class APL
    // checks intersection and dispatches the appropriate spell.
    std::vector<uint32_t> dispel_enemy_priority_spells;
    // Boss spell ids that the active tank should taunt-swap on
    // (debuff stacks, fixate cast). Off-tank takes over.
    std::vector<uint32_t> tank_swap_on_spells;
    // Spell ids the bot should soak (intercept by standing in the AoE).
    // Inverted polarity vs dangerous_auras: dangerous = move OUT;
    // soak = move IN. Used for raid orb-soaks, certain M+ mechanics.
    std::vector<uint32_t> soak_spells;

    // Boss creature entries in encounter order. Order is encounter-
    // progression so the dungeon bot logic can tell whether the run is complete
    // (all bosses dead) and which boss is "next". Used by tank-advance
    // and by diag commands. Live boss creature positions are read from
    // the map; if no waypoints are provided, advancement falls back to a
    // tight-radius Cell scan for these entries.
    std::vector<uint32_t> bosses;

    // Bosses a script brings in a while after the group reaches their waypoint (Ruins of Lordaeron: a player near the statue starts
    // three waves, 1.5 min apart, and The Abandoned comes out of it 1.5 min after the last). Until then the boss is in no scan, and
    // the owed-boss waiver (the tank stood at the owed boss's waypoint for 8 s with the boss unsighted: it is gone, owe the next one)
    // walked the tank on to the next boss between two waves. Here, how long the waiver waits for such a boss at its waypoint. A
    // boss that is not listed keeps the 8 s.
    struct OwedBossHold { uint32_t entry; uint32_t hold_ms; };
    std::vector<OwedBossHold> owed_boss_holds;

    // Creature entries that bots should NEVER target for combat — purely
    // environmental encounter objects (fire platters, bunny stalkers, rope
    // anchors, etc.) that are alive but unkillable. Without this list,
    // bots get stuck in perpetual interrupted-spell loops against these
    // objects (observed: Glubtok Firewall Platter blocking advance for 90+s).
    std::vector<uint32_t> ignore_entries;

    // GameObject entries (levers / buttons) the tank should OPERATE during
    // progression to unblock a boss or path — human-like: walk up and pull
    // the lever. A boss can sit behind a CLOSED door a lever opens (SFK
    // Baron Ashbury behind Cell Door 18934, opened by Lever 18900, wired by
    // the lever's OWN SmartGameObjectAI — no instance script). Without this,
    // bots beeline to the caged boss, get SPELL_FAILED_LINE_OF_SIGHT at
    // point-blank, and stall 0/N. When the tank is near an un-used entry
    // here, idle:dungeon_use_gate_lever walks to it and uses it (per-GUID
    // lockout so the same lever is not toggled shut). Opt-in per dungeon:
    // empty for every dungeon that has no such gating lever, so this is a
    // strict no-op everywhere it is not authored.
    std::vector<uint32_t> use_go_entries;

    // A boss that stands in a room only the dungeon's own portal reaches (City of Dalaran: the Shade of the Archmage in the
    // Violet Chamber, through the Portal To the Violet Chamber once Mana Devourer and Fel Ancient are dead). The navmesh has no
    // path there, so the route ends at the portal and every bot of the party uses it, as players do (idle:dungeon_portal_hop).
    // The boss's progression waypoint is the portal itself. Empty for every dungeon without such a portal.
    struct PortalHop
    {
        uint32_t go_entry = 0;                  // the portal object
        float    x = 0.f, y = 0.f, z = 0.f;     // where it stands
        uint32_t boss_entry = 0;                // the boss beyond it
        std::vector<uint32_t> opened_by;        // bosses that must be dead before it works
    };
    std::vector<PortalHop> portal_hops;

    // A creature the party talks to so that the way on opens (Dire Maul East: Ironbark the Redeemed opens the Conservatory Door to
    // Alzzin when told "We are ready for you to open the door"). Once every boss in opened_by is dead and boss_entry is alive, the
    // tank walks to the creature and picks its first plain gossip option, once (idle:dungeon_door_talk). x/y/z is where it stands.
    // The creature can be the boss itself (Zul'Farrak: Sergeant Bly turns on the party when told to), and a talk can wait for a door
    // another talk opens (Bly only after Weegli Blastfuse has blown the End Door, or his crew turns before the door is open).
    struct DoorTalk
    {
        uint32_t npc_entry = 0;                 // the creature to talk to
        float    x = 0.f, y = 0.f, z = 0.f;     // where it stands
        uint32_t boss_entry = 0;                // the boss beyond the way it opens (may be npc_entry)
        std::vector<uint32_t> opened_by;        // bosses that must be dead first
        uint32_t after_door = 0;                // a door (GO entry) that must stand open first; 0 = none
        // How near the tank must be before it walks over to talk; 0 = 250 y (the creature stands off the route: Ironbark is 71 y
        // from it). A creature on the route that the boss advance walks the tank up to (Pusillin on his spot) takes a short reach,
        // so the walk to him is the advance's paced one and not a straight run past the trash in between.
        float    near_yd = 0.f;
    };
    std::vector<DoorTalk> door_talks;

    // An object a player uses with an item to open the way on, standing off the route (Blackrock Depths: the Shadowforge Lock that
    // opens the Giant Doors takes the Shadowforge Key, and the two Shadowforge Braziers in the Lyceum, both lit with a Shadowforge
    // Torch, open the Golem Rooms to Magmus). The bot that carries the item, out of combat, within kKeyUseNearYd of the spot and on
    // its level, walks to the object while it is still unused and uses the item on it, once (idle:dungeon_key_use). x/y/z is where
    // the object stands. A route detour to such an object does not hold: the route follower cuts across to the crumb nearest the
    // next boss and walks past it (the far brazier is 82 y from the route).
    // An object that takes no item and several users at once (Upper Blackrock Spire's Blackrock Altar: three players use it together
    // to wake Pyroguard Emberseer) has key_item 0: every bot of the party in reach walks to it and uses it, while the creature
    // `while_alive` is alive and out of combat, and a bot that has `hold_aura` (the altar's channel) stays on it until the object
    // takes the aura away.
    // Objects a player uses one after another in a set order (the Sunken Temple's six Atal'ai Statues: the right order raises
    // Atal'alarion, a wrong one springs a trap) are `in_order` and `tank_only`: only the tank uses them, the next one is the first
    // in the list that can still be used (the instance makes a statue unselectable once it is used in turn), and while that one
    // cannot be reached the later ones wait. near_yd / level_yd widen the reach for objects across a large hall or on another floor.
    struct KeyUse
    {
        uint32_t go_entry = 0;                  // the object
        float    x = 0.f, y = 0.f, z = 0.f;     // where it stands
        uint32_t key_item = 0;                  // the item used on it; 0 = the object is used itself, by every bot in reach
        uint32_t while_alive = 0;               // a creature that must be alive and out of combat for the use; 0 = none
        uint32_t hold_aura = 0;                 // while the bot has this aura it stays at the object; 0 = none
        float    near_yd = 0.f;                 // 2D reach from the object; 0 = kKeyUseNearYd
        float    level_yd = 0.f;                // height reach from the object; 0 = kKeyUseLevelYd
        bool     tank_only = false;             // only the tank uses it (an object a second use would spoil)
        bool     in_order = false;              // one of a sequence, in list order (see above)
    };
    std::vector<KeyUse> key_uses;

    // Area triggers that start an event when a player walks into them and that the instance's own script answers (Upper Blackrock
    // Spire's stadium, 2026: Lord Victor Nefarius's waves, then Gyth and Warchief Rend Blackhand). A client sends the trigger when
    // its player crosses into the volume; a bot has no client, so a bot inside one of these sends it as the client would, once each
    // time it comes in (idle:dungeon_area_trigger). The way into the volume is the route's: the boss waypoint for the event is inside.
    std::vector<uint32_t> enter_area_triggers;

    // Creatures off the route that the party has to kill so that the way on opens (Stratholme: the five Thuzadin Acolytes inside
    // each ziggurat keep its Ash'ari Crystal alive, and the Slaughter Square gates to Baron Rivendare open only when all three
    // crystals are gone). The tank, out of combat, within kClearNearYd of the spot and on its level, walks to the nearest of them
    // still alive within `radius` of the spot while a full path reaches it (the ziggurat's door opens when its boss dies) and
    // attacks it when in sight (idle:dungeon_clear_spot). x/y/z is the spot. near_yd / level_yd widen the reach for a spot the
    // tank has to go a long way round for (the Sunken Temple's six Atal'ai Defenders, each on his own balcony at the top of its own
    // stairs, 300-450 y of walk apart and 23 y above the temple floor).
    struct ClearSpot
    {
        uint32_t entry = 0;                     // the creatures to kill
        float    x = 0.f, y = 0.f, z = 0.f;     // where they stand
        float    radius = 0.f;                  // around the spot
        float    near_yd = 0.f;                 // 2D reach from the spot; 0 = kClearNearYd
        float    level_yd = 0.f;                // height reach from the spot; 0 = kClearLevelYd
    };
    std::vector<ClearSpot> clear_spots;

    // Per-dungeon progression waypoints — ordered positions that lead a
    // tank-spec bot through the canonical clear path. The tank-advance
    // rule walks them in sequence; each waypoint is meant to put the
    // tank within nearby_enemies range of the next mob pack / boss room.
    // Path-validation (Detour) still guards against unreachable points,
    // so an out-of-date coord just gets skipped. When empty, the rule
    // falls back to the boss-Cell-scan path (less reliable; can pick
    // a creature past geometry the navmesh has bad coverage for, which
    // produced the "tank ran through a wall" bug in RFC 2026-05-13).
    struct ProgressionPoint { float x; float y; float z; };
    std::vector<ProgressionPoint> progression_waypoints;

    // Per-dungeon INTERMEDIATE routing waypoints — on-navmesh stepping
    // stones used by the boss-navigator to walk a tank toward a boss that
    // is farther than the core 74-poly PathGenerator cap (MAX_PATH_LENGTH).
    // Unlike progression_waypoints these are NOT boss-aligned (no 1:1 index
    // mapping) and are NOT walked by the index fallback or the false-combat
    // escape — they exist only so the boss-nav can chunk a cap-far approach
    // through known-good navmesh points instead of string-pulling along a
    // raw truncated path (which on the Deadmines foundry->harbor descent
    // cuts across an off-mesh ledge and drops the tank). The navigator picks
    // the farthest-forward point that is genuinely closer to the boss AND
    // strictly reachable (clean <=74-poly NORMAL path), steps toward it, and
    // hands back to the direct boss approach once the boss itself is
    // strictly reachable. Detour validates every point, so a stale coord is
    // simply skipped.
    std::vector<ProgressionPoint> route_waypoints;

    // ---- Tight-engagement zone (added 2026-06-29; generalized from the
    // Deadmines harbor / Ripsnarl approach) ----
    // Some encounters end in a dangerous final approach — a chokepoint, boss
    // platform, or descent reached after the main clear — where the group must
    // stay TIGHT and FOCUS-KILL casters instead of CCing/spreading, and the
    // tank must not out-range its healer. When the tank descends BELOW this
    // world-Z the dungeon bot logic switches into "tight engagement" mode:
    //   * tighter advance-cohesion gate (healer ≤18y, every member ≤25y, ≥85% HP)
    //   * tighter follower regroup radius (14y vs the default 22y) so the group
    //     balls up INSIDE the advance gate — eliminates the 18-22y cohesion
    //     dead-band that otherwise stalls the advance forever
    //   * smaller advance step (10y) so the tank never out-ranges heals
    //   * pre-emptive CC suppressed in favor of focus-killing (DPS assist the
    //     tank's target; high_priority_kill_entries set the kill order)
    //   * proactive DPS-engage on the tank's actual target even in the brief,
    //     leash-prone combat of event-spawned packs
    // This is the reusable toolkit that beat the Deadmines harbor; future
    // instances enable it by setting this Z (and the route_waypoints that lead
    // into the zone). 0.0 (default) = feature OFF (the zone never triggers).
    // The trigger is `tank_z < tight_engage_below_z`, so it suits a tight area
    // that sits LOWER than the rest of the wing (Deadmines harbor floor = z<30,
    // gauntlet = z57-62 → set 30.0). Requires route_waypoints to be set too.
    float tight_engage_below_z = 0.0f;
};

// Abstract per-dungeon override. Subclasses live in
// Bot/Dungeon/Scripts/<region>/<dungeon>.cpp and self-register at
// module init via DungeonScriptMgr::Register.
class DungeonScript
{
public:
    virtual ~DungeonScript() = default;

    // The dungeon this script applies to. Difficulty 0 = any
    // difficulty (script-applicable to Normal AND Heroic AND
    // Mythic without redundant duplicates).
    virtual uint32_t map_id() const = 0;
    virtual uint32_t difficulty_id() const { return 0; }

    // Diagnostic name (used in BotInspector "Dungeon: script=X" line).
    virtual char const* name() const = 0;

    // Per-tick advice. Called by the Idle dungeon rules when the
    // bot is inside the dungeon. Snapshot view is read-only.
    virtual DungeonAdvice get_advice(BotSnapshotView const& s) const = 0;

    // Subset of this dungeon's bosses whose creature is EVENT-SUMMONED by the
    // instance script (Skyriss via the Arcatraz warden consoles, Baron
    // Rivendare after the Stratholme ziggurats, Urok Doomhowl from the LBRS
    // ogre summoner, …) and therefore has ZERO static creature spawns. A
    // clientless bot squad can never trigger the summon event, so the boss's
    // InstanceScript encounter sits at NOT_STARTED forever. Full-clear
    // completion (BotSnapshotBuilder) EXCLUDES these: a 5-man is "complete"
    // once every *spawnable* boss is DONE. Snapshot-INDEPENDENT (a static fact
    // about the dungeon design) so the completion path can read it via the
    // cheap O(1) registry lookup without the per-tick GetAdvice() churn.
    //
    // Declared explicitly (author intent is ground truth) rather than
    // DB-censused at runtime. Before adding an entry, verify
    //   SELECT COUNT(*) FROM creature WHERE id=<entry> AND map=<map_id>  == 0
    // A zero-spawn boss that is NOT event-summoned is a DATA ERROR (wrong
    // entry id) and must be fixed in bosses[], never masked here.
    virtual std::vector<uint32_t> event_summoned_bosses() const { return {}; }

    // Doors (GO entries) of this dungeon that players open by hand although the door carries no lock that says so. A shut hand door
    // is normally known by its lock (BotSnapshotBuilder IsShutHandDoor: the Deadmines Heavy Doors' Quick Open). The Hall of Thanes'
    // Vault Door has no lock and no flag, the same data as the Iron Doors its switch and its bosses open, yet in the official sniff
    // players opened it: only the dungeon can say which is which. The builder lets a shut door listed here into the snapshot as a
    // hand door, and idle:dungeon_open_hand_door opens it as it opens the Heavy Doors. Read through the O(1) registry lookup, like
    // event_summoned_bosses(), so it is a static fact about the dungeon, never snapshot-dependent.
    virtual std::vector<uint32_t> hand_doors() const { return {}; }

    // Chests (GO entries) of this dungeon that hold the key to a door on the way, opened by the tank as he passes (Dire Maul North:
    // Fengus's Chest holds the Gordok Courtyard Key). A bot opens no other chest on its own: one it opened for nothing kept loot made
    // for the bot (api use_game_object). The key itself is used by whichever bot holds it (BotSnapshotBuilder KeyForShutDoor,
    // idle:dungeon_open_hand_door). Static, like hand_doors().
    virtual std::vector<uint32_t> key_chests() const { return {}; }
};

// Registry of all loaded DungeonScripts, keyed by (map_id, difficulty).
// A DB-authored traversal link: "from A you can just MOVE to B" (jump a real
// geometric split, walk an unmeshed-but-walkable stretch) — the behavioral
// alternative to baking off-mesh connections into the binary mmap tiles.
// Owner-verified rows live in {SharedDb()}.playerbot_nav_links (editor-
// authored, hot-reloadable via .playerbot reloadroutes); the dungeon stepper
// consumes them when the on-mesh path dead-ends at a link mouth, committing
// the crossing through the existing set_dungeon_cross/DungeonHonorCross
// "just move, don't think" machinery. Bots are the only consumers who need
// these routes, so the core PathGenerator stays untouched and the shipped
// mmaps remain byte-identical to stock TC data.
struct NavLink
{
    uint32_t id = 0;
    uint32_t map_id = 0;
    float ax = 0, ay = 0, az = 0;
    float bx = 0, by = 0, bz = 0;
    float radius = 12.0f;      // how close (3D) to a mouth the consumer must be
    bool  bidirectional = true;
};

// Created and populated at module init via Services::Dungeons().
// Lookup is O(1) via unordered_map.
class DungeonScriptMgr
{
public:
    DungeonScriptMgr() = default;

    // Takes ownership; called once per script at module init. The
    // registry must be assembled before any bot ticks consult it.
    void Register(std::unique_ptr<DungeonScript> script);

    // Register an "always-on" script whose advice is merged with the
    // per-dungeon script's advice on every GetAdvice call, regardless
    // of map_id. Used for M+ affix advice (Sanguine, Spiteful, etc.)
    // and raid-wide patterns. map_id() / difficulty_id() are ignored.
    void RegisterGlobal(std::unique_ptr<DungeonScript> script);

    // Returns the script for `map_id` (and optional matching
    // difficulty). Falls back to a difficulty-0 (any) script if no
    // exact match. nullptr when no script is registered for this
    // map — caller treats as "use generic logic".
    DungeonScript const* GetScriptFor(uint32_t map_id, uint32_t difficulty_id = 0) const;

    // Convenience: returns combined advice for the snapshot's
    // current map. Empty advice when no script is registered.
    DungeonAdvice GetAdvice(BotSnapshotView const& s) const;

    size_t size() const { return scripts_.size(); }

    // Load dungeon route_waypoints from the SHARED playerbot DB
    // ({Playerbot.SharedDatabase}.playerbot_dungeon_routes — populated by
    // gen_dungeon_routes.py and/or the world editor). Keyed by
    // (map_id<<8)|difficulty; INJECTED by GetAdvice for dungeons whose script
    // left route_waypoints empty (the DB is the single route source — no script
    // authors chains anymore). HOT-RELOADABLE: called once at module init and
    // again from `.playerbot reloadroutes` (SOAP) whenever the editor commits
    // route changes — builds the new table off to the side and atomically swaps
    // the shared_ptr under routes_mutex_, so concurrent GetAdvice readers on the
    // bot worker threads keep a consistent (old or new) snapshot and no restart
    // is needed. Returns the number of waypoints loaded.
    size_t LoadGeneratedRoutes();

    // The DB route of a map (the difficulty's rows, else the any-difficulty ones); empty when it has none. The scenario runner's
    // stand-in player walks it.
    std::vector<DungeonAdvice::ProgressionPoint> RouteFor(uint32_t map_id, uint32_t difficulty) const;

    // Load DB-authored traversal links ({SharedDb()}.playerbot_nav_links).
    // Same hot-reload pattern as the routes (build off-lock, swap under
    // routes_mutex_); called at init + from `.playerbot reloadroutes`.
    // Returns the number of links loaded.
    size_t LoadNavLinks();

    // Immutable snapshot of the per-map nav links (nullptr-safe; may be empty).
    // Readers copy the shared_ptr under a brief shared_lock, then read lock-free.
    using NavLinkTable = std::unordered_map<uint32_t, std::vector<NavLink>>;
    std::shared_ptr<NavLinkTable const> GetNavLinks() const
    {
        std::shared_lock<std::shared_mutex> lock(routes_mutex_);
        return nav_links_;
    }

    // Per-(bot,link) hop cooldown so a bot whose target is unreachable from
    // BOTH sides of a bidirectional link cannot ping-pong across it: once a
    // hop is claimed, the same link is refused for this bot for a few seconds
    // (a successful crossing never needs the reverse hop that fast).
    bool TryClaimLinkHop(uint64_t bot_guid_low, uint32_t link_id, uint32_t now_ms);

    // Iterate every registered per-dungeon script (excludes globals).
    // Used by the static `.playerbot smoketest dungeon` validator that
    // walks the registry to verify each script's bosses / interrupts /
    // creature entries resolve through the live ObjectMgr + SpellMgr.
    template <class Fn>
    void for_each_script(Fn fn) const
    {
        for (auto const& [key, script] : scripts_)
            if (script) fn(*script);
    }

private:
    // Key encoding: (map_id << 8) | difficulty_id. Difficulty 0 is
    // the "any" wildcard probed second.
    std::unordered_map<uint64_t, std::unique_ptr<DungeonScript>> scripts_;
    // Global scripts merged on every GetAdvice call. Order of append
    // is preserved; advice is concatenated. Typical use: M+ affix
    // bundles, raid-wide pattern primitives.
    std::vector<std::unique_ptr<DungeonScript>> global_scripts_;
    // DB-sourced route waypoints keyed by (map_id<<8)|difficulty. The map is
    // IMMUTABLE once published; LoadGeneratedRoutes builds a fresh instance and
    // swaps the shared_ptr under routes_mutex_ (hot-reload). GetAdvice readers
    // (bot worker threads, every tick) take a brief shared_lock only to copy the
    // shared_ptr, then read the immutable map lock-free — an in-flight reader
    // keeps the old table alive via its shared_ptr during a swap.
    using RouteTable =
        std::unordered_map<uint64_t, std::vector<DungeonAdvice::ProgressionPoint>>;
    std::shared_ptr<RouteTable const> generated_routes_;
    // DB-authored traversal links (see NavLink). Same publish-by-swap pattern
    // and mutex as the routes; hop-cooldown map guarded by the same mutex
    // (exclusive) — touched only when a hop actually fires (rare).
    std::shared_ptr<NavLinkTable const> nav_links_;
    std::unordered_map<uint64_t, uint32_t> link_hop_until_ms_;
    mutable std::shared_mutex routes_mutex_;
};

} // namespace Playerbot
