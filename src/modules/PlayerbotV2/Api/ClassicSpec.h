// ClassicSpec - effective specialization id for Classic 1.60 (Forever).
//
// The Forever client has ONE ChrSpecialization per class and stores its
// vanilla talent trees as trait configs, so Player::GetPrimarySpecialization()
// says nothing about tank / healer / dps. PlayerbotV2 keys roles and
// rotations on the retail spec ids (71 Arms, 73 Protection, 257 Holy...).
//
// EffectiveSpecId() returns the real spec when it is one of the retail
// specs of the player's class; otherwise a "virtual" retail spec id:
//   1. an owner override (setspec / setrole), else
//   2. inferred from vanilla talent spells the player knows
//      (Shadowform -> Shadow, Shield Slam -> Protection, ...), else
//   3. a per-class default (Priest -> Holy, everything else a DPS spec).
// Use it everywhere the module needs "the bot's spec" for role/rotation
// decisions. Activating a real spec or talent build must keep using
// GetPrimarySpecialization().

#pragma once

#include "Define.h"
#include "ObjectGuid.h"
#include <string>

class Player;

namespace Playerbot::Classic {

// True when spec is one of the retail specializations of class cls.
TC_GAME_API bool IsRetailClassSpec(uint8 cls, uint32 spec);

TC_GAME_API uint32 EffectiveSpecId(Player const* player);

// Inference only (ignores real spec and overrides). 0 for unknown classes.
TC_GAME_API uint32 InferClassicSpec(Player const* player);

// Owner override. Returns false (and stores nothing) when spec does not
// belong to the player's class or the player runs a real retail spec.
// spec 0 clears the override.
TC_GAME_API bool SetSpecOverride(Player const* player, uint32 spec);
TC_GAME_API uint32 GetSpecOverride(ObjectGuid guid);

// True for classes with a single ChrSpecialization (the Classic 1.60 data):
// talents live in one trait config and the spec id says nothing.
TC_GAME_API bool IsSingleSpecClass(uint8 cls);

// A talent of the player's class trees, found by its (1.12) name.
struct TalentNode
{
    int32 TraitNodeID = 0;
    int32 TraitNodeEntryID = 0;
    int32 MaxRanks = 0;
};
TC_GAME_API TalentNode const* FindTalent(Player const* player, std::string const& name);

} // namespace Playerbot::Classic
