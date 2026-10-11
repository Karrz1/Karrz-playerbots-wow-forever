// AplPetThrottle.h: pace for the pet_attack rotation rule of the pet classes (warlock, hunter).
//
// pet_attack sits ahead of every damage rule and fires whenever the pet's victim is not the bot's victim. A pet that cannot or will
// not take the order (it cannot reach the mob, is out of range, a boss immune to it, a stale snapshot) kept the predicate true on
// every tick, so the first-match rotation never got past it: live MC Gehennas 2026-10-08, two warlocks stuck up to 83 s on
// "classic_warlock:pet_attack / no rule acted" at 26-32 y with 96-99% mana, no spell cast. The order is given at most once per
// kPetAttackGapMs; in between the rule steps aside and the nukes run.
#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace Playerbot::AplPet {

constexpr uint32_t kPetAttackGapMs = 3000;

inline std::mutex& Mutex() { static std::mutex m; return m; }
inline std::unordered_map<uint64_t, uint32_t>& LastOrderMs() { static std::unordered_map<uint64_t, uint32_t> m; return m; }

// True when no pet_attack order went out for this bot in the last kPetAttackGapMs.
inline bool PetAttackDue(uint64_t guid, uint32_t now_ms)
{
    std::lock_guard<std::mutex> lk(Mutex());
    auto const it = LastOrderMs().find(guid);
    return it == LastOrderMs().end() || uint32_t(now_ms - it->second) >= kPetAttackGapMs;
}
inline void NotePetAttack(uint64_t guid, uint32_t now_ms)
{
    std::lock_guard<std::mutex> lk(Mutex());
    LastOrderMs()[guid] = now_ms;
}

} // namespace Playerbot::AplPet
