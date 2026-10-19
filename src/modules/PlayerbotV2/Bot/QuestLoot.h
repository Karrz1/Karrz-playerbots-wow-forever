// A quest drop this bot is allowed to take. A bot a player leads loots nothing else: the player takes the rest, but each member with the
// quest has to loot its own quest items (they appear on the corpse for that member alone).
#pragma once

#include "Loot.h"
#include "Player.h"

namespace Playerbot
{

[[nodiscard]] inline bool IsPendingQuestLoot(Player* p, Loot* loot, uint8 slot)
{
    NotNormalLootItem* ffa = nullptr;
    LootItem* it = loot->LootItemInSlot(slot, p, &ffa);
    return it && it->needs_quest && !it->is_looted && !it->is_blocked && !(ffa && ffa->is_looted) && it->HasAllowedLooter(p->GetGUID());
}

} // namespace Playerbot
