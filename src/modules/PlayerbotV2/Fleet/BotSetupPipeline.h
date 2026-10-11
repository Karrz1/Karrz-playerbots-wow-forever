// BotSetupPipeline - Orchestrates the per-bot post-distribution setup steps
// (B1..B10 in WORLD_POPULATION_PLAN.md). Idempotent — each step's progress
// is bitmasked into playerbot_v2_character.setup_pipeline_state so partial
// runs resume safely.
//
// Steps:
//   B1 SetLevel         - Player::GiveLevel(N)
//   B2 GrantStarterKit  - 500g + bracket-appropriate bags
//   B3 GenerateGear     - via Bot/Gear/BotGearGenerator
//   B4 AutoEquip        - Player::EquipItem each generated item
//   B5 ApplyTalents     - PlayerbotAPI::apply_talent_build(context)
//   B6 LearnProfessions - 2 primary + cooking/fishing/first-aid (gated L>=10)
//   B7 AcquireMount     - LearnSpell(racial mount + riding tier) (gated L>=20/30/60/70/80)
//   B8 PlaceInCapital   - TeleportTo(faction capital) (gated L>=10; else starter zone)
//   B9 TravelToZone     - TeleportTo(level-appropriate zone) (gated L>=5)
//   B10 MarkComplete    - setup_pipeline_state = 0xFF
//
// All steps run on the world thread (must hold map locks), so this orchestrator
// is invoked from OnWorldUpdate after a bot is in-world.

#pragma once

#include "Bot/BotTypes.h"

#include <array>
#include <mutex>
#include <string>
#include <unordered_map>

class Player;

namespace Playerbot::V2::Fleet {

// Per-bot pipeline-failure ring. Populated by BotSetupPipeline whenever a
// step's Do* callable returns false (mirrors the existing TC_LOG_WARN path).
// Consumed by /diag so the GM can see "step PlaceAndTravel failed 3x" without
// scraping Server.log. Indexed by bot character_guid_low.
struct PipelineFailureEntry
{
    uint32      ts_ms = 0;
    uint8       step_bit = 0;     // SetupBit::* mask of the failing step
    uint8       state_when = 0;   // setup_pipeline_state at the time of failure
    std::string step_name;        // "StarterKit" / "GenerateGear" / …
};

class PipelineFailureRing
{
public:
    static constexpr size_t kCap = 8;

    // Append. Callable from any thread.
    void Record(uint64 char_guid_low, PipelineFailureEntry entry);

    // Walk recorded failures for one bot, oldest-first. Visitor takes
    // (size_t i, PipelineFailureEntry const&). No-op if no failures
    // recorded for that bot.
    template <class F>
    void ForEach(uint64 char_guid_low, F&& fn) const
    {
        std::lock_guard lk(mtx_);
        auto it = rings_.find(char_guid_low);
        if (it == rings_.end()) return;
        const auto& r = it->second;
        const size_t start = (r.size < kCap) ? 0 : r.head;
        for (size_t i = 0; i < r.size; ++i)
        {
            const size_t idx = (start + i) % kCap;
            fn(i, r.entries[idx]);
        }
    }

