#include "RaidEnchants.h"

#include "BotItemScorer.h"
#include "Bot/ClassTables.h"   // MaxPlayerLevel()
#include "DB2Stores.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Player.h"
#include "RaidLoadout.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "World.h"
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

namespace Playerbot::Gear {

namespace {

std::mutex s_lock;
// Enchanting spell name -> the spell's SpellItemEnchantment id (0 = the data has no such spell). Filled lazily, once per name.
std::unordered_map<std::string, uint32> s_enchant_by_name;
std::set<std::string> s_unresolved_logged;

uint32 EnchantIdOfSpell(SpellInfo const* si)
{
    for (SpellEffectInfo const& e : si->GetEffects())
        if (e.Effect == SPELL_EFFECT_ENCHANT_ITEM && e.MiscValue > 0)
            return uint32(e.MiscValue);
    return 0;
}

// Highest-id spell of that exact name with a permanent enchant effect.
uint32 ResolveEnchantId(std::string const& name)
{
    // "#24161": the enchanting spell by id, for the Zandalar enchants whose names the client data shares between spells (the wish lists the name after it).
    if (!name.empty() && name[0] == '#')
    {
        SpellInfo const* si = sSpellMgr->GetSpellInfo(uint32(std::strtoul(name.c_str() + 1, nullptr, 10)), DIFFICULTY_NONE);
        return si ? EnchantIdOfSpell(si) : 0;
    }
    {
        std::lock_guard lock(s_lock);
        if (auto it = s_enchant_by_name.find(name); it != s_enchant_by_name.end())
            return it->second;
    }
    uint32 found = 0, found_spell = 0;
    LocaleConstant const loc = sWorld->GetDefaultDbcLocale();
    sSpellMgr->ForEachSpellInfo([&](SpellInfo const* si)
    {
        if (!si || si->Difficulty != DIFFICULTY_NONE || !si->SpellName || name != (*si->SpellName)[loc])
            return;
        if (uint32 const id = EnchantIdOfSpell(si); id && si->Id > found_spell)
        {
            found = id;
            found_spell = si->Id;
        }
    });
    std::lock_guard lock(s_lock);
    s_enchant_by_name[name] = found;
    return found;
}

Loadout::Role RoleOf(uint8 cls, uint16 spec)
{
    switch (RaidRoleForBot(cls, spec))
    {
        case RaidRole::Tank:   return Loadout::Role::Tank;
        case RaidRole::Healer: return Loadout::Role::Healer;
        case RaidRole::Caster: return Loadout::Role::Caster;
        case RaidRole::Melee:  return Loadout::Role::Melee;
        case RaidRole::Hunter: return Loadout::Role::Hunter;
    }
    return Loadout::Role::Melee;
}

// The spell school a caster's gloves are for.
uint8 CasterSchool(uint8 cls, uint16 spec)
{
    switch (cls)
    {
        case CLASS_WARLOCK: return SPELL_SCHOOL_SHADOW;
        case CLASS_PRIEST:  return SPELL_SCHOOL_SHADOW;
        case CLASS_MAGE:    return spec == 64 ? SPELL_SCHOOL_FROST : SPELL_SCHOOL_FIRE;   // 64 = frost; fire and arcane take fire power
        case CLASS_DRUID:   return SPELL_SCHOOL_NATURE;   // balance (a feral or restoration druid is not a Caster role)
        case CLASS_SHAMAN:  return SPELL_SCHOOL_NATURE;   // elemental
        default:            return 0;
    }
}

// Every enchantment id the loadouts can put on (all roles, glove variants and schools, every candidate name). Built once.
std::set<uint32> const& LoadoutEnchantIds()
{
    static std::set<uint32> const ids = []
    {
        std::set<uint32> out;
        for (Loadout::Role role : { Loadout::Role::Tank, Loadout::Role::Healer, Loadout::Role::Caster, Loadout::Role::Melee, Loadout::Role::Hunter })
            for (bool agility : { false, true })
                for (uint8 school : { uint8(0), uint8(2), uint8(3), uint8(4), uint8(5) })
                    for (Loadout::EnchantWish const& w : Loadout::WishesFor(role, agility, school))
                        Loadout::FirstKnown(w.names, [&](std::string_view n)
                        {
                            if (uint32 const id = ResolveEnchantId(std::string(n)))
                                out.insert(id);
                            return false;   // visit every candidate
                        });
        return out;
    }();
    return ids;
}

} // namespace

uint32 StripRaidEnchants(Player* p)
{
    if (!p || !p->IsInWorld())
        return 0;
    std::set<uint32> const& ids = LoadoutEnchantIds();
    uint32 done = 0;
    for (uint8 s = EQUIPMENT_SLOT_START; s < EQUIPMENT_SLOT_END; ++s)
    {
        Item* item = p->GetItemByPos(INVENTORY_SLOT_BAG_0, s);
        if (!item)
            continue;
        uint32 const enchant = item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
        if (!enchant || !ids.count(enchant))
            continue;
        p->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, false);
        item->ClearEnchantment(PERM_ENCHANTMENT_SLOT);
        ++done;
        TC_LOG_INFO("playerbot.v2", "[enchant] removed from {} (level {}) slot {} item {}: enchantment {}", p->GetName(), uint32(p->GetLevel()),
                    uint32(s), item->GetEntry(), enchant);
    }
    return done;
}

