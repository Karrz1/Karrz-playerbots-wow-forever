#include "HunterPetsByRace.h"
#include "RaceMask.h"

namespace Playerbot::V2::World {

uint32 HunterPetEntryForRace(uint32 race)
{
    // Per-race starter pet: a beast of the race's starting zone, checked against our world data (dump of 19 Oct: type 1 beast with a
    // pet family). The old table named non-beasts for most Horde races (5826 Geolord Mottle and 2952 Bristleback Quilboar are quilboars,
    // 1908 Vile Fin Oracle a murloc, 5827 Brontus a kodo) and NPCs for the later races (17091, 35040, 54269, 16920), so those hunters
    // never got a pet: "[hunter_pet] ... entry 5826 is not a tameable beast in this data" on every orc and troll world hunter.
    switch (race)
    {
        // Alliance
        case RACE_HUMAN:                    return 299;    // Young Wolf — Northshire
        case RACE_DWARF:                    return 1126;   // Large Crag Boar — Coldridge Valley
        case RACE_NIGHTELF:                 return 2031;   // Young Nightsaber — Shadowglen
        case RACE_GNOME:                    return 299;    // Gnome hunters are modern-only; default to faction wolf
        // Later and allied Alliance races default to faction-generic
        case RACE_DRAENEI:
        case RACE_WORGEN:
        case RACE_PANDAREN_ALLIANCE:
        case RACE_VOID_ELF:
        case RACE_LIGHTFORGED_DRAENEI:
        case RACE_DARK_IRON_DWARF:
        case RACE_KUL_TIRAN:
        case RACE_MECHAGNOME:
        case RACE_DRACTHYR_ALLIANCE:
        case RACE_EARTHEN_DWARF_ALLIANCE:   return 0;      // fall through to GenericAllianceHunterPet

        // Horde
        case RACE_ORC:                      return 3099;   // Dire Mottled Boar — Valley of Trials
        case RACE_UNDEAD_PLAYER:            return 1508;   // Young Scavenger — Tirisfal Glades wolf
        case RACE_TAUREN:                   return 2956;   // Adult Plainstrider — Mulgore
        case RACE_TROLL:                    return 3122;   // Bloodtalon Taillasher — Echo Isles raptor
        // Later and allied Horde races default to faction-generic
        case RACE_GOBLIN:
        case RACE_BLOODELF:
        case RACE_PANDAREN_HORDE:
        case RACE_NIGHTBORNE:
        case RACE_HIGHMOUNTAIN_TAUREN:
        case RACE_ZANDALARI_TROLL:
        case RACE_VULPERA:
        case RACE_MAGHAR_ORC:
        case RACE_DRACTHYR_HORDE:
        case RACE_EARTHEN_DWARF_HORDE:      return 0;      // fall through to GenericHordeHunterPet

        default:                            return 0;
    }
}

} // namespace Playerbot::V2::World
