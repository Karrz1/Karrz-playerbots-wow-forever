#include "ClassicTalentBuilds.h"
#include "SharedDefines.h"
#include <array>

namespace Playerbot::Classic {

namespace {

// 1.12 talent names. Each list spends 51 points; ranks are bought in order,
// so early entries are what a levelling bot gets first. A rank that the tree
// refuses (tier not reached yet) is skipped and retried on the next apply.

// ---- Warrior ----
constexpr TalentPick kWarriorArms[] = {
    { "Improved Heroic Strike", 3 }, { "Deflection", 2 }, { "Improved Charge", 2 }, { "Tactical Mastery", 3 },
    { "Improved Overpower", 2 }, { "Anger Management", 1 }, { "Deep Wounds", 3 },
    { "Two-Handed Weapon Specialization", 5 }, { "Impale", 2 }, { "Sweeping Strikes", 1 },
    { "Sword Specialization", 5 }, { "Improved Hamstring", 1 }, { "Mortal Strike", 1 },
    { "Cruelty", 5 }, { "Unbridled Wrath", 5 }, { "Improved Battle Shout", 5 }, { "Improved Execute", 2 },
    { "Enrage", 3 },
    // Fillers, bought only with points left over: Forever's trees have no talent called
    // Tactical Mastery, Sword Specialization or Improved Battle Shout (PC run 2026-10-05).
    { "Booming Voice", 5 }, { "Improved Rend", 3 }, { "Axe Specialization", 5 }, { "Improved Thunder Clap", 3 },
};
constexpr TalentPick kWarriorFury[] = {
    { "Cruelty", 5 }, { "Unbridled Wrath", 5 }, { "Improved Battle Shout", 5 }, { "Dual Wield Specialization", 5 },
    { "Improved Execute", 2 }, { "Death Wish", 1 }, { "Enrage", 2 }, { "Flurry", 5 }, { "Bloodthirst", 1 },
    { "Improved Cleave", 3 },
    { "Improved Heroic Strike", 3 }, { "Deflection", 3 }, { "Tactical Mastery", 5 }, { "Anger Management", 1 },
    { "Deep Wounds", 3 }, { "Impale", 2 },
    { "Booming Voice", 5 }, { "Improved Demoralizing Shout", 5 }, { "Blood Craze", 3 },   // fillers, see Arms
};
constexpr TalentPick kWarriorProtection[] = {
    { "Shield Specialization", 5 }, { "Anticipation", 5 }, { "Toughness", 5 }, { "Last Stand", 1 },
    { "Improved Shield Block", 3 }, { "Defiance", 5 }, { "Improved Sunder Armor", 3 }, { "Improved Taunt", 2 },
    { "Concussion Blow", 1 }, { "One-Handed Weapon Specialization", 5 }, { "Shield Slam", 1 },
    { "Improved Revenge", 2 },
    { "Improved Heroic Strike", 3 }, { "Deflection", 2 }, { "Tactical Mastery", 3 },
    { "Cruelty", 5 },
    { "Improved Bloodrage", 2 }, { "Booming Voice", 5 },   // fillers, see Arms
};

// ---- Paladin ----
constexpr TalentPick kPaladinHoly[] = {
    { "Divine Intellect", 5 }, { "Spiritual Focus", 5 }, { "Healing Light", 3 }, { "Consecration", 1 },
    { "Divine Strength", 1 }, { "Illumination", 5 }, { "Divine Favor", 1 }, { "Improved Blessing of Wisdom", 2 },
    { "Improved Lay on Hands", 2 }, { "Holy Power", 5 }, { "Holy Shock", 1 },
    { "Redoubt", 5 }, { "Guardian's Favor", 2 }, { "Toughness", 3 }, { "Blessing of Kings", 1 },
    { "Anticipation", 4 }, { "Improved Concentration Aura", 3 }, { "Improved Hammer of Justice", 2 },
};
constexpr TalentPick kPaladinProtection[] = {
    { "Redoubt", 5 }, { "Precision", 3 }, { "Toughness", 2 }, { "Blessing of Kings", 1 },
    { "Improved Righteous Fury", 3 }, { "Shield Specialization", 1 }, { "Anticipation", 5 },
    { "Blessing of Sanctuary", 1 }, { "Improved Hammer of Justice", 3 }, { "Guardian's Favor", 1 },
    { "One-Handed Weapon Specialization", 5 }, { "Holy Shield", 1 },
    { "Divine Strength", 5 }, { "Divine Intellect", 5 }, { "Improved Seal of Righteousness", 5 },
    { "Consecration", 1 }, { "Unyielding Faith", 2 }, { "Improved Lay on Hands", 2 },
};
constexpr TalentPick kPaladinRetribution[] = {
    { "Improved Blessing of Might", 5 }, { "Benediction", 5 }, { "Improved Judgement", 2 },
    { "Improved Seal of the Crusader", 3 }, { "Seal of Command", 1 }, { "Conviction", 5 },
    { "Two-Handed Weapon Specialization", 3 }, { "Sanctity Aura", 1 }, { "Vengeance", 5 }, { "Repentance", 1 },
    { "Divine Strength", 5 }, { "Divine Intellect", 5 }, { "Improved Seal of Righteousness", 5 },
    { "Consecration", 1 }, { "Healing Light", 3 }, { "Unyielding Faith", 1 },
};

// ---- Hunter ----
constexpr TalentPick kHunterBeastMastery[] = {
    { "Improved Aspect of the Hawk", 5 }, { "Endurance Training", 5 }, { "Unleashed Fury", 5 }, { "Ferocity", 5 },
    { "Intimidation", 1 }, { "Spirit Bond", 2 }, { "Bestial Discipline", 2 }, { "Frenzy", 5 },
    { "Bestial Wrath", 1 },
    { "Efficiency", 5 }, { "Lethal Shots", 5 }, { "Aimed Shot", 1 }, { "Hawk Eye", 3 },
    { "Improved Arcane Shot", 1 }, { "Mortal Shots", 5 },
};
constexpr TalentPick kHunterMarksmanship[] = {
    { "Efficiency", 5 }, { "Lethal Shots", 5 }, { "Aimed Shot", 1 }, { "Hawk Eye", 3 },
    { "Improved Arcane Shot", 1 }, { "Mortal Shots", 5 }, { "Barrage", 3 }, { "Scatter Shot", 1 },
    { "Improved Hunter's Mark", 1 }, { "Ranged Weapon Specialization", 5 }, { "Trueshot Aura", 1 },
    { "Monster Slaying", 3 }, { "Humanoid Slaying", 2 }, { "Entrapment", 3 }, { "Survivalist", 5 },
    { "Surefooted", 3 }, { "Trap Mastery", 2 },
};
constexpr TalentPick kHunterSurvival[] = {
    { "Monster Slaying", 3 }, { "Humanoid Slaying", 3 }, { "Savage Strikes", 2 }, { "Entrapment", 2 },
    { "Survivalist", 5 }, { "Deterrence", 1 }, { "Surefooted", 3 }, { "Trap Mastery", 1 },
    { "Killer Instinct", 3 }, { "Improved Feign Death", 2 }, { "Lightning Reflexes", 5 }, { "Wyvern Sting", 1 },
    { "Efficiency", 5 }, { "Lethal Shots", 5 }, { "Aimed Shot", 1 }, { "Hawk Eye", 3 },
    { "Improved Arcane Shot", 1 }, { "Mortal Shots", 5 },
};

// ---- Rogue ----
constexpr TalentPick kRogueAssassination[] = {
    { "Malice", 5 }, { "Murder", 2 }, { "Ruthlessness", 3 }, { "Improved Slice and Dice", 3 },
    { "Relentless Strikes", 1 }, { "Lethality", 5 }, { "Vile Poisons", 1 }, { "Cold Blood", 1 },
    { "Improved Poisons", 4 }, { "Seal Fate", 5 }, { "Vigor", 1 },
    { "Improved Sinister Strike", 2 }, { "Lightning Reflexes", 3 }, { "Precision", 5 }, { "Endurance", 2 },
    { "Riposte", 1 }, { "Improved Backstab", 2 }, { "Dual Wield Specialization", 5 },
};
constexpr TalentPick kRogueCombat[] = {
    { "Improved Sinister Strike", 2 }, { "Lightning Reflexes", 3 }, { "Precision", 5 }, { "Riposte", 1 },
    { "Endurance", 2 }, { "Improved Sprint", 2 }, { "Dual Wield Specialization", 5 }, { "Blade Flurry", 1 },
    { "Sword Specialization", 5 }, { "Weapon Expertise", 2 }, { "Aggression", 2 }, { "Adrenaline Rush", 1 },
    { "Malice", 5 }, { "Murder", 2 }, { "Ruthlessness", 3 }, { "Improved Slice and Dice", 3 },
    { "Relentless Strikes", 1 }, { "Lethality", 1 },
    { "Opportunity", 5 },
};
constexpr TalentPick kRogueSubtlety[] = {
    { "Opportunity", 5 }, { "Sleight of Hand", 2 }, { "Camouflage", 3 }, { "Initiative", 3 },
    { "Improved Ambush", 3 }, { "Ghostly Strike", 1 }, { "Serrated Blades", 3 }, { "Hemorrhage", 1 },
    { "Preparation", 1 }, { "Dirty Deeds", 2 }, { "Setup", 1 }, { "Deadliness", 5 }, { "Premeditation", 1 },
    { "Improved Sinister Strike", 2 }, { "Lightning Reflexes", 3 }, { "Precision", 5 }, { "Endurance", 2 },
    { "Riposte", 1 }, { "Improved Backstab", 2 }, { "Dual Wield Specialization", 5 },
};

// ---- Priest ----
constexpr TalentPick kPriestDiscipline[] = {
    { "Unbreakable Will", 5 }, { "Improved Power Word: Fortitude", 2 }, { "Improved Power Word: Shield", 3 },
    { "Inner Focus", 1 }, { "Meditation", 3 }, { "Martyrdom", 1 }, { "Mental Agility", 5 },
    { "Divine Spirit", 1 }, { "Mental Strength", 4 }, { "Force of Will", 5 }, { "Power Infusion", 1 },
    { "Improved Renew", 3 }, { "Holy Specialization", 2 }, { "Divine Fury", 5 }, { "Inspiration", 3 },
    { "Holy Nova", 1 }, { "Healing Focus", 1 }, { "Improved Healing", 3 }, { "Holy Reach", 2 },
};
constexpr TalentPick kPriestHoly[] = {
    { "Improved Renew", 3 }, { "Holy Specialization", 2 }, { "Divine Fury", 5 }, { "Inspiration", 3 },
    { "Holy Nova", 1 }, { "Healing Focus", 1 }, { "Improved Healing", 3 }, { "Holy Reach", 2 },
    { "Spirit of Redemption", 1 }, { "Spiritual Guidance", 4 }, { "Spiritual Healing", 5 },
    { "Unbreakable Will", 5 }, { "Improved Power Word: Fortitude", 2 }, { "Improved Power Word: Shield", 3 },
    { "Meditation", 3 }, { "Inner Focus", 1 }, { "Martyrdom", 1 }, { "Mental Agility", 5 }, { "Divine Spirit", 1 },
};
constexpr TalentPick kPriestShadow[] = {
    { "Spirit Tap", 5 }, { "Improved Shadow Word: Pain", 2 }, { "Shadow Focus", 5 }, { "Mind Flay", 1 },
    { "Improved Mind Blast", 2 }, { "Shadow Weaving", 5 }, { "Vampiric Embrace", 1 }, { "Silence", 1 },
    { "Shadow Reach", 3 }, { "Darkness", 5 }, { "Shadowform", 1 },
    { "Wand Specialization", 5 }, { "Improved Power Word: Fortitude", 2 }, { "Improved Power Word: Shield", 3 },
    { "Inner Focus", 1 }, { "Meditation", 3 }, { "Martyrdom", 1 }, { "Mental Agility", 5 },
};

// ---- Shaman ----
constexpr TalentPick kShamanElemental[] = {
    { "Convection", 5 }, { "Concussion", 5 }, { "Elemental Focus", 1 }, { "Call of Thunder", 5 },
    { "Elemental Devastation", 3 }, { "Improved Fire Totems", 1 }, { "Elemental Fury", 1 }, { "Storm Reach", 2 },
    { "Call of Flame", 2 }, { "Lightning Mastery", 5 }, { "Elemental Mastery", 1 },
    { "Tidal Focus", 5 }, { "Totemic Focus", 5 }, { "Nature's Guidance", 3 }, { "Totemic Mastery", 1 },
    { "Healing Grace", 1 }, { "Tidal Mastery", 5 },
};
constexpr TalentPick kShamanEnhancement[] = {
    { "Ancestral Knowledge", 5 }, { "Thundering Strikes", 5 }, { "Two-Handed Axes and Maces", 1 },
    { "Anticipation", 4 }, { "Flurry", 5 }, { "Elemental Weapons", 3 }, { "Improved Weapon Totems", 2 },
    { "Weapon Mastery", 5 }, { "Stormstrike", 1 },
    { "Tidal Focus", 5 }, { "Totemic Focus", 5 }, { "Nature's Guidance", 3 }, { "Totemic Mastery", 1 },
    { "Healing Grace", 1 }, { "Restorative Totems", 5 },
};
constexpr TalentPick kShamanRestoration[] = {
    { "Improved Healing Wave", 5 }, { "Tidal Focus", 5 }, { "Nature's Guidance", 3 }, { "Totemic Mastery", 1 },
    { "Healing Focus", 1 }, { "Tidal Mastery", 5 }, { "Nature's Swiftness", 1 }, { "Healing Way", 3 },
    { "Restorative Totems", 1 }, { "Purification", 5 }, { "Mana Tide Totem", 1 },
    { "Restorative Totems", 5 }, { "Healing Grace", 3 }, { "Ancestral Healing", 3 }, { "Totemic Focus", 5 },
    { "Healing Focus", 5 }, { "Improved Reincarnation", 1 },
};

// ---- Mage ----
constexpr TalentPick kMageArcane[] = {
    { "Arcane Subtlety", 2 }, { "Arcane Focus", 3 }, { "Arcane Concentration", 5 },
    { "Improved Arcane Explosion", 3 }, { "Arcane Resilience", 1 }, { "Magic Absorption", 1 },
    { "Arcane Meditation", 3 }, { "Improved Arcane Missiles", 2 }, { "Presence of Mind", 1 },
    { "Arcane Mind", 5 }, { "Arcane Instability", 3 }, { "Magic Attunement", 1 }, { "Arcane Power", 1 },
    { "Improved Frostbolt", 5 }, { "Ice Shards", 5 }, { "Piercing Ice", 3 }, { "Frostbite", 2 },
    { "Frost Channeling", 3 }, { "Elemental Precision", 2 },
};
constexpr TalentPick kMageFire[] = {
    { "Improved Fireball", 5 }, { "Ignite", 5 }, { "Pyroblast", 1 }, { "Improved Flamestrike", 3 },
    { "Burning Soul", 1 }, { "Improved Scorch", 3 }, { "Master of Elements", 2 }, { "Critical Mass", 3 },
    { "Blast Wave", 1 }, { "Impact", 1 }, { "Fire Power", 5 }, { "Combustion", 1 },
    { "Arcane Subtlety", 2 }, { "Arcane Focus", 3 }, { "Arcane Concentration", 5 },
    { "Improved Arcane Explosion", 3 }, { "Magic Absorption", 2 }, { "Arcane Meditation", 3 },
    { "Improved Arcane Missiles", 2 },
};
constexpr TalentPick kMageFrost[] = {
    { "Improved Frostbolt", 5 }, { "Ice Shards", 5 }, { "Piercing Ice", 3 }, { "Cold Snap", 1 },
    { "Improved Blizzard", 1 }, { "Frost Channeling", 3 }, { "Shatter", 2 }, { "Ice Block", 1 },
    { "Elemental Precision", 3 }, { "Frostbite", 1 }, { "Winter's Chill", 5 }, { "Ice Barrier", 1 },
    { "Arcane Subtlety", 2 }, { "Arcane Focus", 3 }, { "Arcane Concentration", 5 },
    { "Improved Arcane Explosion", 3 }, { "Magic Absorption", 2 }, { "Arcane Meditation", 3 },
    { "Improved Arcane Missiles", 2 },
};

// ---- Warlock ----
constexpr TalentPick kWarlockAffliction[] = {
    { "Improved Corruption", 5 }, { "Improved Life Tap", 2 }, { "Improved Drain Soul", 2 },
    { "Improved Curse of Weakness", 1 }, { "Improved Curse of Agony", 3 }, { "Amplify Curse", 1 },
    { "Fel Concentration", 1 }, { "Nightfall", 2 }, { "Grim Reach", 2 }, { "Siphon Life", 1 },
    { "Suppression", 5 }, { "Shadow Mastery", 5 }, { "Dark Pact", 1 },
    { "Demonic Embrace", 5 }, { "Fel Intellect", 5 }, { "Fel Domination", 1 }, { "Fel Stamina", 4 },
    { "Master Summoner", 2 }, { "Unholy Power", 2 }, { "Improved Healthstone", 1 },
};
constexpr TalentPick kWarlockDemonology[] = {
    { "Demonic Embrace", 5 }, { "Fel Intellect", 5 }, { "Fel Domination", 1 }, { "Fel Stamina", 4 },
    { "Unholy Power", 5 }, { "Demonic Sacrifice", 1 }, { "Master Summoner", 2 }, { "Improved Healthstone", 2 },
    { "Master Demonologist", 5 }, { "Soul Link", 1 },
    { "Improved Shadow Bolt", 5 }, { "Bane", 5 }, { "Devastation", 5 }, { "Destructive Reach", 2 },
    { "Improved Firebolt", 2 }, { "Cataclysm", 1 },
};
constexpr TalentPick kWarlockDestruction[] = {
    { "Improved Shadow Bolt", 5 }, { "Bane", 5 }, { "Devastation", 5 }, { "Destructive Reach", 2 },
    { "Improved Firebolt", 2 }, { "Cataclysm", 1 }, { "Ruin", 1 }, { "Improved Immolate", 4 },
    { "Emberstorm", 5 }, { "Conflagrate", 1 },
    { "Demonic Embrace", 5 }, { "Fel Intellect", 5 }, { "Fel Domination", 1 }, { "Fel Stamina", 4 },
    { "Master Summoner", 2 }, { "Unholy Power", 2 }, { "Improved Healthstone", 1 },
};

// ---- Druid ----
constexpr TalentPick kDruidBalance[] = {
    { "Improved Wrath", 5 }, { "Improved Moonfire", 5 }, { "Omen of Clarity", 1 }, { "Nature's Reach", 2 },
    { "Natural Weapons", 2 }, { "Vengeance", 5 }, { "Nature's Grace", 1 }, { "Moonglow", 3 },
    { "Improved Starfire", 1 }, { "Moonfury", 5 }, { "Moonkin Form", 1 },
    { "Furor", 5 }, { "Nature's Focus", 5 }, { "Reflection", 3 }, { "Improved Mark of the Wild", 2 },
    { "Improved Rejuvenation", 3 }, { "Tranquil Spirit", 2 },
};
constexpr TalentPick kDruidFeralCat[] = {
    { "Ferocity", 5 }, { "Feral Aggression", 5 }, { "Sharpened Claws", 3 }, { "Feline Swiftness", 2 },
    { "Improved Shred", 2 }, { "Predatory Strikes", 3 }, { "Blood Frenzy", 2 }, { "Savage Fury", 2 },
    { "Faerie Fire (Feral)", 1 }, { "Heart of the Wild", 5 }, { "Leader of the Pack", 1 },
    { "Furor", 5 }, { "Improved Mark of the Wild", 5 }, { "Improved Enrage", 2 }, { "Nature's Focus", 3 },
    { "Reflection", 3 }, { "Subtlety", 2 },
};
constexpr TalentPick kDruidFeralBear[] = {
    { "Ferocity", 5 }, { "Thick Hide", 5 }, { "Feral Charge", 1 }, { "Sharpened Claws", 3 },
    { "Feline Swiftness", 1 }, { "Predatory Strikes", 3 }, { "Primal Fury", 2 }, { "Faerie Fire (Feral)", 1 },
    { "Savage Fury", 2 }, { "Blood Frenzy", 2 }, { "Heart of the Wild", 5 }, { "Leader of the Pack", 1 },
    { "Furor", 5 }, { "Improved Mark of the Wild", 5 }, { "Improved Enrage", 2 }, { "Nature's Focus", 3 },
    { "Reflection", 3 }, { "Subtlety", 2 },
};
constexpr TalentPick kDruidRestoration[] = {
    { "Furor", 5 }, { "Improved Healing Touch", 5 }, { "Reflection", 3 }, { "Improved Mark of the Wild", 2 },
    { "Tranquil Spirit", 5 }, { "Nature's Swiftness", 1 }, { "Improved Rejuvenation", 3 },
    { "Gift of Nature", 5 }, { "Improved Regrowth", 5 }, { "Swiftmend", 1 }, { "Subtlety", 5 },
    { "Improved Wrath", 5 }, { "Improved Moonfire", 4 }, { "Natural Weapons", 1 }, { "Omen of Clarity", 1 },
};

// Class default first: it wins ties when a bot's talents fit several builds.
constexpr std::array<TalentBuild, 28> kBuilds = {{
    { CLASS_WARRIOR, 71,  kWarriorArms },
    { CLASS_WARRIOR, 72,  kWarriorFury },
    { CLASS_WARRIOR, 73,  kWarriorProtection },
    { CLASS_PALADIN, 70,  kPaladinRetribution },
    { CLASS_PALADIN, 65,  kPaladinHoly },
    { CLASS_PALADIN, 66,  kPaladinProtection },
    { CLASS_HUNTER,  254, kHunterMarksmanship },
    { CLASS_HUNTER,  253, kHunterBeastMastery },
    { CLASS_HUNTER,  255, kHunterSurvival },
    { CLASS_ROGUE,   260, kRogueCombat },
    { CLASS_ROGUE,   259, kRogueAssassination },
    { CLASS_ROGUE,   261, kRogueSubtlety },
    { CLASS_PRIEST,  257, kPriestHoly },
    { CLASS_PRIEST,  256, kPriestDiscipline },
    { CLASS_PRIEST,  258, kPriestShadow },
    { CLASS_SHAMAN,  263, kShamanEnhancement },
    { CLASS_SHAMAN,  262, kShamanElemental },
    { CLASS_SHAMAN,  264, kShamanRestoration },
    { CLASS_MAGE,    64,  kMageFrost },
    { CLASS_MAGE,    63,  kMageFire },
    { CLASS_MAGE,    62,  kMageArcane },
    { CLASS_WARLOCK, 265, kWarlockAffliction },
    { CLASS_WARLOCK, 266, kWarlockDemonology },
    { CLASS_WARLOCK, 267, kWarlockDestruction },
    { CLASS_DRUID,   102, kDruidBalance },
    { CLASS_DRUID,   103, kDruidFeralCat },
    { CLASS_DRUID,   104, kDruidFeralBear },
    { CLASS_DRUID,   105, kDruidRestoration },
}};

} // namespace

std::span<TalentBuild const> GetTalentBuilds()
{
    return kBuilds;
}

std::span<TalentPick const> GetTalentBuild(uint8 cls, uint32 spec)
{
    for (TalentBuild const& b : kBuilds)
        if (b.cls == cls && b.spec == spec)
            return b.picks;
    return {};
}

} // namespace Playerbot::Classic
