// InvitePolicy - whose group invite a bot takes (Playerbot.Interact.AcceptInvitesFrom).
//
// Only invites from real players are weighed: a bot's invite (a pickup group, a listed bot group, the dungeon finder) is the bot
// system's own and always goes through. The bot's owner and a game master are always answered, whatever the setting, so an owner's
// companions and a GM's tests never stop working. Each step takes the ones before it: guild = friends and guild members.
// A bot that belongs to a player (a companion, an alt played as a bot, .playerbot self) takes a player's invite from its owner
// and a GM only, whatever the setting: with "anyone" a stranger could pull someone else's character into his group, lead it about and
// sell it things through the group trade (BotAI: a group mate's trade is bought at the fair price).
//
// Pure, tested in tests/game/ClassicBotParties.cpp. The rule that answers the invite is idle:group_invite_accept (CoreFlowRules.cpp).
#pragma once

#include <cstdint>
#include <string_view>

namespace Playerbot::InvitePolicy
{

enum class From : std::uint8_t
{
    Anyone,    // default: every player's invite is taken (the bots' behaviour before the key existed)
    Guild,     // friends and members of the bot's guild
    Friends,   // a player on the bot's friend list (whisper it "addfriend <name>")
    None,      // no player's invite, except the owner's and a GM's
};

inline From Parse(std::string_view v)
{
    if (v == "guild")   return From::Guild;
    if (v == "friends") return From::Friends;
    if (v == "none")    return From::None;
    return From::Anyone;   // "anyone", and anything not understood
}

// What the bot knows about the player who invited it.
struct Inviter
{
    bool real = false;        // a player, not a bot
    bool owner = false;       // the bot's owner (its account)
    bool gm = false;          // game master or higher
    bool friend_ = false;     // on the bot's friend list
    bool guildmate = false;   // in the bot's guild
    bool bot_owned = false;   // the bot belongs to a player's account (an owner binding)
};

inline bool Accepts(From policy, Inviter const& i)
{
    if (!i.real || i.owner || i.gm)
        return true;
    if (i.bot_owned)
        return false;
    switch (policy)
    {
        case From::Anyone:  return true;
        case From::Guild:   return i.friend_ || i.guildmate;
        case From::Friends: return i.friend_;
        case From::None:    return false;
    }
    return true;
}

inline char const* Name(From policy)
{
    switch (policy)
    {
        case From::Anyone:  return "anyone";
        case From::Guild:   return "guild";
        case From::Friends: return "friends";
        case From::None:    return "none";
    }
    return "?";
}

} // namespace Playerbot::InvitePolicy
