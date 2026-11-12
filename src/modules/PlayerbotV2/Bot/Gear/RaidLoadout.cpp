#include "RaidLoadout.h"

#include <array>

namespace Playerbot::Gear::Loadout {

namespace {

// Vanilla trade enchants of the Naxxramas era. Weapons: Crusader for the physical roles (the Crusader proc was the melee standard), Spell Power
// (Mighty Intellect before it is known) for casters. Casters also wear Arcanum of Focus on head and legs and a Zandalar Signet / Power of the
// Scourge on the shoulders (the 182 warlock wore only chest, bracer, glove, cloak and weapon enchants). Gloves and bracers follow the role's
// main stat; a tank takes stamina and threat.
constexpr EnchantWish kMelee[] = {
    { SLOT_HEAD,      "#24161|Arcanum of Rapidity" },
    { SLOT_LEGS,      "#24161|Arcanum of Rapidity" },
    { SLOT_SHOULDERS, "Might of the Scourge|Zandalar Signet of Might" },
    { SLOT_CHEST,    "Enchant Chest - Greater Stats|Enchant Chest - Major Stats|Enchant Chest - Stats" },
    { SLOT_WRISTS,   "Enchant Bracer - Superior Strength|Enchant Bracer - Greater Strength|Enchant Bracer - Strength" },
    { SLOT_HANDS,    "Enchant Gloves - Greater Strength|Enchant Gloves - Strength" },
    { SLOT_FEET,     "Enchant Boots - Greater Agility|Enchant Boots - Agility" },
    { SLOT_BACK,     "Enchant Cloak - Lesser Agility|Enchant Cloak - Agility" },
    { SLOT_MAINHAND, "Enchant Weapon - Crusader" },
    { SLOT_OFFHAND,  "Enchant Weapon - Crusader" },
};
constexpr EnchantWish kMeleeAgility[] = {
    { SLOT_HEAD,      "#24161|Arcanum of Rapidity" },
    { SLOT_LEGS,      "#24161|Arcanum of Rapidity" },
    { SLOT_SHOULDERS, "Might of the Scourge|Zandalar Signet of Might" },
    { SLOT_CHEST,    "Enchant Chest - Greater Stats|Enchant Chest - Major Stats|Enchant Chest - Stats" },
    { SLOT_WRISTS,   "Enchant Bracer - Superior Strength|Enchant Bracer - Greater Strength|Enchant Bracer - Strength" },
    { SLOT_HANDS,    "Enchant Gloves - Greater Agility|Enchant Gloves - Agility" },
    { SLOT_FEET,     "Enchant Boots - Greater Agility|Enchant Boots - Agility" },
    { SLOT_BACK,     "Enchant Cloak - Lesser Agility|Enchant Cloak - Agility" },
    { SLOT_MAINHAND, "Enchant Weapon - Crusader" },
    { SLOT_OFFHAND,  "Enchant Weapon - Crusader" },
};
constexpr EnchantWish kHunter[] = {
    { SLOT_HEAD,      "#24162|Falcon's Call|Arcanum of Rapidity" },
    { SLOT_LEGS,      "#24162|Falcon's Call|Arcanum of Rapidity" },
    { SLOT_SHOULDERS, "Might of the Scourge|Zandalar Signet of Might" },
    { SLOT_CHEST,    "Enchant Chest - Greater Stats|Enchant Chest - Major Stats|Enchant Chest - Stats" },
    { SLOT_WRISTS,   "Enchant Bracer - Superior Agility|Enchant Bracer - Greater Agility|Enchant Bracer - Agility" },
    { SLOT_HANDS,    "Enchant Gloves - Greater Agility|Enchant Gloves - Agility" },
    { SLOT_FEET,     "Enchant Boots - Greater Agility|Enchant Boots - Agility" },
    { SLOT_BACK,     "Enchant Cloak - Lesser Agility|Enchant Cloak - Agility" },
    { SLOT_MAINHAND, "Enchant 2H Weapon - Agility|Enchant Weapon - Agility|Enchant Weapon - Crusader" },
};
constexpr EnchantWish kTank[] = {
    { SLOT_HEAD,      "#24149|Presence of Might|Arcanum of Protection" },
    { SLOT_LEGS,      "#24149|Presence of Might|Arcanum of Protection" },
    { SLOT_SHOULDERS, "Fortitude of the Scourge|Zandalar Signet of Might" },
    { SLOT_CHEST,    "Enchant Chest - Greater Stats|Enchant Chest - Major Stats|Enchant Chest - Stats" },
    { SLOT_WRISTS,   "Enchant Bracer - Superior Stamina|Enchant Bracer - Greater Stamina|Enchant Bracer - Stamina" },
    { SLOT_HANDS,    "Enchant Gloves - Threat" },
    { SLOT_FEET,     "Enchant Boots - Greater Stamina|Enchant Boots - Stamina" },
    { SLOT_BACK,     "Enchant Cloak - Greater Defense|Enchant Cloak - Defense" },
    { SLOT_MAINHAND, "Enchant Weapon - Crusader" },
    { SLOT_OFFHAND,  "Enchant Shield - Superior Stamina|Enchant Shield - Greater Stamina|Enchant Shield - Stamina" },
};
constexpr EnchantWish kHealer[] = {
    { SLOT_HEAD,      "#24168|#24167|Arcanum of Focus" },
    { SLOT_LEGS,      "#24168|#24167|Arcanum of Focus" },
    { SLOT_SHOULDERS, "Zandalar Signet of Serenity|#24420" },
    { SLOT_FEET,     "Enchant Boots - Spirit|Enchant Boots - Greater Stamina|Enchant Boots - Stamina" },
    { SLOT_CHEST,    "Enchant Chest - Greater Stats|Enchant Chest - Major Stats|Enchant Chest - Stats" },
    { SLOT_WRISTS,   "Enchant Bracer - Mana Regeneration|Enchant Bracer - Healing Power|Enchant Bracer - Superior Spirit" },
    { SLOT_HANDS,    "Enchant Gloves - Healing Power" },
    { SLOT_BACK,     "Enchant Cloak - Lesser Agility" },
    { SLOT_MAINHAND, "Enchant Weapon - Healing Power|Enchant Weapon - Mighty Intellect" },
};
constexpr EnchantWish kCasterFire[] = {
    { SLOT_HEAD,      "Arcanum of Focus" },
    { SLOT_LEGS,      "Arcanum of Focus" },
    { SLOT_SHOULDERS, "Zandalar Signet of Mojo|Power of the Scourge" },
    { SLOT_CHEST,    "Enchant Chest - Greater Stats|Enchant Chest - Major Stats|Enchant Chest - Stats" },
    { SLOT_WRISTS,   "Enchant Bracer - Superior Intellect|Enchant Bracer - Greater Intellect|Enchant Bracer - Intellect" },
    { SLOT_HANDS,    "Enchant Gloves - Fire Power" },
    { SLOT_FEET,     "Enchant Boots - Greater Stamina|Enchant Boots - Stamina" },
    { SLOT_BACK,     "Enchant Cloak - Lesser Agility" },
    { SLOT_MAINHAND, "Enchant Weapon - Spell Power|Enchant Weapon - Mighty Intellect" },
};
constexpr EnchantWish kCasterFrost[] = {
    { SLOT_HEAD,      "Arcanum of Focus" },
    { SLOT_LEGS,      "Arcanum of Focus" },
    { SLOT_SHOULDERS, "Zandalar Signet of Mojo|Power of the Scourge" },
    { SLOT_CHEST,    "Enchant Chest - Greater Stats|Enchant Chest - Major Stats|Enchant Chest - Stats" },
    { SLOT_WRISTS,   "Enchant Bracer - Superior Intellect|Enchant Bracer - Greater Intellect|Enchant Bracer - Intellect" },
    { SLOT_HANDS,    "Enchant Gloves - Frost Power" },
    { SLOT_FEET,     "Enchant Boots - Greater Stamina|Enchant Boots - Stamina" },
    { SLOT_BACK,     "Enchant Cloak - Lesser Agility" },
    { SLOT_MAINHAND, "Enchant Weapon - Spell Power|Enchant Weapon - Mighty Intellect" },
};
constexpr EnchantWish kCasterShadow[] = {
    { SLOT_HEAD,      "Arcanum of Focus" },
    { SLOT_LEGS,      "Arcanum of Focus" },
    { SLOT_SHOULDERS, "Zandalar Signet of Mojo|Power of the Scourge" },
    { SLOT_CHEST,    "Enchant Chest - Greater Stats|Enchant Chest - Major Stats|Enchant Chest - Stats" },
    { SLOT_WRISTS,   "Enchant Bracer - Superior Intellect|Enchant Bracer - Greater Intellect|Enchant Bracer - Intellect" },
    { SLOT_HANDS,    "Enchant Gloves - Shadow Power" },
    { SLOT_FEET,     "Enchant Boots - Greater Stamina|Enchant Boots - Stamina" },
    { SLOT_BACK,     "Enchant Cloak - Lesser Agility" },
    { SLOT_MAINHAND, "Enchant Weapon - Spell Power|Enchant Weapon - Mighty Intellect" },
};
constexpr EnchantWish kCasterNature[] = {
    { SLOT_HEAD,      "Arcanum of Focus" },
    { SLOT_LEGS,      "Arcanum of Focus" },
    { SLOT_SHOULDERS, "Zandalar Signet of Mojo|Power of the Scourge" },
    { SLOT_CHEST,    "Enchant Chest - Greater Stats|Enchant Chest - Major Stats|Enchant Chest - Stats" },
    { SLOT_WRISTS,   "Enchant Bracer - Superior Intellect|Enchant Bracer - Greater Intellect|Enchant Bracer - Intellect" },
    { SLOT_HANDS,    "Enchant Gloves - Natural Power|Enchant Gloves - Fire Power|Enchant Gloves - Healing Power" },
    { SLOT_FEET,     "Enchant Boots - Greater Stamina|Enchant Boots - Stamina" },
    { SLOT_BACK,     "Enchant Cloak - Lesser Agility" },
    { SLOT_MAINHAND, "Enchant Weapon - Spell Power|Enchant Weapon - Mighty Intellect" },
};
constexpr EnchantWish kCasterPlain[] = {
    { SLOT_HEAD,      "Arcanum of Focus" },
    { SLOT_LEGS,      "Arcanum of Focus" },
    { SLOT_SHOULDERS, "Zandalar Signet of Mojo|Power of the Scourge" },
    { SLOT_CHEST,    "Enchant Chest - Greater Stats|Enchant Chest - Major Stats|Enchant Chest - Stats" },
    { SLOT_WRISTS,   "Enchant Bracer - Superior Intellect|Enchant Bracer - Greater Intellect|Enchant Bracer - Intellect" },
    { SLOT_FEET,     "Enchant Boots - Greater Stamina|Enchant Boots - Stamina" },
    { SLOT_BACK,     "Enchant Cloak - Lesser Agility" },
    { SLOT_MAINHAND, "Enchant Weapon - Spell Power|Enchant Weapon - Mighty Intellect" },
};

// The cheap trade enchants a levelling or freshly 60 player could afford.
constexpr std::string_view kCheap[] = {
    "Enchant Bracer - Strength", "Enchant Bracer - Agility", "Enchant Bracer - Stamina", "Enchant Bracer - Intellect",
    "Enchant Gloves - Strength", "Enchant Gloves - Agility", "Enchant Boots - Agility", "Enchant Boots - Stamina",
    "Enchant Cloak - Lesser Agility", "Enchant Cloak - Defense", "Enchant Shield - Stamina", "Enchant Chest - Stats",
};

} // namespace

EnchantTier TierForAverageItemLevel(std::uint32_t avg_ilvl)
{
    if (avg_ilvl >= kNaxxEnchantIlvl)
        return EnchantTier::Naxx;
    return avg_ilvl >= kStandardEnchantIlvl ? EnchantTier::Standard : EnchantTier::Cheap;
}

EnchantTier TierOfEnchant(std::string_view name)
{
    if (name.ends_with(" of the Scourge"))
        return EnchantTier::Naxx;
    for (std::string_view c : kCheap)
        if (name == c)
            return EnchantTier::Cheap;
    return EnchantTier::Standard;
}

std::span<EnchantWish const> WishesFor(Role role, bool agility_hands, std::uint8_t school)
{
    switch (role)
    {
        case Role::Tank:   return kTank;
        case Role::Healer: return kHealer;
        case Role::Hunter: return kHunter;
        case Role::Melee:  return agility_hands ? std::span<EnchantWish const>(kMeleeAgility) : std::span<EnchantWish const>(kMelee);
        case Role::Caster:
            switch (school)
            {
                case 2:  return kCasterFire;
                case 3:  return kCasterNature;
                case 4:  return kCasterFrost;
                case 5:  return kCasterShadow;
                default: return kCasterPlain;
            }
    }
    return {};
}

} // namespace Playerbot::Gear::Loadout
