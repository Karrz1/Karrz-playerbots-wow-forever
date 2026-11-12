#include "BisPools.h"

namespace Playerbot::Gear::Bis {

namespace {

// Melee: warrior, rogue, retribution paladin, enhancement shaman (their Phase 6 raid lists, merged).
constexpr PoolEntry kMelee[] = {
    { Kind::Trinket, 1, "Diamond Flask" },
    { Kind::Trinket, 1, "Hand of Justice" },
    { Kind::Trinket, 1, "Earthstrike" },
    { Kind::Trinket, 2, "Drake Fang Talisman" },
    { Kind::Trinket, 3, "Slayer's Crest" },
    { Kind::Trinket, 3, "Kiss of the Spider" },
    { Kind::Trinket, 3, "Jom Gabbar" },
    { Kind::Neck,    2, "Prestor's Talisman of Connivery" },
    { Kind::Neck,    3, "Stormrage's Talisman of Seething" },
    { Kind::Back,    3, "Shroud of Dominion" },
    { Kind::Back,    3, "Cloak of the Fallen God" },
    { Kind::Ring,    1, "Don Julio's Band" },
    { Kind::Ring,    2, "Circle of Applied Force" },
    { Kind::Ring,    3, "Band of Unnatural Forces" },
    { Kind::Ring,    3, "Bonescythe Ring" },
    { Kind::Ring,    3, "Ring of the Qiraji Fury" },
};

// Hunter.
constexpr PoolEntry kHunter[] = {
    { Kind::Trinket, 1, "Blackhand's Breadth" },
    { Kind::Trinket, 1, "Earthstrike" },
    { Kind::Trinket, 3, "Slayer's Crest" },
    { Kind::Trinket, 3, "Kiss of the Spider" },
    { Kind::Neck,    2, "Prestor's Talisman of Connivery" },
    { Kind::Back,    3, "Cloak of the Fallen God" },
    { Kind::Ring,    3, "Ring of the Cryptstalker" },
    { Kind::Ring,    3, "Band of Reanimation" },
};

// Casters: mage, warlock, shadow priest, balance druid.
constexpr PoolEntry kCaster[] = {
    { Kind::Trinket, 2, "Mind Quickening Gem" },
    { Kind::Trinket, 2, "Neltharion's Tear" },
    { Kind::Trinket, 3, "The Restrained Essence of Sapphiron" },
    { Kind::Trinket, 3, "Mark of the Champion" },
    { Kind::Neck,    1, "Choker of the Fire Lord" },
    { Kind::Neck,    3, "Amulet of Vek'nilash" },
    { Kind::Neck,    3, "Gem of Trapped Innocents" },
    { Kind::Neck,    3, "Malice Stone Pendant" },
    { Kind::Back,    3, "Cloak of the Necropolis" },
    { Kind::Back,    3, "Cloak of the Devoured" },
    { Kind::Ring,    3, "Frostfire Ring" },
    { Kind::Ring,    3, "Ring of the Eternal Flame" },
    { Kind::Ring,    3, "Ring of the Fallen God" },
    { Kind::Ring,    3, "Band of the Inevitable" },
    // Off hand and wand, from the items this client has (itemsparse-decoded.tsv 2026-10-22): the vanilla caster weapons by name (Staff of the Shadow
    // Flame, Hand of Edward the Odd, Sapphiron's Eyes, Wand of Qiraji Nobility) are not in its item table. Kris of Unspoken Names stays the main hand.
    { Kind::Offhand, 1, "Therazane's Touch" },
    { Kind::Offhand, 2, "Master Dragonslayer's Orb" },
    { Kind::Offhand, 3, "Royal Scepter of Vek'lor" },
    { Kind::Ranged,  1, "Greater Eternal Wand" },
    { Kind::Ranged,  1, "Brilliant Wand" },
};

// Healers: priest, druid, paladin, shaman.
constexpr PoolEntry kHealer[] = {
    { Kind::Trinket, 1, "Zandalarian Hero Charm" },
    { Kind::Trinket, 1, "Shard of the Scale" },
    { Kind::Trinket, 2, "Rejuvenating Gem" },
    { Kind::Trinket, 3, "Warmth of Forgiveness" },
    { Kind::Trinket, 3, "Eye of the Dead" },
    { Kind::Trinket, 3, "Scarab Brooch" },
    { Kind::Neck,    3, "Amulet of the Fallen God" },
    { Kind::Neck,    3, "Necklace of Necropsy" },
    { Kind::Back,    3, "Cloak of Suturing" },
    { Kind::Back,    3, "Cloak of Clarity" },
    { Kind::Ring,    2, "Pure Elementium Band" },
    { Kind::Ring,    3, "Ring of Faith" },
    { Kind::Ring,    3, "Band of Unanswered Prayers" },
    { Kind::Ring,    3, "Ring of the Martyr" },
    { Kind::Ring,    3, "Ring of Redemption" },
    { Kind::Ring,    3, "Ring of the Earthshatterer" },
};

// Tanks: protection warrior, feral druid (defensive and threat entries both; the role score decides).
constexpr PoolEntry kTank[] = {
    { Kind::Trinket, 1, "Earthstrike" },
    { Kind::Trinket, 1, "Diamond Flask" },
    { Kind::Trinket, 1, "Nat Pagle's Broken Reel" },
    { Kind::Trinket, 2, "Drake Fang Talisman" },
    { Kind::Trinket, 2, "Lifegiving Gem" },
    { Kind::Trinket, 2, "Styleen's Impeding Scarab" },
    { Kind::Trinket, 3, "Kiss of the Spider" },
    { Kind::Neck,    3, "Mark of C'Thun" },
    { Kind::Neck,    3, "Sadist's Collar" },
    { Kind::Back,    3, "Cloak of the Fallen God" },
    { Kind::Back,    3, "Cryptfiend Silk Cloak" },
    { Kind::Ring,    2, "Circle of Applied Force" },
    { Kind::Ring,    3, "Ring of the Dreadnaught" },
    { Kind::Ring,    3, "Ring of Emperor Vek'lor" },
    { Kind::Ring,    3, "Ring of the Godslayer" },
    { Kind::Ring,    3, "Signet of the Fallen Defender" },
};

} // namespace

std::span<PoolEntry const> PoolFor(Loadout::Role role)
{
    switch (role)
    {
        case Loadout::Role::Melee:  return kMelee;
        case Loadout::Role::Hunter: return kHunter;
        case Loadout::Role::Caster: return kCaster;
        case Loadout::Role::Healer: return kHealer;
        case Loadout::Role::Tank:   return kTank;
    }
    return {};
}

} // namespace Playerbot::Gear::Bis