    static PipelineFailureRing& Instance();

private:
    struct Ring
    {
        std::array<PipelineFailureEntry, kCap> entries{};
        size_t head = 0;
        size_t size = 0;
    };
    mutable std::mutex                   mtx_;
    std::unordered_map<uint64, Ring>     rings_;
};

namespace SetupBit {
    constexpr uint8 SetLevel        = 1u << 0;
    constexpr uint8 StarterKit      = 1u << 1;
    constexpr uint8 GenerateGear    = 1u << 2;
    constexpr uint8 AutoEquip       = 1u << 3;
    constexpr uint8 ApplyTalents    = 1u << 4;
    constexpr uint8 LearnProfessions = 1u << 5;
    constexpr uint8 AcquireMount    = 1u << 6;
    constexpr uint8 PlaceAndTravel  = 1u << 7;   // B8 + B9 collapsed
    constexpr uint8 AllDone         = 0xFFu;
}

class BotSetupPipeline
{
public:
    // The caller's own mirror of the two playerbot_v2_character columns this
    // pipeline resumes from. Passing one in replaces the synchronous
    // `SELECT setup_pipeline_state, distribution_level` that RunFor /
    // RunStarterOnly used to do on the WORLD thread on every call — at the
    // 10-bots-per-second drive rate that readback was one blocking DB
    // round-trip per driven bot per second. BotPopulationManager keeps such a
    // mirror per bot (pipeline_row_cache_, filled by an async query) and hands
    // it over here; the pipeline updates it in place whenever it persists, so
    // the mirror stays authoritative and nobody has to read the row back.
    // Passing nullptr keeps the old synchronous read (used by callers that
    // hold no mirror).
    struct StateRef
    {
        uint8 state      = 0;
        uint8 dist_level = 0;
    };

    // Run remaining setup steps for this bot up to target_level. Each call
    // is incremental; resumes from setup_pipeline_state. Returns true if
    // pipeline is now complete.
    //
    // Safe to call repeatedly. Cheap if already complete (single int read).
    bool RunFor(Player* bot, uint8 target_level, StateRef* known = nullptr);

    // Reduced setup for bots kept at their racial START level (no distribution
    // bucket): completes the class/racial starter quest chain (grants spells &
    // skills) and relocates the bot out of the no-navmesh starter zone (Acherus
    // / Forbidden Reach / Mardum) to its faction capital — WITHOUT leveling,
    // gearing, or mounting it. Used for Dracthyr / Death Knight start-level bots
    // until per-race start-level behavior is scripted; otherwise they sit
    // incomplete (missing class spells) and stuck in the starter zone. Re-entrant
    // (one quest per call); returns true once the chain is done + relocated.
    bool RunStarterOnly(Player* bot, StateRef* known = nullptr);

    // Classic 1.60: moves a Skyborne bot that reached level 10 on Zephras Isle to a leveling town of the vanilla world, exactly
    // as the placement step does for a new bot of that level. The island has no road, boat or portal off it, so this is the
    // way off. Returns true when the bot was placed. World thread.
    bool Graduate(Player* bot);

    // Reset pipeline state to 0 — forces a re-run on next RunFor. Used by
    // .playerbot pipeline <name> diagnostic command for debugging.
    void Reset(Player* bot);

    // Ensure the bot has adequate bags (every bag slot a 30-slot bag), equipped
    // DIRECTLY into empty slots so it works even with a full backpack (escapes
    // the bagless-bot inventory deadlock). Static — no pipeline state needed.
    // Called from DoGrantStarterKit AND the hygiene gear-backfill so bots that
    // never ran distribution (manually-created test bots) also get bags.
    static void EnsureBags(Player* bot);

    // The spellbook of the bot's current level, as `.bot add` companions get it: class quest abilities, every class trainer
    // spell up to the level, weapon skills. DoSetLevel runs it after GiveLevel; a caller that levels the bot itself (the
    // scenario runner) must run it too, since DoSetLevel returns early for a bot already at the target level and its
    // spellbook would stay at what GiveLevel grants (no Thunder Clap on a level-21 warrior).
    static void LearnLevelKit(Player* bot);

private:
    bool DoSetLevel        (Player* bot, uint8 target_level);
    bool DoGrantStarterKit (Player* bot);
    bool DoGenerateGear    (Player* bot);
    bool DoAutoEquip       (Player* bot);   // implicit: piggybacks on GenerateGear
    bool DoApplyTalents    (Player* bot);
    bool DoLearnProfessions(Player* bot);
    bool DoAcquireMount    (Player* bot);
    bool DoPlaceAndTravel  (Player* bot);

    // DB persistence for setup_pipeline_state.
    void PersistState(uint64 char_guid_low, uint8 state, uint8 distribution_level);
};

} // namespace Playerbot::V2::Fleet
