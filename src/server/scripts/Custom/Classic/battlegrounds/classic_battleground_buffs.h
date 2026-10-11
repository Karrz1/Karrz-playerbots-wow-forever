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
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CLASSIC_BATTLEGROUND_BUFFS_H
#define CLASSIC_BATTLEGROUND_BUFFS_H

// Speed / Restoration / Berserking powerups on the classic battleground maps (WSG 489, AB 529).
//
// The buff objects are type 6 (trap) objects with a trigger radius of 0 and the string id "bg_buff_object". Retail triggers
// them from battleground area triggers (areatrigger_scripts.cpp, at_battleground_buffs); the classic maps have none of those
// triggers wired, so nothing ever fired the object: a player standing on the glowing powerup in the Silverwing tunnel could not
// use it, and bots could not either. The scripts poll instead, like the WSG flag carriers (bots send no area trigger packets
// either): a living player within 3 y of a buff object takes it - the same call the area trigger makes - and the object
// despawns and comes back after its spawn time.

#include "Battleground.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Player.h"

namespace ClassicBattlegroundBuffs
{
    static constexpr float TakeRange = 3.0f;

    inline void Poll(Battleground* battleground, Map* map)
    {
        for (auto const& [guid, bgPlayer] : battleground->GetPlayers())
        {
            Player* player = ObjectAccessor::GetPlayer(map, guid);
            if (!player || !player->IsInWorld() || !player->IsAlive() || player->HasAuraType(SPELL_AURA_GHOST) || player->GetVehicle())
                continue;

            if (GameObject* buff = player->FindNearestGameObjectWithOptions(TakeRange, { .StringId = "bg_buff_object" }))
            {
                buff->ActivateObject(GameObjectActions::Disturb, 0, player);
                buff->DespawnOrUnsummon();
            }
        }
    }
}

#endif
