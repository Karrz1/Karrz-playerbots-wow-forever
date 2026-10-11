// TradeRule - who a bot lets trade with it. One rule: a real player in the bot's own group or raid, or its owner, is
// accepted (the bot takes what he gives and offers nothing); everyone else is declined with "busy".
// Pure: no game types, tested in tests/game/ClassicTradeRule.cpp.

#pragma once

namespace Playerbot {

// accept_enabled: Playerbot.Behavior.AcceptOwnerTrade (on by default; the owner's gift trade already used it).
constexpr bool AcceptsTradeFrom(bool accept_enabled, bool partner_is_real, bool partner_is_owner, bool partner_in_my_group)
{
    return accept_enabled && partner_is_real && (partner_is_owner || partner_in_my_group);
}

} // namespace Playerbot
