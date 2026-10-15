#include "ClassicSpec.h"
#include "ClassicTalentBuilds.h"
#include "DB2Stores.h"
#include "Player.h"
#include "SharedDefines.h"
#include "DBCEnums.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TraitMgr.h"
#include "TraitPacketsCommon.h"
#include "World.h"
#include <array>
#include <cctype>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Playerbot::Classic {

namespace {

struct ClassSpecs
{
    uint8 cls;
    std::array<uint32, 4> specs;   // retail ChrSpecialization ids, 0 = unused
};

constexpr std::array<ClassSpecs, 13> kRetailSpecs = {{
    { CLASS_WARRIOR,      { 71, 72, 73, 0 } },
    { CLASS_PALADIN,      { 65, 66, 70, 0 } },
    { CLASS_HUNTER,       { 253, 254, 255, 0 } },
    { CLASS_ROGUE,        { 259, 260, 261, 0 } },
    { CLASS_PRIEST,       { 256, 257, 258, 0 } },
    { CLASS_DEATH_KNIGHT, { 250, 251, 252, 0 } },
    { CLASS_SHAMAN,       { 262, 263, 264, 0 } },
    { CLASS_MAGE,         { 62, 63, 64, 0 } },
    { CLASS_WARLOCK,      { 265, 266, 267, 0 } },
    { CLASS_MONK,         { 268, 269, 270, 0 } },
    { CLASS_DRUID,        { 102, 103, 104, 105 } },
    { CLASS_DEMON_HUNTER, { 577, 581, 1480, 0 } },
    { CLASS_EVOKER,       { 1467, 1468, 1473, 0 } },
}};

// Vanilla talent spells (rank 1 ids) that identify a talent build, checked in
// order: the first one the player knows decides. Deep talents of the less
// ambiguous tree come first (a Shadow priest also has Inner Focus).
struct Signature
{
    uint32 spell;
    uint32 spec;
};

struct ClassRules
{
    uint8 cls;
    uint32 fallback;
    std::array<Signature, 8> signatures;   // spell 0 = unused
};

constexpr std::array<ClassRules, 9> kRules = {{
    { CLASS_WARRIOR, 71, {{
        { 23922, 73 },   // Shield Slam
        { 12809, 73 },   // Concussion Blow
        { 23881, 72 },   // Bloodthirst
        { 12294, 71 },   // Mortal Strike
    }}},
    { CLASS_PALADIN, 70, {{
        { 20473, 65 },   // Holy Shock
        { 20925, 66 },   // Holy Shield
        { 20066, 70 },   // Repentance
        { 20216, 65 },   // Divine Favor
        { 20375, 70 },   // Seal of Command
    }}},
    { CLASS_HUNTER, 254, {{
        { 19574, 253 },  // Bestial Wrath
        // Vanilla Survival is still a ranged spec; retail 255 is melee, so
        // Survival talents map to Marksmanship (the fallback).
    }}},
    { CLASS_ROGUE, 260, {{
        { 14177, 259 },  // Cold Blood
        { 13750, 260 },  // Adrenaline Rush
        { 14185, 261 },  // Preparation
        { 16511, 261 },  // Hemorrhage
        { 13877, 260 },  // Blade Flurry
    }}},
    { CLASS_PRIEST, 257, {{
        { 15473, 258 },  // Shadowform
        { 15286, 258 },  // Vampiric Embrace
        { 15407, 258 },  // Mind Flay
        { 10060, 256 },  // Power Infusion
        { 20711, 257 },  // Spirit of Redemption
        { 724,   257 },  // Lightwell
    }}},
    { CLASS_SHAMAN, 263, {{
        { 16190, 264 },  // Mana Tide Totem
        { 16188, 264 },  // Nature's Swiftness
        { 17364, 263 },  // Stormstrike
        { 16166, 262 },  // Elemental Mastery
    }}},
    { CLASS_MAGE, 64, {{
        { 12042, 62 },   // Arcane Power
        { 11129, 63 },   // Combustion
        { 11426, 64 },   // Ice Barrier
        { 11366, 63 },   // Pyroblast
        { 12043, 62 },   // Presence of Mind
    }}},
    { CLASS_WARLOCK, 265, {{
        { 18220, 265 },  // Dark Pact
        { 18265, 265 },  // Siphon Life
        { 19028, 266 },  // Soul Link
        { 18788, 266 },  // Demonic Sacrifice
        { 17962, 267 },  // Conflagrate
        { 17877, 267 },  // Shadowburn
    }}},
    { CLASS_DRUID, 102, {{
        { 24858, 102 },  // Moonkin Form
        { 18562, 105 },  // Swiftmend
        { 17116, 105 },  // Nature's Swiftness
        { 17007, 104 },  // Leader of the Pack (vanilla ferals tank in raids)
        { 16979, 104 },  // Feral Charge
    }}},
}};

std::shared_mutex g_overrideLock;
std::unordered_map<ObjectGuid, uint32> g_overrides;

// Talent name <-> trait node entry, per class. The class trees never change
// after load, so each class is resolved once.
struct ClassTalentIndex
{
    std::unordered_map<std::string, TalentNode> byName;              // TalentKey -> node
    std::map<std::pair<int32, int32>, std::string> nameByEntry;     // node -> TalentKey of its first name
};

// Lookup key of a talent name: lower case letters and digits only, so the
// build's "Two-Handed Weapon Specialization" matches "Two Handed Weapon
// Specialization" or a different capitalisation in the client data.
std::string TalentKey(std::string_view name)
{
    std::string key;
    key.reserve(name.size());
    for (char c : name)
        if (std::isalnum(static_cast<unsigned char>(c)))
            key.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    return key;
}

std::mutex g_indexLock;
std::unordered_map<uint8, ClassTalentIndex> g_indexes;

// Every name a talent goes by: the definition's override name and the names
// of its spell and its visible spell (the tooltip shows the override name if
// set, else the visible spell's; a build may use either).
std::vector<std::string> TalentNamesOf(int32 traitDefinitionId)
{
    std::vector<std::string> names;
    TraitDefinitionEntry const* def = sTraitDefinitionStore.LookupEntry(traitDefinitionId);
    if (!def)
        return names;
    LocaleConstant const locale = sWorld->GetDefaultDbcLocale();
    if (char const* overrideName = def->OverrideName[locale]; overrideName && *overrideName)
        names.emplace_back(overrideName);
    for (int32 spellId : { def->SpellID, def->VisibleSpellID })
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(uint32(spellId), DIFFICULTY_NONE))
            if (char const* name = info->SpellName->Str[locale]; name && *name)
                names.emplace_back(name);
    return names;
}

