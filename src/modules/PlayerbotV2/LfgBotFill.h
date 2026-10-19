// LfgBotFill.h: bots for a Group Finder listing. A player posts a dungeon listing; if the group is not complete
// Playerbot.Lfg.BotFillSeconds later (default 180) the missing tank / healer / damage places are filled with the
// owner's own level-matched companions. Real players always come first: they are counted before any bot is added.
#pragma once

#include "Define.h"
#include "Bot/BotTypes.h"
#include "LfgRoleGap.h"

#include <string>
#include <vector>

class Player;

namespace Playerbot::V2
{

// Module side (LfgBotFill.cpp). All world thread.
void LfgListingChanged(Player* leader, uint32 categoryId, std::vector<uint32> const& activities, bool listed);
void LfgTick(uint32 nowMs);
// `.bot lfg [tank|healer|dps]`: fill now. `ownRole` is the caller's role when they said it (Unknown = from their group role/class).
bool LfgFillNow(Player* leader, Playerbot::Role ownRole, std::string& msg);
// Bot-made groups in the Group Finder list (Playerbot.Lfg.BotGroups.*): every second from the world tick.
void LfgBotGroupsTick(uint32 nowMs);
// A player searched the list: list a bot group for their faction now when there is none (a failed try is not repeated within 5 s).
void LfgBotGroupsOnSearch(Player* searcher);
// A player invited a member of a listed bot group: they join, take the lead and the bots become their companions. False when the
// group is not a listed bot group (or is full), the usual "already in a group" answer then follows.
bool LfgBotGroupJoinRequest(Player* member, Player* applicant);
// True when `member` is in a listed bot group that `applicant` (a player without a group, same faction) can join now: a player's
// whisper "invite" to one of its bots then makes the same join (a bot leaves when the group is full).
bool LfgBotGroupCanTake(Player* member, Player* applicant);
// Creates one companion for the owner (shared with `.bot add`).
bool AddCompanionForOwner(Player* owner, uint8 cls, Playerbot::Role role, std::string& msg);

} // namespace Playerbot::V2
