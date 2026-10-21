// ForeignClassSpell - whether a spell a bot knows belongs to another class (Api/PlayerbotAPI.cpp purge_foreign_class_spells).
#pragma once

#include <cstdint>

namespace Playerbot::Api {

// taught_by: the union of the class masks (1 << (class - 1)) of the trainers that teach the spell, 0 when no class trainer teaches it.
// A spell no class trainer teaches says nothing about its owner and is kept; one that only other classes' trainers teach is foreign.
constexpr bool IsForeignClassSpell(uint32_t taught_by, uint32_t class_mask)
{
    return taught_by != 0 && (taught_by & class_mask) == 0;
}

} // namespace Playerbot::Api
