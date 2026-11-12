// ChannelChat - bots answer what real players write in General, Trade and LookingForGroup (Playerbot.Chatter.ChannelReplies.*).
//
//   LFM ("LF healer DM")     a free world bot of the wanted role and level whispers the player to invite it (one per role, two at most).
//   LFG ("lfg sfk")          a listed bot group with a place whispers him to join; else a free bot of his level offers to come.
//   WTB ("wtb [Linen Cloth]") a bot in the channel's reach that has it whispers its price; "buy 1" and a trade then work as usual.
//   a question               now and then one bot answers in the channel from its own state (BotConversation); a line the rules
//                            do not understand goes to the answer hook, when one is set.
//   a greeting               now and then one bot says hi back.
// The channel's reach decides who may answer: General the bots in the player's zone, Trade the bots in a city of his faction,
// LookingForGroup any bot of his faction. Only world bots answer (no owner, not grouped). The line parsing is Bot/ChannelTalk.h.
//
// The bots' own LFG calls (idle:yell_lfg) go to the LookingForGroup channel and General through LfgCallSlot / Post.
//
// World thread only.
#pragma once

#include "Define.h"
#include "Bot/BotTypes.h"

#include <string>

class Channel;
class Player;

namespace Playerbot::V2::ChannelChat
{

// A real player wrote `msg` in the system channel `ch` (from the chat hook, before the line is sent).
void OnPlayerLine(Player* sender, Channel* ch, std::string const& msg);

// An answer source for channel lines the rules leave unanswered (a chat model, for one). Called on the world thread with the bot
// chosen to answer; return true when it takes the line (it then answers later through Post), false to let the line go. Must not
// block. nullptr clears it.
using AnswerHook = bool (*)(Player* bot, Player* sender, uint32 channel_id, std::string const& msg);
void SetAnswerHook(AnswerHook hook);

// `bot` writes `text` in its channel `channel_id` (1 General, 2 Trade, LfgChannelId()) at `when_ms` (game time, 0 = now).
void Post(BotId bot, uint32 channel_id, std::string text, uint32 when_ms = 0);

// The LookingForGroup channel of the client's ChatChannels table, 0 when it has none.
uint32 LfgChannelId();

// One LFG call per faction per 20 s realm-wide, and never a line that went out recently. True = `line` may be posted now (noted).
bool LfgCallSlot(bool alliance, std::string const& line, uint32 now_ms);

} // namespace Playerbot::V2::ChannelChat
