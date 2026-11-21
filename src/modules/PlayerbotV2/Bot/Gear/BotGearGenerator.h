// BotGearGenerator - Pure function: given (level, class, spec), returns a list
// of equipment-slot/item-entry pairs that gear the bot to the bracket's
// expected ilvl. Phase B3 of WORLD_POPULATION_PLAN.md.
//
// Algorithm:
//   1. Compute target ilvl from level (linear ramp, capped at max-level ilvl).
//   2. For each equipment slot:
//      - Filter ItemTemplate by RequiredLevel <= bot.level, ItemLevel ~= target,
//        AllowableClass match, ArmorType match (cloth/leather/mail/plate per class),
//        InventoryType match (slot fits), Bonding != BoP-raid-only.
//      - Pick deterministic from filtered list via bot-id hash.
//   3. Return slot/entry tuples.
//
// Cached at module init: per (class, level_bracket, slot) candidate pools so
// per-bot lookups are O(1).

#pragma once
#include "../../Fleet/BotPool.h"
#include "OwnedGearPolicy.h"

#include "Bot/BotTypes.h"
#include <string>
#include <vector>

class Player;
struct ItemTemplate;
namespace Playerbot { struct BotSnapshot; }

namespace Playerbot::V2::Gear {

struct GearItem
{
    uint8  slot;        // EquipmentSlots::EQUIPMENT_SLOT_*
    uint32 item_entry;  // ItemTemplate.entry
};

struct GearGenerationContext
{
    uint8  level;
    uint8  cls;
    uint16 spec;
    uint64 bot_id;      // for deterministic selection
    // Race matters because Player::CanUseItem hard-rejects an item whose
    // AllowableRace excludes the wearer, or whose faction flag belongs to the
    // other side, with EQUIP_ERR_CANT_EQUIP_EVER. The candidate pool is built
    // per CLASS and cannot express either, so the check has to happen at pick
    // time. Leave 0 to skip the race/faction gate (callers that genuinely have
    // no race, e.g. pre-creation planning).
    uint8  race = 0;
    // The bot itself, when there is one: Classic weapon skills and plate/mail are trained, so a class-legal pick can
    // still be refused with EQUIP_ERR_PROFICIENCY_NEEDED (a level 60 warrior handed a two-handed mace it never learned).
    // Picks are limited to what Player::CanUseItem accepts and whose skill it has. Null = no gate (planning).
    Player const* wearer = nullptr;
    // A world / ambient bot (no owner): capped at Playerbot.World.Gear.MaxTier and mostly blue (Playerbot.World.Gear.EpicPercent),
    // like the players of the era. Owned and raid bots keep Playerbot.Gear.MaxTier.
    ::Playerbot::Pool::BotRole role = ::Playerbot::Pool::BotRole::Owned;
    // A level-cap companion's own effective item level ceiling (0 = the configured Playerbot.Gear.MaxTier / RaidIlvlCap). Set by the
    // caller from the owner's equipment, see Owned::OwnerBasedCap. Ignored for world bots.
    int32  ilvl_cap = 0;
    // What the owner asked for (Owned::GearMode, already reduced by Owned::EffectiveMode): at the cap T1 / T2 / T3 put the class tier set on
    // and raise the ceiling to that tier for the other slots; Best ignores the owner's own gear level. Ignored for world bots.
    Owned::GearMode gear_mode = Owned::GearMode::Match;
    // A fight the bot gears resistance for (a SpellSchools value; 0 = none): at the cap, in a tier or best-in-slot mode, every armour and
    // jewellery slot takes the piece with the most resistance to that school under the mode's item level (Gear::ResistPoints), the tier
    // set filling the rest, as a vanilla raid dressed for Princess Huhuran. Weapons, trinkets and the ranged slot keep their picks.
    // Ignored for world bots.
    uint8  resist_school = 0;
    // A piece worn for the fight whatever it scores (an item entry; 0 = none): the Onyxia Scale Cloak, whose aura is what the drakes' Shadow
    // Flame checks. It goes on over the pick, the tier piece or the resistance piece in its slot. Ignored for world bots.
    uint32 wear_entry = 0;
    // Generate even for a world bot under level 10 (WorldGearStart.h). Set only to replace a starter piece the client cannot draw, so
    // the bot is not left bare; the caller then uses just the slots it needs.
    bool   replace_unrenderable_starter = false;
    bool   world_bot() const { return ::Playerbot::Pool::UsesWorldGear(role); }
};

// Owned-bot policy (OwnedGearPolicy.h): below the cap a companion's pick is the role-weighted best green or blue within
// Playerbot.Gear.Owned.IlvlBand item levels of its level; at the cap its ceiling follows the owner (Playerbot.Gear.Owned.MatchOwner).
// Off (Playerbot.Gear.Owned.Enable = 0): the level-scaled generic pick.
bool OwnedLevelGearActive(uint8 level, ::Playerbot::Pool::BotRole role);
bool OwnedMatchOwnerActive();
// Playerbot.OwnedGear.AllowedModes: the gear modes the server lets owners choose (Owned::ParseAllowedModes; default every mode).
uint32 OwnedAllowedModes();
int32 OwnedOwnerSlack();
// The configured ceiling a level-cap companion would have without the owner's gear (Playerbot.Gear.MaxTier / RaidIlvlCap).
int32 OwnedConfiguredCap();
// Role score of an item for a below-cap companion, quality adjusted (Owned::QualityAdjusted); -100000 when unwearable for the role.
int32 OwnedSlotScore(ItemTemplate const* tpl, uint8 cls, uint16 spec, uint8 level, uint8 slot);

// The item entries of a class's tier set (ItemSet.db2, found by name, see Owned::TierSetName), tier 1..3; empty when the data has none.
std::vector<uint32> const& TierSetPieces(uint8 cls, int32 tier);
// Whether `entry` is a piece of the tier set the mode names for the class.
bool TierSetIsHealerSet(uint8 cls, uint16 spec);
bool IsTierSetPick(uint8 cls, Owned::GearMode mode, uint32 entry, uint16 spec = 0);

struct SourcedGearPick { uint8 slot; uint32 entry; };
// The Naxxramas gear list of a class and spec when the tier mode wears a sourced best-in-slot list instead of the tier set (warrior dps in the
// Tier 3 mode); empty otherwise.
std::vector<SourcedGearPick> const& SourcedTierGear(uint8 cls, uint16 spec, Owned::GearMode mode);

// Variety: a pick may be any candidate of the best's quality whose effective item level is within this many points of
// the best's (and that scores about as well), chosen by bot_id and slot. Two items of one quality this close are
// peers, so swapping one for the other is never a downgrade (BotPopulationManager::RerollCompanionGear relies on it).
constexpr int32 kVarietyIlvlBand = 4;

// Raid-ready gear (Playerbot.Behavior.RaidGear): a bot at the level cap gets pre-raid best-in-slot style pieces picked by
// role stat weights (Playerbot::Gear::ScoreItemRaid) instead of the level-scaled generic pick. Candidates are rare and
// epic items of required level 50+ and effective item level up to kRaidIlvlCap (Dire Maul, BRS, Scholomance, Stratholme,
// UBRS and the first Molten Core tier; never AQ / Naxxramas), never PvP items; a slot with none falls back to anything
// the wearer may use. Classic only (the stat model is vanilla's).
constexpr uint32 kRaidMinRequiredLevel = 50;
constexpr int32  kRaidIlvlCap          = 71;   // legacy default; the live cap is Playerbot.Gear.MaxTier / RaidIlvlCap
// A regear replaces a worn piece only when the pick scores at least this much higher (never a downgrade).
constexpr float  kRaidUpgradeRatio     = 1.10f;
// Per-bot variety: any of the best kRaidVarietyTop candidates within kRaidVarietyMargin of the best score may be picked.
constexpr uint32 kRaidVarietyTop       = 4;
constexpr float  kRaidVarietyMargin    = 0.07f;
bool RaidGearActive(uint8 level);
// The effective item level ceiling raid gear may reach (Playerbot.Gear.MaxTier / RaidIlvlCap, read once at first use).
int32 LiveRaidIlvlCap();
// The same ceiling for world bots (Playerbot.World.Gear.MaxTier, default 1 = item level 76).
int32 LiveWorldIlvlCap();
// A world bot's piece that is above its limits: effective item level over the world cap, or an epic in a slot this bot's share
// of epics (Playerbot.World.Gear.EpicPercent, by bot id and slot) does not cover. Such a piece is replaced by the regear pass.
bool WorldGearOver(ItemTemplate const* tpl, uint8 level, uint64 bot_id, uint8 slot);
// True for a world bot that wears a PvP rank set on its six armour slots (decided per bot).
bool WorldPvpSet(uint64 bot_id);

// The owner's "equip" orders, planned from the bot's published snapshot (thread safe, no Player): `arg` is the order text
// after "equip" (already lower case): empty / "upgrades" / "best" / "best gear" = every bag item that is an upgrade (the
// raid role score and its 10% rule at the level cap, the older stat-weight rule below it); an item link
// (|Hitem:ID:...|h[name]|h) or a name = that bag item, worn if usable whatever its score (PvP items refused).
// Fills `picks` with the equip swaps to emit and returns the whisper text ("Name: equipped [x] (+ilvl 8), ..." /
// "Name: nothing better in my bags").
struct EquipOrderPick { uint8 bag, slot, to_slot; uint32 entry; };
std::string PlanEquipOrder(::Playerbot::BotSnapshot const& snap, std::string const& bot_name, std::string const& arg, std::vector<EquipOrderPick>& picks);

// Bag-item upgrade test of the snapshot-side auto-equip rules at the level cap (RaidGearActive), the same decision
// RaidRegearPass makes: the bagged item must be wearable PvE gear (no PvP item, effective item level <= kRaidIlvlCap) and
// score kRaidUpgradeRatio above the worn piece in `slot` (ScoreItemRaid), a two-hander against main hand + off hand
// together, and an off hand never next to a worn two-hander. cur_* are the worn entries (0 = empty). Touches no Player:
// item templates and DB2 only, so it can run on the snapshot build / bot worker threads.
bool RaidEquipUpgrade(uint8 cls, uint16 spec, uint8 level, uint8 slot, uint32 bag_entry,
                      uint32 cur_entry, uint32 cur_mainhand_entry, uint32 cur_offhand_entry,
                      bool world_bot = false, uint64 bot_id = 0);

// Below the level cap the bag-item and need-roll compares rank by EquipFitScore (item level and weighted stats), which knows no
// armor type: a level 21 warrior wore an agility leather chest over its mail one (DoctorSun, Discord 17 Nov), and the same
// warrior rolled need on it. Each side of such a compare is multiplied by this: the share of its worth the piece `entry` keeps
// for the wearer (Playerbot::Gear::ArmorTypeFitFactor; 1 for no armor piece, an unknown entry or 0). Item templates only.
float LevellingArmorFactor(uint32 entry, uint8 cls, uint16 spec, uint8 level);

// Below the level cap: whether a two-hander in the bags may replace the worn main hand when an off hand is worn too. Equipping
// it takes both off, so it has to beat the pair (ScoreItemRaid, which counts +healing and spell damage equip spells and each
// class's weapon style), not the main hand alone. True when no off hand is worn or the item is no two-hander. Item templates
// and DB2 only, like RaidEquipUpgrade.
bool TwoHandBeatsWornPair(uint8 cls, uint16 spec, uint8 level, uint32 bag_entry, uint32 cur_mainhand_entry, uint32 cur_offhand_entry);

// A fishing pole: no weapon to the bots' own gear rules (bots do not fish). A worn one counts as an empty main hand, so any
// weapon replaces it, and one in the bags is never put on by itself (a player gave "different gear sets" to a tank bot and it
// tanked with a fishing pole: the pole is a two-hander, so no shield could go on next to it).
bool IsFishingPole(uint32 entry);
// The equipment slot the bots' own gear rules (auto-equip, "equip upgrades", traded gear) may put bag item `entry` in, given
// the slot the server picked for it (`slot`), or 0xFF for none. Never a fishing pole. A shield tank (Protection Warrior 73 /
// Protection Paladin 66) takes a one-hand weapon in the main hand and a shield in the off hand, and nothing else in those two
// slots: a two-hander or an off-hand weapon would push the shield off, and EnsureShieldTankWeapon puts it back (the old churn).
// Item templates only, safe on the snapshot build / bot worker threads.
uint8 AutoEquipSlot(uint16 spec, uint8 slot, uint32 entry);
// A shield tank (73 / 66) whose off hand holds no shield (empty, or a one-hander it was given): a shield from the bags goes on
// there whatever the two score. A weapon's damage outscores a shield's stats in every compare, so a tank that once had a
// weapon in the off hand kept it and its shield stayed in the bags (DoctorSun, RU Discord 11-21: two one-handers, no shield,
// the tank died). `slot` is the one AutoEquipSlot gave, `worn_entry` what is in it now (0 = nothing). Item templates only.
bool ShieldGoesOnShieldTank(uint16 spec, uint8 slot, uint32 bag_entry, uint32 worn_entry);

// Build per-class candidate pools at module init. Idempotent. Walks
// sObjectMgr->GetItemTemplateStore() exactly once.
void Initialize();

// Pure-function gear generator. Returns one entry per equipment slot the
// bot can fill given its class. Empty vector if generator not initialized
// or no candidates exist.
std::vector<GearItem> GenerateGearFor(GearGenerationContext const& ctx);

// Warrior and rogue (vanilla's only melee classes with a ranged attack; a paladin has no ranged item) wear a thrown
// weapon in the ranged slot (Playerbot.Behavior.TankRangedWeapon): thrown is its own ammo, a bow or gun needs ammo
// nothing restocks. GenerateGearFor then offers only thrown weapons for that slot.
bool UsesThrownRanged(uint8 cls);

// The best thrown weapon (highest required level, then quality, then effective item level) the wearer may use at its
// level, 0 when the pool has none. Classic-era items only (the pools exclude everything else).
uint32 PickThrownWeapon(Player const* wearer);

// Whether a bot may wear this item at all. Classic 1.60 (Forever) also ships Season of Discovery items (ids from
// ~208000, rune set bonuses); bots there are dressed in vanilla's own items, which all sit below 25000, and no honor-rank
// rewards. A non-vanilla piece the realm itself hands out (loot, vendor, quest reward: Forever's new dungeon gear) is kept
// once a bot has it; the gear pools still leave it out.
// Holiday items only while their holiday runs, and none with Bots.HolidayClothes = 0 (worn ones come off at the next gear check).
bool IsBotWearableItemId(uint32 entry);

// Approximate ilvl target for a character level (linear L1=1 -> L80=600,
// then L80=600 -> L90=700; clamped at the realm MaxPlayerLevel).
// Exposed so the hygiene re-gear pass can detect under-geared bots (avg
// equipped effective ilvl well below this target).
uint16 TargetIlvlForLevel(uint8 level);

} // namespace Playerbot::V2::Gear
