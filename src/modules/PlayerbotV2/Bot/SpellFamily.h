// SpellFamily.h - the ranks of a spell when the spell data has no rank chain.
//
// SpellMgr builds rank chains from SkillLineAbility.SupercedesSpell, which is empty in the 70205 data for most class spells
// (only ~605 chains: Healing Touch, Regrowth, Holy Light, Flash of Light, Greater Heal, Flash Heal, Frostbolt, Fireball, Shadow Bolt,
// Banish, the curses, ... have none). GetFirstSpellInChain then returns the id itself, so every rank-1 id the rotations hold was
// cast, tracked and compared as rank 1 (MC Garr 2026-10-08: druids cast Healing Touch 5185 knowing 9889, paladins Flash of Light
// 19750 knowing 19943). A "family" is every spell of the same name, spell family and first effect (effect type and aura type);
// its ranks ascend by base level. A real chain wins where there is one.
#pragma once

#include "Define.h"
#include "Log.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "World.h"
#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Playerbot::SpellFamily {

namespace detail {

struct Index
{
    std::unordered_map<uint32, uint32> family_of;                  // spell id -> family number
    std::vector<std::vector<uint32>>   families;                   // ascending by base level, then id
};

inline std::string KeyOf(SpellInfo const* si, LocaleConstant loc)
{
    char const* n = si->SpellName ? (*si->SpellName)[loc] : nullptr;
    if (!n || !*n || si->GetEffects().empty())
        return {};
    SpellEffectInfo const& e0 = si->GetEffect(EFFECT_0);
    return std::string(n) + '|' + std::to_string(si->SpellFamilyName) + '|' + std::to_string(uint32(e0.Effect)) + '|' +
           std::to_string(uint32(e0.ApplyAuraName)) + '|' + (si->IsPassive() ? 'p' : 'a');
}

inline Index const& GetIndex()
{
    static Index idx;
    static std::once_flag once;
    std::call_once(once, []
    {
        LocaleConstant const loc = sWorld->GetDefaultDbcLocale();
        std::unordered_map<std::string, uint32> by_key;
        sSpellMgr->ForEachSpellInfo([&](SpellInfo const* si)
        {
            if (!si || si->Difficulty != DIFFICULTY_NONE)
                return;
            std::string key = KeyOf(si, loc);
            if (key.empty())
                return;
            auto [it, fresh] = by_key.try_emplace(std::move(key), uint32(idx.families.size()));
            if (fresh)
                idx.families.emplace_back();
            idx.families[it->second].push_back(si->Id);
            idx.family_of[si->Id] = it->second;
        });
        for (auto& f : idx.families)
            std::sort(f.begin(), f.end(), [](uint32 a, uint32 b)
            {
                SpellInfo const* sa = sSpellMgr->GetSpellInfo(a, DIFFICULTY_NONE);
                SpellInfo const* sb = sSpellMgr->GetSpellInfo(b, DIFFICULTY_NONE);
                uint32 const la = sa ? sa->BaseLevel : 0, lb = sb ? sb->BaseLevel : 0;
                return la != lb ? la < lb : a < b;
            });
        // [spell_data] (client moves 70205 -> 70291): which of the curses and tank / raid debuff spells the loaded DB2 data carries and how
        // many ranks each family has, once at the first lookup. Curse of the Elements 1490 and Curse of Shadow 17862 were missing in 70205.
        for (uint32 const id : { 704u, 702u, 980u, 1490u, 17862u, 1714u, 8647u, 7386u, 20271u, 1130u })
        {
            SpellInfo const* si = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
            auto const fam = idx.family_of.find(id);
            TC_LOG_INFO("playerbot.v2", "[spell_data] {} {}: {}, {} rank(s) in its family", id,
                si && si->SpellName ? (*si->SpellName)[loc] : "-", si ? "present" : "MISSING",
                fam == idx.family_of.end() ? 0u : uint32(idx.families[fam->second].size()));
        }
    });
    return idx;
}

} // namespace detail

// Ranks of the spell, lowest first: the spell data's chain ranks together with every spell of the same family. The two overlap for
// most spells and differ for some: Mark of the Wild 1126 has a chain that holds only part of its ranks, and a bot that cast a rank
// outside the chain never saw the aura as "its" buff (MC Garr 2026-10-10: the druids recast it for ever, the tracked list stayed empty).
inline std::vector<uint32> Ranks(uint32 spell_id)
{
    std::vector<uint32> out;
    if (!spell_id)
        return out;
    if (sSpellMgr->GetSpellChainNode(spell_id))
    {
        uint32 rank = sSpellMgr->GetFirstSpellInChain(spell_id);
        for (int guard = 0; rank && guard < 32; ++guard, rank = sSpellMgr->GetNextSpellInChain(rank))
            out.push_back(rank);
    }
    detail::Index const& idx = detail::GetIndex();
    if (auto it = idx.family_of.find(spell_id); it != idx.family_of.end())
    {
        if (out.empty())
            return idx.families[it->second];
        bool added = false;
        for (uint32 const id : idx.families[it->second])
            if (std::find(out.begin(), out.end(), id) == out.end())
            { out.push_back(id); added = true; }
        if (added)
            std::sort(out.begin(), out.end(), [](uint32 a, uint32 b)
            {
                SpellInfo const* sa = sSpellMgr->GetSpellInfo(a, DIFFICULTY_NONE);
                SpellInfo const* sb = sSpellMgr->GetSpellInfo(b, DIFFICULTY_NONE);
                uint32 const la = sa ? sa->BaseLevel : 0, lb = sb ? sb->BaseLevel : 0;
                return la != lb ? la < lb : a < b;
            });
    }
    if (out.empty())
        out.push_back(spell_id);
    return out;
}

// One id every rank of the spell maps to (the chain's first rank, else the family's lowest).
inline uint32 Root(uint32 spell_id)
{
    if (!spell_id)
        return 0;
    if (sSpellMgr->GetSpellChainNode(spell_id))
        if (uint32 const f = sSpellMgr->GetFirstSpellInChain(spell_id))
            return f;
    detail::Index const& idx = detail::GetIndex();
    if (auto it = idx.family_of.find(spell_id); it != idx.family_of.end())
        return idx.families[it->second].front();
    return spell_id;
}

// Same spell, any rank: one root, or (a partial chain gives a rank outside it a different root) one family.
inline bool Same(uint32 a, uint32 b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (Root(a) == Root(b)) return true;
    detail::Index const& idx = detail::GetIndex();
    auto const fa = idx.family_of.find(a);
    auto const fb = idx.family_of.find(b);
    return fa != idx.family_of.end() && fb != idx.family_of.end() && fa->second == fb->second;
}

} // namespace Playerbot::SpellFamily
