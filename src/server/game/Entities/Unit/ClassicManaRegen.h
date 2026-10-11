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

#ifndef TRINITY_CLASSIC_MANA_REGEN_H
#define TRINITY_CLASSIC_MANA_REGEN_H

// Classic 1.60 (vanilla): the "five second rule". Spirit mana regeneration stops for 5 seconds after a mana-costing spell is cast
// (only the share of SPELL_AURA_MOD_MANA_REGEN_INTERRUPT keeps running, e.g. Meditation, Mage Armor); after that it runs at the full
// rate, in or out of combat. Combat itself does not interrupt it (the retail core did that, and it never started the window on a
// cast, so a caster in combat got no spirit regeneration at all). Pure: tested in tests/game/ClassicManaRegen.cpp.
namespace ClassicManaRegen
{
    constexpr unsigned FiveSecondRuleMs = 5000;

    // True while the regeneration is interrupted: a mana spell was paid less than 5 s ago, or an aura interrupts it.
    inline bool IsInterrupted(unsigned msSinceLastManaSpell, bool interruptAura)
    {
        return interruptAura || msSinceLastManaSpell < FiveSecondRuleMs;
    }
}

#endif
