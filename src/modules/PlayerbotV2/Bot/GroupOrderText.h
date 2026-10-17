// Recognition of the bare group orders a player gives to the bots of their group ("follow", "pull back",
// "formation circle", ...), shared by party/raid chat and by the ForeverBots addon's addon-message transport.
// Pure text: no Player, no Group, no config singleton (the owner-word switch is passed in).
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Playerbot::V2 {

// Addon-message prefix the ForeverBots panel sends its orders under (C_ChatInfo.RegisterAddonMessagePrefix, <= 16 chars).
inline constexpr std::string_view kAddonOrderPrefix = "FBOTS";

// The normalized order for a party/raid line ("assist" becomes "attack", "pullback" becomes "pull back", case and outer
// blanks removed), or nullopt when the line is ordinary chatter (or starts with the ';' squad prefix, which has its own
// path). owner_words enables the plain-language owner commands (Playerbot.Behavior.OwnerCommands).
std::optional<std::string> ClassifyGroupOrder(std::string_view msg, bool owner_words);

// A party or raid line addressed to one bot or to all of them without the ';' prefix: "Lentha use altar", "Baxter, follow",
// "all use altar". The first word must be the name of a bot in the sender's group (is_group_bot, case-insensitive; a
// trailing ',' or ':' is allowed) or "all" / "everyone"; the rest is the order. Returns the whisper-parser form
// ("Lentha:use altar", "squad:use altar") or nullopt for chatter. Whether the rest is a real order is the parser's call,
// and an unknown one stays silent, so "Lentha nice heals" does nothing.
std::optional<std::string> AddressedGroupOrder(std::string_view msg, std::function<bool(std::string_view)> const& is_group_bot);

// The same for an addon message: only the fixed prefix counts, the text is classified as party chat would be.
std::optional<std::string> ClassifyAddonOrder(std::string_view prefix, std::string_view text, bool owner_words);

}  // namespace Playerbot::V2
