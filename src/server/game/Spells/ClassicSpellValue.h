/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TRINITY_CLASSIC_SPELL_VALUE_H
#define TRINITY_CLASSIC_SPELL_VALUE_H

#include <cmath>
#include <cstdint>

// Classic 1.60: vanilla (1.12) damage / heal / absorb values of the spells every caster casts.
//
// The 70291 client's SpellEffect.db2 carries values that differ from vanilla for part of the legacy spells. Creatures: the Defias
// Evoker's Flamestrike 11829 has base points 479.45 where vanilla is 75 + 1d13 (76..88), so it killed 650 HP players in two casts
// (Deadmines, 2026-10-09). Players: a level 60 warlock's Shadow Bolt rank 10 (25307) rolled 254 and 261 where vanilla is
// 481 + 1d57 (482..538), half of it (2026-10-09, [spell_dmg] of Karrz). The table classic_spell_value holds the vanilla dice of every
// damaging / healing / absorbing effect of the VMaNGOS 1.12 spell_template; a legacy spell takes its base value from there whoever
// casts it (players, bots, pets, totems, creatures). Everything that follows stays as it was: points per level, talent and aura
// modifiers (ApplyEffectModifiers), the spell power coefficient and the damage / healing done and taken auras. Effects whose type
// differs from the client's, and spells new to Forever, keep their DB2 value. Pure: no game types, tested in
// tests/game/ClassicSpellValue.cpp.

namespace ClassicSpellValue
{
    // One effect of a vanilla spell (VMaNGOS spell_template effectBasePointsN / effectDieSidesN / effectRealPointsPerLevelN).
    struct Value
    {
        uint8_t Effect = 0;            // SpellEffects, checked against the DB2 effect before the value is used
        uint16_t ApplyAura = 0;        // AuraType of an APPLY_AURA effect, else 0, checked the same way
        int32_t BasePoints = 0;        // vanilla "base points": the dice come on top of it
        int32_t DieSides = 0;          // vanilla die sides: 0 none, 1 adds one, n adds 1..n
        float PointsPerLevel = 0.0f;   // vanilla real points per level
    };

    // The average roll: base points plus the mean of 1..sides (Flamestrike 75 + 1d13 = 82, its DoT 15 + 1 = 16, Smash 809 + 1d181 = 900).
    inline float Mean(Value const& v)
    {
        return float(v.BasePoints) + (v.DieSides >= 1 ? (1.0f + float(v.DieSides)) * 0.5f : 0.0f);
    }

    // SpellEffectInfo::Scaling.Variance of the same roll: the spread (sides - 1) as a fraction of the average; the core rolls
    // +-half of it around the average (Smash 810..990 = 900 +-10% = variance 0.2).
    inline float Variance(Value const& v)
    {
        float const mean = std::fabs(Mean(v));
        return v.DieSides > 1 && mean > 0.0f ? float(v.DieSides - 1) / mean : 0.0f;
    }

    // The row describes the same effect as the DB2's: a different effect type or aura at that index means the vanilla and the
    // client spell are not the same spell any more (index reorder, rework) and the DB2 value stays.
    inline bool MatchesEffect(Value const& v, uint32_t effect, uint32_t applyAura)
    {
        return v.Effect == effect && v.ApplyAura == applyAura;
    }

    // Spell ids from this on are new to Forever and carry percentages of the expected spell damage (SpellInfo.cpp); they have no
    // vanilla row anyway.
    constexpr uint32_t FirstScaledSpellId = 1000000;

    // Who takes the vanilla value: any caster of a legacy spell (player, bot, pet, totem or creature).
    inline bool AppliesToSpell(uint32_t spellId)
    {
        return spellId < FirstScaledSpellId;
    }

    // The base points of a vanilla effect at a caster level: base points and mean die, plus points per level for the levels the
    // caster has above the spell (level above the spell's MaxLevel does not count). What SpellEffectInfo::CalcValue computes
    // before the modifiers.
    inline float ValueAtLevel(Value const& v, int32_t casterLevel, int32_t spellBaseLevel, int32_t spellMaxLevel)
    {
        float value = Mean(v);
        if (v.PointsPerLevel != 0.0f)
        {
            int32_t level = casterLevel;
            if (spellMaxLevel > 0 && level > spellMaxLevel)
                level = spellMaxLevel;
            level -= spellBaseLevel;
            if (level < 0)
                level = 0;
            value += float(level) * v.PointsPerLevel;
        }
        return value;
    }
}

#endif