ClassTalentIndex const* GetClassTalentIndex(Player const* player)
{
    uint8 const cls = player->GetClass();
    std::lock_guard lock(g_indexLock);
    if (auto itr = g_indexes.find(cls); itr != g_indexes.end())
        return &itr->second;

    UF::TraitConfig const* active = player->GetTraitConfig(player->m_activePlayerData->ActiveCombatTraitConfigID);
    if (!active)
        return nullptr;

    ClassTalentIndex& index = g_indexes[cls];
    for (TraitMgr::NodeEntryInfo const& e : TraitMgr::GetNodeEntriesForConfig(WorldPackets::Traits::TraitConfig(*active)))
    {
        for (std::string const& name : TalentNamesOf(e.TraitDefinitionID))
        {
            std::string key = TalentKey(name);
            if (key.empty())
                continue;
            index.nameByEntry.try_emplace({ e.TraitNodeID, e.TraitNodeEntryID }, key);
            index.byName.try_emplace(std::move(key), TalentNode{ e.TraitNodeID, e.TraitNodeEntryID, e.MaxRanks });
        }
    }
    return &index;
}

// Which build do the player's purchased talents fit best? Each rank counts
// for a spec whose build lists that talent (up to the build's ranks). 0 when
// the player has no purchased talents; ties go to the earlier build.
uint32 InferFromTalents(Player const* player)
{
    UF::TraitConfig const* active = player->GetTraitConfig(player->m_activePlayerData->ActiveCombatTraitConfigID);
    if (!active)
        return 0;
    ClassTalentIndex const* index = GetClassTalentIndex(player);
    if (!index)
        return 0;

    std::unordered_map<std::string, int32> owned;
    for (UF::TraitEntry const& e : active->Entries)
        if (e.Rank > 0)
            if (auto itr = index->nameByEntry.find({ e.TraitNodeID, e.TraitNodeEntryID }); itr != index->nameByEntry.end())
                owned[itr->second] += e.Rank;
    if (owned.empty())
        return 0;

    uint32 bestSpec = 0;
    int32 bestScore = 0;
    for (TalentBuild const& build : GetTalentBuilds())
    {
        if (build.cls != player->GetClass())
            continue;
        int32 score = 0;
        for (TalentPick const& pick : build.picks)
            if (auto itr = owned.find(TalentKey(pick.name)); itr != owned.end())
                score += std::min<int32>(itr->second, pick.ranks);
        if (score > bestScore)
        {
            bestScore = score;
            bestSpec = build.spec;
        }
    }
    return bestSpec;
}

} // namespace