uint32 ApplyRaidEnchants(Player* p, uint16 spec)
{
    if (!p || !p->IsInWorld())
        return 0;
    // The regear's reroll and scrub passes run at any level: a levelling bot gets none of these.
    if (p->GetLevel() < MaxPlayerLevel())
    {
        StripRaidEnchants(p);
        return 0;
    }
    // The bot's gear decides how good its enchants are (RaidLoadout.h EnchantTier): greens get the cheap ones, Naxxramas enchants
    // only from tier 2.5 gear.
    uint32 sum = 0, cnt = 0;
    for (uint8 s = EQUIPMENT_SLOT_START; s < EQUIPMENT_SLOT_END; ++s)
        if (s != EQUIPMENT_SLOT_BODY && s != EQUIPMENT_SLOT_TABARD)
            if (Item const* eq = p->GetItemByPos(INVENTORY_SLOT_BAG_0, s))
            {
                sum += eq->GetItemLevel(p);
                ++cnt;
            }
    Loadout::EnchantTier const tier = Loadout::TierForAverageItemLevel(cnt ? sum / cnt : 0u);
    std::set<uint32> const& ours = LoadoutEnchantIds();
    uint8 const cls = p->GetClass();
    bool const agility_hands = cls == CLASS_ROGUE || cls == CLASS_HUNTER || (cls == CLASS_DRUID && spec != 102);
    uint32 done = 0;
    for (Loadout::EnchantWish const& w : Loadout::WishesFor(RoleOf(cls, spec), agility_hands, CasterSchool(cls, spec)))
    {
        Item* item = p->GetItemByPos(INVENTORY_SLOT_BAG_0, w.slot);
        if (!item)
            continue;
        // An enchant the loadouts did not put on (a player's own) stays.
        uint32 const current = item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
        if (current && !ours.count(current))
            continue;
        ItemTemplate const* tpl = item->GetTemplate();
        if (!tpl)
            continue;
        // Weapon enchants go on weapons only (a shield or held-in-off-hand item takes none of them); shield enchants on shields only.
        bool const weapon_slot = w.slot == Loadout::SLOT_MAINHAND || w.slot == Loadout::SLOT_OFFHAND;
        bool const wants_shield = w.names.starts_with("Enchant Shield");
        if (weapon_slot && !wants_shield && tpl->GetClass() != ITEM_CLASS_WEAPON)
            continue;
        if (wants_shield && tpl->GetInventoryType() != INVTYPE_SHIELD)
            continue;
        // "Enchant 2H Weapon" enchants go on two-handers only.
        bool const two_hand = tpl->GetInventoryType() == INVTYPE_2HWEAPON;
        bool any_known = false;
        std::string_view const name = Loadout::FirstKnown(w.names, [&](std::string_view n)
        {
            if ((!two_hand && n.starts_with("Enchant 2H")) || ResolveEnchantId(std::string(n)) == 0)
                return false;
            any_known = true;
            return Loadout::EnchantAllowed(n, tier);
        });
        if (name.empty())
        {
            if (!any_known)
            {
                bool first = false;
                {
                    std::lock_guard lock(s_lock);
                    first = s_unresolved_logged.insert(std::string(w.names)).second;
                }
                if (first)
                    TC_LOG_INFO("playerbot.v2", "[enchant] unresolved: none of \"{}\" is an enchanting spell in the spell data", w.names);
            }
            // Nothing for this gear level: an enchant of a better tier (gear the bot no longer wears, an older build) comes off.
            if (current)
            {
                p->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, false);
                item->ClearEnchantment(PERM_ENCHANTMENT_SLOT);
                TC_LOG_INFO("playerbot.v2", "[enchant] {} slot {} item {}: enchantment {} removed (tier {})", p->GetName(), uint32(w.slot), tpl->GetId(),
                            current, uint32(tier));
            }
            continue;
        }
        uint32 const enchant = ResolveEnchantId(std::string(name));
        if (enchant == current)
            continue;
        p->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, false);
        item->SetEnchantment(PERM_ENCHANTMENT_SLOT, enchant, 0, 0);
        p->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, true);
        ++done;
        TC_LOG_INFO("playerbot.v2", "[enchant] {} slot {} item {}: \"{}\" (enchantment {}, tier {}, was {})", p->GetName(), uint32(w.slot), tpl->GetId(), name,
                    enchant, uint32(tier), current);
    }
    return done;
}

} // namespace Playerbot::Gear
