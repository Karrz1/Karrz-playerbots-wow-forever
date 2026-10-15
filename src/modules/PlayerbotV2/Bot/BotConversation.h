// BotConversation - model-free conversation for bots. Classifies what a player typed (how are you, where are you from, what
// level, quest help, directions, ...) and answers from the bot's real state with several phrasings per intent, varied by the
// bot's politeness / verbosity and a stable per-bot writing style. Pure logic: no Player / BotAI access, so it is tested
// offline (tests/game/ClassicBotConversation.cpp). BotChatReactor fills a ConversationContext from the live bot.
//
// Commands (whyidle, follow, ...) never reach this: the command parser and the ;-prefix path run first.
//
// An optional ReplyProvider can answer lines the rule set does not understand (Intent::Unknown) - for example an external
// chat service. None ships here; with no provider registered Unknown simply returns false and the caller's
// existing fallback (ike3 text pools) applies.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Playerbot::V2 {

enum class ConvTone : uint8_t { Rude, Neutral, Polite };
enum class ConvLength : uint8_t { Terse, Normal, Chatty };

enum class ConvIntent : uint8_t
{
    Unknown,
    HowAreYou,
    WhereFrom,       // "where are you from"
    WhereAreYou,     // "where are you" - current zone
    Level,
    ClassSpec,
    WhatDoing,
    GroupInvite,
    QuestHelp,
    Directions,      // "how do I get to Ironforge"
    Trade,           // "can you craft / do you have a profession / want to trade"
    Guild,
    Name,
    Joke,
    Insult,
    Compliment,
    Goodbye,
    Greeting,        // only answered here for someone the bot knows (see ConversationContext::knows_sender)
};

struct ConversationContext
{
    std::string self_name;
    std::string sender_name;
    std::string class_name;                // "mage"; empty if unknown
    std::string zone;                      // current area name; empty if unknown
    std::string activity;                  // "out questing", "running with a group", ... ; empty -> "just hanging around"
    std::string quest_title;               // a quest in the log, empty if none
    std::string guild_name;                // empty if unguilded
    std::vector<std::string> professions;  // "mining", "tailoring"
    uint32_t level = 1;
    bool in_group = false;
    bool knows_sender = false;             // the sender is in the bot's group (its owner / party): greet them as a known person
    uint32_t bot_seed = 0;                 // stable per bot: drives its writing style
    ConvTone tone = ConvTone::Neutral;
    ConvLength length = ConvLength::Normal;
};

// Answers a line the rules did not understand. Called on the world thread; an implementation must not block - return false when
// no answer is ready (a network-backed one queues the request and answers a later line, or returns false).
class ReplyProvider
{
public:
    virtual ~ReplyProvider() = default;
    virtual bool TryReply(ConversationContext const& ctx, std::string const& message, std::string& out) = 0;
};

// Lower-cases, folds punctuation to spaces, collapses whitespace (max 256 chars). BotChatReactor's own Normalize is equivalent.
std::string ConvNormalize(std::string const& in);

// Classifies a normalized line. `slot` receives the destination keyword for Directions (lower-case), else empty.
ConvIntent ConvClassify(std::string const& norm, std::string* slot = nullptr);

// True when the rules understand the line (so a whisper that matched nothing else still deserves an answer).
bool ConvUnderstands(std::string const& norm);

// Builds the reply for `norm` (the normalized line); `message` is the raw line handed to a provider. `sel` varies the phrasing.
// Returns false when there is nothing to say. `addressed` gates the provider (it only sees lines meant for this bot).
bool ConvReply(ConversationContext const& ctx, std::string const& norm, std::string const& message, uint32_t sel, bool addressed,
               std::string& out);

// Short name of an intent for log lines ("how_are_you", "directions", "unknown").
char const* ConvIntentName(ConvIntent intent);

// Reply to the same line sent again by the same player (`repeat` = how many times it was already answered, 1 = second time), so a
// repeated "hi" gets "you already said hi" instead of silence or the identical answer.
std::string ConvRepeatReply(ConversationContext const& ctx, uint32_t sel, uint32_t repeat);

// At most one provider; nullptr clears it. Thread-safe.
void ConvSetProvider(std::shared_ptr<ReplyProvider> provider);

} // namespace Playerbot::V2
