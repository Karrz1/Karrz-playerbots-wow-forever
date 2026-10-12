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

#ifndef ClassicBotSkills_h__
#define ClassicBotSkills_h__

#include <algorithm>
#include <cstdint>

// Classic 1.60 (vanilla) combat table skills of a bot. Defense and the weapon skills decide crushing blows (a creature whose maximum
// skill is 15 or more above the victim's Defense), glancing blows, dodge, parry and miss. A player earns them by use; a bot never
// would, so API::learn_weapon_skills_free sets them to what a player who trained them has: five per level.
namespace ClassicBotSkills
{
    constexpr uint16_t PerLevel = 5;

    // The value a combat skill is raised to: the level's cap, or the skill's own maximum when that is higher (a racial bonus is a
    // modifier on top and never part of the stored rank, so in practice the cap).
    constexpr uint16_t Target(uint32_t level, uint16_t currentMax)
    {
        return std::max<uint16_t>(uint16_t(level * PerLevel), currentMax);
    }

    // Whether the stored rank or the stored maximum is below the target.
    constexpr bool NeedsRaise(uint16_t rank, uint16_t maxRank, uint16_t target)
    {
        return rank < target || maxRank < target;
    }
}

#endif // ClassicBotSkills_h__
