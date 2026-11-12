#pragma once
// Permanent enchants for a raid-ready bot (Playerbot.Gear.RaidEnchants): every equipped piece the role's loadout (RaidLoadout.h) names gets
// its enchant, as a vanilla raider's did. World thread.

#include "Define.h"

class Player;

namespace Playerbot::Gear {

// Enchants the bot's worn items for its gear level (Loadout::EnchantTier by average equipped item level): an item with no permanent
// enchant gets the best one its tier allows, and one of the loadouts' own enchants of another tier is swapped (or taken off when the
// tier allows none in that slot). A permanent enchant the loadouts do not name is left alone. Returns how many it enchanted. Logs "[enchant]" lines: what went on, and
// the names the spell data did not know (once per name).
// Below the realm level cap it enchants nothing and takes the loadout's enchants off instead (StripRaidEnchants): they are level-60 raid
// enchants, not a levelling bot's.
uint32 ApplyRaidEnchants(Player* p, uint16 spec);

// Takes every enchant the raid loadouts name (any role) off the worn items of a bot below the level cap. Other permanent enchants stay.
// Returns how many it removed. Logs "[enchant] removed" lines.
uint32 StripRaidEnchants(Player* p);

} // namespace Playerbot::Gear
