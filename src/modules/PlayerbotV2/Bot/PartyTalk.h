// PartyTalk - what a party / raid / say line means to the bots that hear it: thanks, grats, a laugh, a pull or wait cue, ready,
// hello, the bot's own name. Pure text logic (no game objects), used by BotChatReactor and tested in
// tests/game/ClassicPartyTalk.cpp.
//
// English and Russian words are both understood: a Russian player's "жди" pauses the group like "wait" does, and "спс" gets a
// "не за что" back (the reply pools are English and are translated for each reader where the line leaves the server).

#pragma once

#include "TradeTalk.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace Playerbot::V2::PartyTalk {

// Lowercase + collapse whitespace to a single space. Strips leading/trailing
// whitespace and ASCII punctuation that would otherwise wreck keyword
// matching ("ty!" → "ty", "thx," → "thx"). Russian letters are lowered too
// (and ё read as е). Returns at most 256 bytes; longer messages are
// truncated for matching purposes only (the bot's reply doesn't depend on
// input length).
inline std::string Normalize(std::string const& in)
{
    std::string out;
    out.reserve(std::min<size_t>(in.size(), 256));
    bool last_was_space = true;
    for (char c : in)
    {
        if (out.size() >= 256) break;
        unsigned char uc = static_cast<unsigned char>(c);
        // Treat punctuation-like ASCII as whitespace so "ty!" / "thx," normalize cleanly.
        if (uc == '!' || uc == '?' || uc == '.' || uc == ',' ||
            uc == ';' || uc == ':' || uc == '(' || uc == ')' ||
            uc == '\t' || uc == '\r' || uc == '\n' || uc == ' ')
        {
            if (!last_was_space) { out.push_back(' '); last_was_space = true; }
            continue;
        }
        out.push_back(uc < 0x80 ? char(std::tolower(uc)) : char(uc));
        last_was_space = false;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return TradeTalk::FoldRussian(out);
}

// Returns true if normalized `hay` contains `needle` as a whole word.
// "ty" matches "ty", "ty everyone", " ty all" — not "pretty" or "country".
inline bool ContainsWord(std::string const& hay, char const* needle)
{
    const std::string n = needle;
    size_t pos = 0;
    while ((pos = hay.find(n, pos)) != std::string::npos)
    {
        const bool left_ok  = (pos == 0) || hay[pos - 1] == ' ';
        const bool right_ok = (pos + n.size() == hay.size()) || hay[pos + n.size()] == ' ';
        if (left_ok && right_ok) return true;
        pos += n.size();
    }
    return false;
}

inline bool ContainsAnyWord(std::string const& hay, std::initializer_list<char const*> needles)
{
    for (char const* n : needles)
        if (ContainsWord(hay, n))
            return true;
    return false;
}

// Letters in a UTF-8 line (a Russian letter is two bytes): a length rule like "a short question" must not halve for Russian.
inline size_t Letters(std::string const& s)
{
    size_t n = 0;
    for (char c : s)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
            ++n;
    return n;
}

enum class ReactKind : uint8_t
{
    None,
    Thanks,        // ty / thx / thanks
    Grats,         // gz / grats / congratulations
    Laugh,         // lol / haha / lmao / xd
    Acknowledge,   // pull / go / wait / hold / inc / back / brb — behavioral cues
    Ready,         // ready / rdy — separate so reply can echo "rdy!" not just "k"
    Greeting,      // hi / hello / hey — small social opener
    DirectAddress, // bot's name appears as a whole word
    LocationQuery, // "where" / "wo bist du" — bot whispers back its current zone
};

// True when the message carries a pause cue ("wait"/"hold"/"stop"/"back"/
// "regroup") — real-player shorthand that means "everyone freeze, don't
// pull yet". back/regroup pause a bit longer because the group is
// typically reforming after a wipe or repositioning.
// Distinct from the broader Acknowledge classification because we only
// want to inject the BotAI pause for these specific tokens, not for
// "pull"/"go" (which mean the opposite) or "afk"/"brb" (which mean
// "the SPEAKER is gone", not "the group should hold").
//
// A bare "back" is not one: after "brb" it means "I'm back", and a group that
// froze for 12 s with "ok holding" on it read as bots. "fall back" / "back up"
// / "get back" / "move back" are the orders.
inline bool IsPauseCue(std::string const& norm)
{
    return ContainsAnyWord(norm, { "wait", "hold", "stop", "regroup", "rg", "fall back", "back up", "get back", "move back",
                                   "жди", "ждите", "подожди", "подождите", "стоп", "стой", "стойте", "назад" });
}

inline size_t Words(std::string const& norm)
{
    if (norm.empty())
        return 0;
    size_t n = 1;
    for (char c : norm)
        if (c == ' ')
            ++n;
    return n;
}

// "go" as an order: "go", "ok go", "lets go", "go go", "go next", "we go", a line that starts with "go". Not "i have to go" /
// "gotta go" (the player is leaving) or "where do we go next" (a question): before, both cleared the hold and handed the lead
// back to the dungeon autopilot. Russian "го" / "погнали" / "вперед" are orders wherever they stand.
inline bool SaysGo(std::string const& norm)
{
    if (ContainsAnyWord(norm, { "го", "погнали", "вперед" }))
        return true;
    if (!ContainsWord(norm, "go"))
        return false;
    if (ContainsAnyWord(norm, { "to go", "gotta go", "must go", "gonna go", "where", "how", "why", "when", "what", "dont go", "don't go" }))
        return false;
    return Words(norm) <= 3 || norm.rfind("go ", 0) == 0 || ContainsAnyWord(norm, { "lets go", "let's go", "go go", "ok go" });
}

// "back" / "im back" / "re" after a "brb": the group says "wb".
inline bool IsBackCue(std::string const& norm)
{
    return norm == "back" || norm == "im back" || norm == "i'm back" || norm == "i am back" || norm == "re" || norm == "back now"
        || norm == "back again" || norm == "ok back"
        || norm == "я тут" || norm == "вернулся" || norm == "вернулась" || norm == "я вернулся" || norm == "я вернулась";
}

// Drink cue — healer or caster announces a mana break. Triggers a
// LONGER pause (30s) than the standard wait cue because a real drink
// from full empty takes ~25s and you want the tank to wait through
// the whole regen, not just the first sip. "маны" is the word of "нет маны" / "мало маны" (out of / low on mana); a bare
// "мана" is left out, since "мана есть" means the opposite.
inline bool IsDrinkCue(std::string const& norm)
{
    return ContainsAnyWord(norm, { "drink", "drinking", "mb", "oom",
                                   "пью", "попью", "маны" });
}

// "go" / "pull" / "pulling" — the inverse cue: clear any pause so a
// previous "wait" doesn't keep the group frozen indefinitely.
inline bool IsGoCue(std::string const& norm)
{
    return SaysGo(norm) || ContainsAnyWord(norm, { "pull", "pulling", "engage", "пул", "пулю", "пулим" });
}

// "inc" / "incoming" — an alert: a short pause, not a hold.
inline bool IsIncCue(std::string const& norm)
{
    return ContainsAnyWord(norm, { "inc", "incoming", "инк" });
}

inline ReactKind ClassifyMessage(std::string const& norm, std::string const& bot_first_name_lower)
{
    const bool addressed =
        !bot_first_name_lower.empty() && bot_first_name_lower.size() >= 3 &&
        ContainsWord(norm, bot_first_name_lower.c_str());

    // Includes a handful of non-English thanks tokens — EU players mix
    // DE/FR/ES/RU freely in /p chat.
    const bool has_thanks = ContainsAnyWord(norm, {
        "ty", "thx", "thanks", "thank", "tyvm", "danke", "merci", "gracias",
        "спасибо", "спс", "пасиб", "пасибо", "спасиб", "благодарю", "сенкс" });
    // "Ding!" / "Ding 47" — bots and players announce a level-up
    // this way. Treat it as a Grats trigger so the rest of the group
    // congratulates on the ding. "Hark!" prefix from Roleplay-verbose
    // bot dings is also recognised. Without this, dings dropped into
    // dead air — humans congratulate every party-ding. A Russian player
    // says "ап" ("ап 20") for the same thing.
    const bool has_grats = ContainsAnyWord(norm, {
        "gz", "grats", "gratz", "congrats", "congratulations", "ding", "hark",
        "грац", "гц", "поздравляю", "поздравляем", "ап", "динь" });
    const bool has_laugh = ContainsAnyWord(norm, {
        "lol", "haha", "lmao", "xd", "rofl",
        "лол", "хаха", "хахаха", "ахах", "ахаха", "ахахах", "хд", "ржу", "ору" });
    // "gg" / "wp" / "good game" — universal after-kill or end-of-BG line.
    // Folded into Grats reply pool ("gz!"/"nice"/"well done") since the
    // sentiment is the same (acknowledging a finish).
    const bool has_gg = ContainsAnyWord(norm, { "gg", "wp", "wellplayed", "гг", "вп" });
    // Behavioral cues: words real players type to coordinate the pull/pace
    // of the group. Bots don't execute these (the ;-prefix command path
    // does), but a one-line acknowledgement reads like a focused human
    // teammate instead of a silent NPC. "inc"/"incoming" is acknowledged
    // even though no defensive action follows — silence on "inc" feels
    // worse than a "k" while the bot keeps doing its job.
    const bool has_ack =
        IsPauseCue(norm) || IsGoCue(norm) || IsIncCue(norm) || IsBackCue(norm) ||
        ContainsAnyWord(norm, { "afk", "brb", "афк" });
    const bool has_ready = ContainsAnyWord(norm, { "ready", "rdy", "гтв", "готов", "готова", "готовы" });
    // Greeting tokens — the kind of thing said when a player joins
    // /p or just opens the conversation. EU mix again (German/French/
    // Spanish/Russian hello variants). "yo" / "hey" / "hi" / "sup" are also
    // common in younger-skewed groups.
    const bool has_greeting = ContainsAnyWord(norm, {
        "hi", "hello", "hey", "yo", "sup", "hallo", "bonjour", "hola", "moin",
        "привет", "прив", "приветик", "хай", "ку", "здарова", "здравствуйте", "салют", "хеллоу" });
    // Location query — "where are you" / "wo bist du" / "где ты".
    // Triggers a zone-aware reply ("im in {zone}") instead of the generic
    // "?" direct-address response, on the whisper and the party path. It is
    // only classified as a LocationQuery when the bot is addressed (a bare
    // party "where do we go next?" must not trip a zone dump from every bot).
    const bool has_location_query = ContainsAnyWord(norm, { "where", "wherer", "wo", "где" });

    // When a social keyword AND the bot's name are both present, the social
    // keyword wins ("gz Aron" → Aron replies "ty!", not "yes?"). When only
    // the name appears, the address reply ("?", "yes?") fires.
    if (addressed)
    {
        if (has_thanks)         return ReactKind::Thanks;
        if (has_grats || has_gg) return ReactKind::Grats;
        if (has_ready)          return ReactKind::Ready;
        if (has_laugh)          return ReactKind::Laugh;
        if (has_ack)            return ReactKind::Acknowledge;
        if (has_location_query) return ReactKind::LocationQuery;
        if (has_greeting)       return ReactKind::Greeting;
        return ReactKind::DirectAddress;
    }

    if (has_thanks)         return ReactKind::Thanks;
    if (has_grats || has_gg) return ReactKind::Grats;
    if (has_ready)          return ReactKind::Ready;
    if (has_laugh)          return ReactKind::Laugh;
    if (has_ack)            return ReactKind::Acknowledge;
    if (has_greeting)       return ReactKind::Greeting;

    return ReactKind::None;
}

} // namespace Playerbot::V2::PartyTalk