TalentNode const* FindTalent(Player const* player, std::string const& name)
{
    ClassTalentIndex const* index = player ? GetClassTalentIndex(player) : nullptr;
    if (!index)
        return nullptr;
    auto itr = index->byName.find(TalentKey(name));
    return itr != index->byName.end() ? &itr->second : nullptr;
}

bool IsRetailClassSpec(uint8 cls, uint32 spec)
{
    if (!spec)
        return false;
    for (ClassSpecs const& c : kRetailSpecs)
        if (c.cls == cls)
            for (uint32 s : c.specs)
                if (s == spec)
                    return true;
    return false;
}

uint32 InferClassicSpec(Player const* player)
{
    if (!player)
        return 0;
    uint8 const cls = player->GetClass();
    // Talents bought from our builds say it best; the signature spells below
    // cover characters without trait-config talents (or none at all yet).
    if (uint32 fromTalents = InferFromTalents(player))
        return fromTalents;
    for (ClassRules const& r : kRules)
    {
        if (r.cls != cls)
            continue;
        for (Signature const& sig : r.signatures)
            if (sig.spell && player->HasSpell(sig.spell))
                return sig.spec;
        return r.fallback;
    }
    return 0;
}

uint32 EffectiveSpecId(Player const* player)
{
    if (!player)
        return 0;
    uint32 const real = uint32(AsUnderlyingType(player->GetPrimarySpecialization()));
    if (IsRetailClassSpec(player->GetClass(), real))
        return real;
    if (uint32 forced = GetSpecOverride(player->GetGUID()))
        return forced;
    if (uint32 inferred = InferClassicSpec(player))
        return inferred;
    return real;
}

bool SetSpecOverride(Player const* player, uint32 spec)
{
    if (!player)
        return false;
    uint32 const real = uint32(AsUnderlyingType(player->GetPrimarySpecialization()));
    if (IsRetailClassSpec(player->GetClass(), real))
        return false;
    std::unique_lock lock(g_overrideLock);
    if (!spec)
    {
        g_overrides.erase(player->GetGUID());
        return true;
    }
    if (!IsRetailClassSpec(player->GetClass(), spec))
        return false;
    g_overrides[player->GetGUID()] = spec;
    return true;
}

uint32 GetSpecOverride(ObjectGuid guid)
{
    std::shared_lock lock(g_overrideLock);
    auto itr = g_overrides.find(guid);
    return itr != g_overrides.end() ? itr->second : 0;
}

bool IsSingleSpecClass(uint8 cls)
{
    return sDB2Manager.GetChrSpecializationByIndex(cls, 0) && !sDB2Manager.GetChrSpecializationByIndex(cls, 1);
}

} // namespace Playerbot::Classic
