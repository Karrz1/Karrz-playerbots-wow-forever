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

#ifndef ClassicSpellHit_h__
#define ClassicSpellHit_h__

#include <algorithm>
#include <cstdint>

// Classic 1.60 (vanilla, VMaNGOS SpellCaster::MagicSpellHitChance) base chance of a spell to hit, in percent, from the level difference
// alone (target level minus caster level): 96% at the same level, one point less per level up to +2 (94%), then a steep step: 83% at
// +3 and `perLevelStep` more per level (11 against creatures, 7 against players), never below 22%. Below the caster's level the chance
// rises one point per level (the caller caps the total at 99%). Retail instead loses 3% per level up to +3 (85%).
namespace ClassicSpellHit
{
    constexpr float MinBaseHitChance = 22.0f;
    constexpr float MaxHitChance = 99.0f;
    constexpr float MinHitChance = 1.0f;

    constexpr float PerLevelStep(bool targetIsPlayer)
    {
        return targetIsPlayer ? 7.0f : 11.0f;
    }

    constexpr float BaseHitChance(int32_t levelDifference, bool targetIsPlayer)
    {
        float const chance = levelDifference < 3 ? 96.0f - float(levelDifference)
                                                 : 94.0f - float(levelDifference - 2) * PerLevelStep(targetIsPlayer);
        return std::max(chance, MinBaseHitChance);
    }

    // The total after the modifiers (spell mods, hit gear, target auras): between 1% and 99%.
    constexpr float ClampHitChance(float chance)
    {
        return std::clamp(chance, MinHitChance, MaxHitChance);
    }
}

#endif // ClassicSpellHit_h__
