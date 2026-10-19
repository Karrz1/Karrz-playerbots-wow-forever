// BotItemScorer - Score how well an item suits a bot.
//
// Used by:
//   - PlayerbotAPI::complete_quest -> ScoreQuestReward: pick best reward
//     index when the bot calls complete_quest with kRewardChoiceAuto.
//   - PlayerbotV2 Bot/Gear/BotGearGenerator: pick the best in-slot item
//     from the per-class candidate pool when distributing gear.
//   - PlayerbotV2 Fleet/StarterQuestAutocomplete: pick the best reward
//     index for a class/race starter quest auto-completion.
//
// Score components (rough magnitude, highest-impact first):
//   - Class can equip       (-1000 / 0)   hard block on mis-class items
//   - Primary stat match    (+500)        decisive at endgame for ilvl-tied gear
//   - Wrong primary stat    (-300)        wasted item for the spec
//   - Armor type match      (+200)        cloth on a mage = good, on a paladin = bad
//   - Wrong armor type      (-100)        but neutral for misc/cosmetic accessories
//   - Weapon proficiency    (+100..+200)  per-class inventory-type alignment
//   - Stamina               (+60)         universal HP buff
//   - Secondaries           (+40)         crit/haste/mastery/versatility
//   - Spirit                (+50/-25)     healer perk, dead weight elsewhere
//   - Tank stats            (+35/+0)      defense/dodge/parry/block (tanks only)
//   - Item level            (+ilvl)       tie-break
//   - Quality               (+5 x Q)      Epic narrowly beats Rare at same ilvl

#pragma once

#include "Define.h"
#include "ItemTemplate.h"   // ItemSubclassArmor

struct ItemTemplate;
class Player;

namespace Playerbot::Gear {

// Score a candidate item for the bot's class+spec+level. Higher = better fit.
// Negative values are possible (mis-equipped or wrong primary). The class
// can-equip check returns -1000 to effectively rule out items the bot
// physically can't wear.
int32 ScoreItemForBot(ItemTemplate const* tpl, Player* bot);

// Same as above but takes class/spec/level explicitly - used by call sites
// without a Player* (e.g. pre-distribution scoring).
int32 ScoreItemForClass(ItemTemplate const* tpl,
                        uint8 cls, uint16 spec, uint8 level);

// Effective (level-scaled) item level a wearer of `level` would actually have
// equipped, computed WITHOUT a live Item via BonusData::Initialize(tpl) +
// the static Item::GetItemLevel overload. TC 12.0 scales many items at
// wear-time via PlayerLevelToItemLevelCurveId clamped to ContentTuning, so the
// STATIC GetBaseItemLevel() can be wildly different from what the bot wears
// (e.g. a base-120 item collapsing to effective ilvl 5 at L16). This is the
// value the snapshot / .playerbot inspect reports (Item::GetItemLevel(owner)).
// For items with no scaling curve it returns the static base ilvl unchanged.
// NOT for the synchronous snapshot-build thread (DB2 curve lookups); use at
// setup / hygiene / OOC only.
int32 EffectiveItemLevelForLevel(ItemTemplate const* tpl, uint8 level);

// The armor subclass a class should wear: the highest tier its proficiency
// allows. THE single definition - the gear generator indexes its candidate
// pools with it and the scorer ranks with it, and when those two disagreed the
// generator stocked a pool the scorer then penalised (-100 for "wrong armor"),
// so sub-40 hunters drew leather out of a mail pool.
//
// Level-independent on purpose. The level-40 proficiency step is Cataclysm-era;
// since Mists every class holds its final armor skill from level 1. Verified
// against live character data 2026-09-15: of sub-40 characters, 12330/12426
// warriors hold Plate Mail (293) and 13931/14022 hunters hold Mail (413).
ItemSubclassArmor PreferredArmorForClass(uint8 cls);

// Returns the primary stat (ITEM_MOD_STRENGTH/AGILITY/INTELLECT) for the
// (class, spec) pair. Hybrid classes (Druid/Monk/Pally/Sham) have spec-
// dependent primaries; pure classes return their fixed stat.
uint32 PrimaryStatForBot(uint8 cls, uint16 spec);

// True if this (class, spec) is a healer spec - used to flip Spirit
// scoring from waste to perk.
bool IsHealerSpec(uint8 cls, uint16 spec);

// ---- Classic 1.60 raid-ready scoring (Playerbot.Behavior.RaidGear) -------------------------------------------------
// The generic scorer above was written for the retail stat model (primary stat, "secondaries" at a flat 40): it does
// not look at armor, spell power, healing, attack power, hit, mp5, defense, block or weapon damage at all. The raid
// scorer weighs what a vanilla raider of the role actually gears for.
enum class RaidRole : uint8
{
    Tank,       // prot warrior / prot paladin / bear druid: armor, stamina, defense, dodge/parry/block, then strength/agility, hit
    Healer,     // spell power / healing, intellect, mp5, spell crit (spirit for priest/druid/shaman)
    Caster,     // spell damage, hit, crit, intellect
    Melee,      // attack power, strength / agility, hit, crit, weapon damage
    Hunter      // agility, ranged attack power, hit, crit, ranged weapon damage
};

RaidRole RaidRoleForBot(uint8 cls, uint16 spec);
char const* RaidRoleName(RaidRole role);

// Stat-weighted score of an item for the (class, spec) wearing it at `level` in equipment slot `slot` (the slot matters
// for weapons: main hand / off hand / ranged). Higher is better, about ten points per weighted stat point; 0 or less
// means useless to this role, a very negative value means never wear (PvP stats). Includes the equip / use / proc
// item spells' auras (+healing, +damage, +attack power, hit, crit, mp5...) that are not item stats.
int32 ScoreItemRaid(ItemTemplate const* tpl, uint8 cls, uint16 spec, uint8 level, uint8 slot);

// The item's resistance points in `school` (SpellSchools: holy 1, fire 2, nature 3, frost 4, shadow 5, arcane 6) for a wearer at `level`:
// the school's own resistance stat plus resistance to all schools, as the wearer gets them. 0 when it carries none.
int32 ResistPoints(ItemTemplate const* tpl, uint8 school, uint8 level);

// True when the (class, spec) fights with weapons in both hands or a two-hander, so a shield or a held-in-off-hand caster item
// (Royal Scepter of Vek'lor) in the off hand is dead weight: rogues, warriors that are not protection, hunters and enhancement
// shamans. Its off hand is a weapon or empty (behind a two-hander).
inline bool OffhandMustBeWeapon(uint8 cls, uint16 spec)
{
    return cls == CLASS_ROGUE || cls == CLASS_HUNTER || (cls == CLASS_WARRIOR && spec != 73) || (cls == CLASS_SHAMAN && spec == 263);
}

// True for items that must never reach a bot as PvE gear: honor-rank rewards, PvP-power / resilience stats,
// battleground reputation rewards (Stormpike, Frostwolf, Arathi, Warsong, Silverwing), tournament gear.
bool IsPvpItem(ItemTemplate const* tpl);

} // namespace Playerbot::Gear
