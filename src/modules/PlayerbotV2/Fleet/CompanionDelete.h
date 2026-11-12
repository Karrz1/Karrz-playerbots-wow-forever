// CompanionDelete.h: what `.bot delete` (and the add-on's Delete target) may remove. Only the bots made for a player, which live
// on the bot pool accounts. A character on any other account is a player's own character (a `.playerbot summon` alt, or one
// marked by hand): `.bot delete` matched it as an owned bot and deleted it for good (Discord guide check, 2026-10-25), so it is
// refused and the player is pointed to `.bot dismiss`. Pure; tested in tests/game/ClassicCompanionDelete.cpp.
#pragma once

#include <string>

namespace Playerbot::V2::Fleet
{

// `on_pool_account`: the character's account is one of the bot pool accounts (BotAccountMgr::is_pool_account). An account that
// cannot be resolved (0) is not a pool account, so it is refused too.
inline bool MayDeleteCompanion(bool on_pool_account)
{
    return on_pool_account;
}

inline std::string CompanionDeleteRefusal(std::string const& name)
{
    return name + " is one of your own characters, so it is not deleted. `.bot dismiss " + name + "` sends it home.";
}

} // namespace Playerbot::V2::Fleet
