// AplPetThrottle.h: pace for the one pet_attack order of a bot's combat tick (State_InCombat, all pet classes).
//
// The order goes out whenever the pet's victim is not the bot's victim. A pet that cannot or will not take it (it cannot reach the
// mob, is out of range, a boss immune to it, a stale snapshot) kept that true on every tick (live MC Gehennas 2026-10-08, two
// warlocks stuck up to 83 s). A new victim is ordered at once; the same victim again only after kPetAttackGapMs.
#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "ObjectGuid.h"

namespace Playerbot::AplPet {

constexpr uint32_t kPetAttackGapMs = 3000;

struct LastOrder { ObjectGuid target; uint32_t ms = 0; };

inline std::mutex& Mutex() { static std::mutex m; return m; }
inline std::unordered_map<uint64_t, LastOrder>& LastOrders() { static std::unordered_map<uint64_t, LastOrder> m; return m; }

// True when this bot has not ordered its pet onto `target` in the last kPetAttackGapMs.
inline bool PetAttackDue(uint64_t guid, ObjectGuid target, uint32_t now_ms)
{
    std::lock_guard<std::mutex> lk(Mutex());
    auto const it = LastOrders().find(guid);
    return it == LastOrders().end() || it->second.target != target || uint32_t(now_ms - it->second.ms) >= kPetAttackGapMs;
}
inline void NotePetAttack(uint64_t guid, ObjectGuid target, uint32_t now_ms)
{
    std::lock_guard<std::mutex> lk(Mutex());
    LastOrders()[guid] = LastOrder{target, now_ms};
}

} // namespace Playerbot::AplPet
