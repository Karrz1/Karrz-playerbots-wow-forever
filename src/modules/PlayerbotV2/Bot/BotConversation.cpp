// BotConversation - see BotConversation.h.

#include "BotConversation.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <mutex>

namespace Playerbot::V2 {

namespace {

// ---- text helpers -------------------------------------------------------------------------------------------------------

bool HasWord(std::string const& hay, char const* needle)
{
    std::string const n = needle;
    size_t pos = 0;
    while ((pos = hay.find(n, pos)) != std::string::npos)
    {
        bool const left = pos == 0 || hay[pos - 1] == ' ';
        bool const right = pos + n.size() == hay.size() || hay[pos + n.size()] == ' ';
        if (left && right)
            return true;
        pos += n.size();
    }
    return false;
}

bool HasAny(std::string const& hay, std::initializer_list<char const*> words)
{
    for (char const* w : words)
        if (HasWord(hay, w))
            return true;
    return false;
}

std::string Lower(std::string s)
{
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void Replace(std::string& s, std::string const& key, std::string const& val)
{
    size_t pos = 0;
    while ((pos = s.find(key, pos)) != std::string::npos)
    {
        s.replace(pos, key.size(), val);
        pos += val.size();
    }
}

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 2654435761u ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
}

// ---- destinations -------------------------------------------------------------------------------------------------------

struct Destination
{
    char const* keys[3];                   // nullptr-terminated when fewer than 3
    char const* name;
    char const* route;
};

// Vanilla geography only; nothing here that is not true of the Classic world.
Destination const kDestinations[] = {
    { { "stormwind", "sw" }, "Stormwind", "follow the road out of Elwynn Forest, or take the Deeprun Tram from Ironforge" },
    { { "ironforge" }, "Ironforge", "it is in Dun Morogh, or take the Deeprun Tram from Stormwind" },
    { { "darnassus" }, "Darnassus", "it is up in Teldrassil, and a boat from Auberdine in Darkshore gets you to Rutheran Village" },
    { { "orgrimmar", "org" }, "Orgrimmar", "it is in Durotar, and the zeppelin from Tirisfal Glades brings you right to it" },
    { { "thunder bluff", "tb" }, "Thunder Bluff", "it is up on the mesas of Mulgore, take the lift up" },
    { { "undercity", "uc" }, "Undercity", "the entrance is in Tirisfal Glades, by the ruins of Lordaeron" },
    { { "elwynn forest", "elwynn" }, "Elwynn Forest", "it is the zone around Stormwind, head out of the main gate" },
    { { "westfall" }, "Westfall", "it is just west of Elwynn Forest" },
    { { "duskwood" }, "Duskwood", "it is south of Elwynn Forest" },
    { { "redridge mountains", "redridge" }, "Redridge Mountains", "it is east of Elwynn Forest" },
    { { "dun morogh" }, "Dun Morogh", "it is the snowy zone around Ironforge" },
    { { "loch modan" }, "Loch Modan", "it is east of Dun Morogh" },
    { { "teldrassil" }, "Teldrassil", "take the boat from Auberdine in Darkshore to Rutheran Village" },
    { { "darkshore" }, "Darkshore", "it is the coast south of Teldrassil, reach it by boat from Rutheran Village" },
    { { "durotar" }, "Durotar", "it is the orc starting zone, Orgrimmar is there, just east of the Barrens" },
    { { "mulgore" }, "Mulgore", "it is south-west of the Barrens, Thunder Bluff is there" },
    { { "the barrens", "barrens", "crossroads" }, "The Barrens", "it is between Durotar and Mulgore, the Crossroads is the main town" },
    { { "tirisfal glades", "tirisfal" }, "Tirisfal Glades", "it is the undead starting zone, Undercity is below it" },
};

Destination const* FindDestination(std::string const& norm)
{
    for (Destination const& d : kDestinations)
        for (char const* k : d.keys)
            if (k && HasWord(norm, k))
                return &d;
    return nullptr;
}

// ---- classification -----------------------------------------------------------------------------------------------------

bool IsYouWord(std::string const& norm)
{
    return HasAny(norm, { "you", "u", "ya", "yourself", "your", "ur" });
}

} // namespace

std::string ConvNormalize(std::string const& in)
{
    std::string out;
    out.reserve(std::min<size_t>(in.size(), 256));
    bool last_space = true;
    for (char c : in)
    {
        if (out.size() >= 256)
            break;
        unsigned char const uc = static_cast<unsigned char>(c);
        if (uc == '\'')
            continue;                      // "where's" -> "wheres", "i'm" -> "im"
        if (uc == '!' || uc == '?' || uc == '.' || uc == ',' || uc == ';' || uc == ':' || uc == '(' || uc == ')' ||
            uc == '"' || uc == '\t' || uc == '\r' || uc == '\n' || uc == ' ')
        {
            if (!last_space)
            {
                out.push_back(' ');
                last_space = true;
            }
            continue;
        }
        out.push_back(char(std::tolower(uc)));
        last_space = false;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

ConvIntent ConvClassify(std::string const& norm, std::string* slot)
{
    if (slot)
        slot->clear();
    if (norm.empty())
        return ConvIntent::Unknown;

    bool const you = IsYouWord(norm);

    // Insults first: they should not be read as a question that happens to contain "you".
    if (HasAny(norm, { "stupid", "idiot", "noob", "suck", "sucks", "trash", "useless", "dumb", "moron", "ugly", "stfu" }) ||
        HasWord(norm, "shut up") || HasWord(norm, "fuck you") || HasWord(norm, "f you"))
        return ConvIntent::Insult;

    if (HasAny(norm, { "bye", "cya", "goodbye", "gn", "goodnight", "gtg", "g2g", "later", "laters", "farewell", "ttyl", "see you", "see ya" }) ||
        HasWord(norm, "good night") || HasWord(norm, "got to go") || HasWord(norm, "have to go"))
        return ConvIntent::Goodbye;

    if (HasWord(norm, "joke") || HasWord(norm, "funny") || HasWord(norm, "make me laugh"))
        return ConvIntent::Joke;

    // Directions: "how do i get to X", "where is X", "way to X", "path to X", "how to get to X".
    bool const asks_way = (HasWord(norm, "how") && (HasWord(norm, "get") || HasWord(norm, "go") || HasWord(norm, "reach"))) ||
                          HasWord(norm, "directions") || HasWord(norm, "way to") || HasWord(norm, "path to") ||
                          HasWord(norm, "route") || (HasWord(norm, "where") && (HasWord(norm, "is") || HasWord(norm, "s") || HasWord(norm, "are")) && !you) ||
                          HasWord(norm, "wheres");
    if (asks_way)
    {
        if (Destination const* d = FindDestination(norm))
        {
            if (slot)
                *slot = Lower(d->name);
            return ConvIntent::Directions;
        }
        // "where is the auction house / flight master": no destination we know; Unknown lets a provider try.
    }

    if (HasWord(norm, "quest") || HasWord(norm, "quests") || HasWord(norm, "q"))
    {
        if (HasAny(norm, { "help", "stuck", "find", "where", "how", "do", "need", "doing", "on", "got", "have", "any" }))
            return ConvIntent::QuestHelp;
    }

    if (HasAny(norm, { "guild", "guilds" }))
        return ConvIntent::Guild;

    if (HasAny(norm, { "profession", "professions", "craft", "crafting", "crafter", "enchant", "enchanter", "mats", "recipe", "recipes", "mining",
                       "herbalism", "skinning", "tailoring", "alchemy", "blacksmithing", "engineering", "leatherworking", "cooking", "fishing" }) ||
        HasWord(norm, "make me") || HasWord(norm, "can you make"))
        return ConvIntent::Trade;

    if (HasWord(norm, "where") && HasAny(norm, { "from", "come from", "born" }))
        return ConvIntent::WhereFrom;

    if ((HasAny(norm, { "name", "called" }) && !HasAny(norm, { "quest", "quests" }) && (you || HasWord(norm, "whats") || HasWord(norm, "what"))) || HasWord(norm, "who are you") ||
        HasWord(norm, "who r u") || HasWord(norm, "who is this"))
        return ConvIntent::Name;

    if (HasWord(norm, "where") && you)
        return ConvIntent::WhereAreYou;
    if (HasWord(norm, "wherer") || HasWord(norm, "wo bist du"))
        return ConvIntent::WhereAreYou;

    if (HasAny(norm, { "level", "lvl", "lv" }) && !HasWord(norm, "how to level"))
        return ConvIntent::Level;

    if ((HasWord(norm, "class") || HasWord(norm, "spec") || HasWord(norm, "talents")) && (you || HasWord(norm, "what") || HasWord(norm, "which")))
        return ConvIntent::ClassSpec;

    if ((HasWord(norm, "how") || HasWord(norm, "hows")) && ((HasAny(norm, { "are", "r", "re" }) && you) || HasAny(norm, { "going", "doing", "things", "been", "life" })))
        return ConvIntent::HowAreYou;
    if (HasAny(norm, { "hbu", "wbu", "whats up", "wassup", "wazzup" }) && !HasWord(norm, "doing"))
        return ConvIntent::HowAreYou;

    if ((HasWord(norm, "what") && (HasWord(norm, "doing") || HasWord(norm, "up to"))) || HasWord(norm, "wyd") ||
        (HasWord(norm, "sup") && HasWord(norm, "doing")) || HasWord(norm, "busy"))
        return ConvIntent::WhatDoing;

    if (HasAny(norm, { "group", "party", "team", "invite", "lfg", "lfm", "dungeon", "dungeons", "run", "join" }))
    {
        if (HasAny(norm, { "want", "wanna", "can", "join", "invite", "lfg", "lfm", "up", "need", "come", "wana", "would", "will" }))
            return ConvIntent::GroupInvite;
    }

    if (HasAny(norm, { "awesome", "amazing", "nice", "cool", "great", "love", "best", "rock", "rocks", "legend" }) &&
        (you || HasAny(norm, { "job", "bot", "player" })))
        return ConvIntent::Compliment;

    // A short line that opens with a greeting ("hi", "hey Aron", "hello there"); a longer line is something else.
    if (std::count(norm.begin(), norm.end(), ' ') < 3 &&
        HasAny(norm, { "hi", "hey", "hello", "heya", "hiya", "howdy", "yo", "sup", "hallo", "moin", "hola", "bonjour", "morning" }))
        return ConvIntent::Greeting;

    return ConvIntent::Unknown;
}

bool ConvUnderstands(std::string const& norm)
{
    return ConvClassify(norm) != ConvIntent::Unknown;
}

namespace {

// ---- phrasing -----------------------------------------------------------------------------------------------------------

struct Pools
{
    std::vector<std::string> rude;
    std::vector<std::string> neutral;
    std::vector<std::string> polite;
};

// Each pool is ordered short to long: Terse bots draw from the first half only.
Pools const& PoolsFor(ConvIntent intent, bool variant)
{
    // variant selects the "no data" form where one exists (no quest, no guild, no profession).
    static Pools const how = {
        { "fine", "ok", "alive", "meh" },
        { "good", "doing good", "not bad", "pretty good, you?", "all good, {doing}", "good thanks, {doing}" },
        { "doing great, thanks for asking!", "pretty good, how about you?", "all good here, {doing}. you?", "really good, thanks :)" } };
    static Pools const from = {
        { "around", "{zone}", "dunno, {zone} atm" },
        { "{zone} atm", "around here, {zone} now", "been all over, {zone} at the moment" },
        { "just a {cls} from around here, i'm in {zone} right now", "all over really, {zone} at the moment, you?", "i travel a lot, currently in {zone} :)" } };
    static Pools const where = {
        { "{zone}", "{zone} atm", "in {zone}" },
        { "im in {zone}", "{zone} atm", "over in {zone}", "currently in {zone}" },
        { "i'm over in {zone} :)", "currently in {zone}, come find me!", "you'll find me in {zone}" } };
    static Pools const level = {
        { "{lvl}", "{lvl} {cls}", "level {lvl}" },
        { "level {lvl} {cls}", "im {lvl} {cls}", "{lvl} {cls}, you?", "lvl {lvl}" },
        { "i'm level {lvl}, a {cls} :)", "level {lvl} {cls}, how about you?", "{lvl} {cls}, still levelling" } };
    static Pools const cls = {
        { "{cls}", "a {cls}", "{cls}, why?" },
        { "im a {cls}", "{cls}, level {lvl}", "{lvl} {cls}", "playing a {cls}" },
        { "i'm a {cls} :) level {lvl}", "a {cls}, they're a lot of fun!", "{cls}, and loving it" } };
    static Pools const doing = {
        { "{doing}", "nothing much", "{doing}, why" },
        { "{doing}", "not much, {doing}", "{doing} in {zone}", "{doing}, you?" },
        { "{doing} in {zone}, and you?", "just {doing}, enjoying it :)", "{doing}, how about yourself?" } };
    static Pools const group_free = {
        { "ok invite", "sure", "invite" },
        { "sure, send me an invite", "sure! invite me", "sounds good, invite me", "yeah why not, invite" },
        { "sure, send me an invite :)", "sounds fun, invite me and i'll come", "happy to, just send the invite" } };
    static Pools const group_busy = {
        { "in a group", "busy", "cant" },
        { "already in a group, sorry", "im with a party right now", "in a group atm" },
        { "already in a group, sorry! maybe later :)", "i'm with a party right now, ask me again soon?" } };
    static Pools const quest_have = {
        { "on {quest}", "{quest}", "doing {quest}" },
        { "im on {quest} myself", "doing {quest} atm, what do you need?", "{quest}, what about you?" },
        { "i'm working on {quest} myself, happy to help if you tell me where you are :)", "i'm on {quest} right now, tell me what you're stuck on" } };
    static Pools const quest_none = {
        { "which quest", "what quest", "name?" },
        { "which quest?", "what quest is it?", "whats the quest called?" },
        { "sure, which quest is it?", "happy to help, what's the quest called?", "which one are you stuck on? tell me where you are :)" } };
    static Pools const trade_have = {
        { "{prof}", "i do {prof}", "{prof}, why" },
        { "i do {prof}", "{prof} here, what do you need?", "im into {prof}, what are you after?" },
        { "i do {prof}, what do you need?", "i'm a {prof} person, happy to help if i can :)", "{prof}, tell me what you're after!" } };
    static Pools const trade_none = {
        { "no", "nothing", "nope" },
        { "no professions yet", "not really, still levelling", "nothing yet" },
        { "no professions yet, sorry! still levelling :)", "i haven't picked any up yet, maybe later!" } };
    static Pools const guild_have = {
        { "{guild}", "in {guild}", "<{guild}>" },
        { "im in {guild}", "{guild}, you?", "i'm in <{guild}>" },
        { "i'm in <{guild}>, great bunch :)", "<{guild}> for me, are you looking for one?" } };
    static Pools const guild_none = {
        { "no", "nope", "none" },
        { "no guild", "not in one right now", "unguilded atm" },
        { "no guild right now, are you recruiting? :)", "not in a guild yet, do you know a good one?" } };
    static Pools const name = {
        { "{self}", "{self}", "its {self}" },
        { "im {self}", "{self}, you?", "my name's {self}" },
        { "i'm {self}, nice to meet you :)", "{self}, and you are?", "call me {self}" } };
    static Pools const insult = {
        { "whatever", "ok", "k" },
        { "ok then", "not nice", "alright", "wow ok" },
        { "that's not very nice", "no need for that", "okay... i'll let that go :)" } };
    static Pools const compliment = {
        { "ok", "ty", "yeah" },
        { "thanks!", "ty", "thanks, you too", "appreciate it" },
        { "aw thanks :)", "thank you, that's kind!", "thanks, you're pretty great yourself :)" } };
    static Pools const bye = {
        { "bye", "later", "cya" },
        { "cya", "bye!", "see you around", "later" },
        { "bye, take care :)", "see you around!", "have a good one, bye!" } };
    static Pools const greet_known = {
        { "yo", "hey", "sup {sender}" },
        { "hey {sender}", "hi {sender}!", "hey, what's up?", "hello {sender}" },
        { "hi {sender}! good to see you :)", "hey {sender}, ready when you are", "hello {sender}, how can i help?" } };
    static Pools const dir_here = {
        { "this is it", "you're here", "here" },
        { "that's where i am right now", "you're already close, im here too" },
        { "that's where i am right now :)", "you're nearly there, i'm in the same zone" } };
    static Pools const dir = {
        { "{route}", "{dest}: {route}" },
        { "{dest}? {route}", "{route}", "for {dest}, {route}" },
        { "for {dest}, {route}", "{dest}? {route} :)", "{dest}: {route}. press M for the map if you get lost" } };

    switch (intent)
    {
        case ConvIntent::HowAreYou: return how;
        case ConvIntent::WhereFrom: return from;
        case ConvIntent::WhereAreYou: return where;
        case ConvIntent::Level: return level;
        case ConvIntent::ClassSpec: return cls;
        case ConvIntent::WhatDoing: return doing;
        case ConvIntent::GroupInvite: return variant ? group_busy : group_free;
        case ConvIntent::QuestHelp: return variant ? quest_none : quest_have;
        case ConvIntent::Trade: return variant ? trade_none : trade_have;
        case ConvIntent::Guild: return variant ? guild_none : guild_have;
        case ConvIntent::Name: return name;
        case ConvIntent::Insult: return insult;
        case ConvIntent::Compliment: return compliment;
        case ConvIntent::Goodbye: return bye;
        case ConvIntent::Greeting: return greet_known;
        case ConvIntent::Directions: return variant ? dir_here : dir;
        default: break;
    }
    static Pools const empty;
    return empty;
}

// Light Warcraft humour, kept inoffensive. Tone-independent.
std::vector<std::string> const& Jokes()
{
    static std::vector<std::string> const j = {
        "why did the murloc cross the road? mrglglgl",
        "how many paladins to change a lightbulb? none, they bubble and hearth",
        "a warrior walks into a bar. taunts everything. gets kicked out",
        "i asked a mage for a drink. now i have a very large pile of water",
        "what do you call a rogue with no stealth? a warrior",
        "my hearthstone and i have a lot in common, we're always on cooldown",
        "a hunter's pet walks into a bar. the hunter says 'no, not again'",
        "never trust a warlock who says 'trust me', he probably wants a soulstone placed",
        "healers never die of boredom, only of someone pulling the next pack",
    };
    return j;
}

std::vector<std::string> const& FollowUps()
{
    static std::vector<std::string> const f = { "you?", "and you?", "what about you?", "how about yourself?" };
    return f;
}

std::string JoinProfessions(std::vector<std::string> const& p)
{
    if (p.empty())
        return {};
    if (p.size() == 1)
        return p[0];
    return p[0] + " and " + p[1];
}

// Stable per-bot writing style: some bots type like chat kids (lower-case, no apostrophes), some use proper sentences.
bool IsCasual(uint32_t seed) { return (Mix(seed, 1) & 3u) != 0; }          // 75%
bool UsesSmiley(uint32_t seed) { return (Mix(seed, 2) & 3u) == 0; }        // 25%
bool UsesExclaim(uint32_t seed) { return (Mix(seed, 3) & 7u) == 0; }       // 12.5%

// Capitalises the pronoun in proper-sentence style: "i" -> "I", "im" -> "I'm", "i'll" -> "I'll".
std::string FixPronoun(std::string const& t)
{
    std::string out;
    size_t i = 0;
    while (i < t.size())
    {
        size_t j = i;
        while (j < t.size() && t[j] != ' ')
            ++j;
        std::string w = t.substr(i, j - i);
        std::string stem = w, tail;
        while (!stem.empty() && (stem.back() == ',' || stem.back() == '.' || stem.back() == '!' || stem.back() == '?' || stem.back() == ':'))
        {
            tail.insert(tail.begin(), stem.back());
            stem.pop_back();
        }
        if (stem == "i" || stem == "i'm" || stem == "i'll" || stem == "i'd" || stem == "i've")
            stem[0] = 'I';
        else if (stem == "im")
            stem = "I'm";
        out += stem + tail;
        if (j < t.size())
            out.push_back(' ');
        i = j + 1;
    }
    return out;
}

std::string ApplyStyle(std::string text, ConversationContext const& ctx)
{
    if (text.empty())
        return text;
    bool const smiley = ctx.tone != ConvTone::Rude && UsesSmiley(ctx.bot_seed) && text.find(":)") == std::string::npos;
    if (IsCasual(ctx.bot_seed))
    {
        text = Lower(text);
        text.erase(std::remove(text.begin(), text.end(), '\''), text.end());
        if (smiley && text.back() != '?')
            text += " :)";
        return text;
    }
    text = FixPronoun(text);
    text[0] = char(std::toupper(static_cast<unsigned char>(text[0])));
    if (smiley && text.back() != '?')
        text += " :)";
    else if (text.back() != '?' && text.back() != '!' && text.back() != ')' && text.back() != '>' && text.back() != '.')
        text.push_back(UsesExclaim(ctx.bot_seed) && ctx.tone == ConvTone::Polite ? '!' : '.');
    return text;
}

std::string const& Pick(std::vector<std::string> const& v, uint32_t sel, size_t limit)
{
    size_t const n = std::max<size_t>(1, std::min(limit, v.size()));
    return v[sel % n];
}

std::mutex g_provider_mtx;
std::shared_ptr<ReplyProvider> g_provider;

} // namespace

char const* ConvIntentName(ConvIntent intent)
{
    switch (intent)
    {
        case ConvIntent::HowAreYou: return "how_are_you";
        case ConvIntent::WhereFrom: return "where_from";
        case ConvIntent::WhereAreYou: return "where_are_you";
        case ConvIntent::Level: return "level";
        case ConvIntent::ClassSpec: return "class";
        case ConvIntent::WhatDoing: return "what_doing";
        case ConvIntent::GroupInvite: return "group";
        case ConvIntent::QuestHelp: return "quest";
        case ConvIntent::Directions: return "directions";
        case ConvIntent::Trade: return "profession";
        case ConvIntent::Guild: return "guild";
        case ConvIntent::Name: return "name";
        case ConvIntent::Joke: return "joke";
        case ConvIntent::Insult: return "insult";
        case ConvIntent::Compliment: return "compliment";
        case ConvIntent::Goodbye: return "goodbye";
        case ConvIntent::Greeting: return "greeting";
        case ConvIntent::Unknown: break;
    }
    return "unknown";
}

std::string ConvRepeatReply(ConversationContext const& ctx, uint32_t sel, uint32_t repeat)
{
    static Pools const again = {
        { "again?", "i heard you", "yes yes" },
        { "you already said that", "heard you the first time", "still here, anything else?", "yep, i'm listening" },
        { "you already said that :) what's on your mind?", "i'm still here! anything i can help with?", "got it the first time, thanks :)" } };
    static Pools const insistent = {
        { "...", "what do you want", "stop spamming" },
        { "ok ok, i'm here", "you keep saying that, what do you need?", "i'm not going anywhere, ask away" },
        { "i'm right here, just tell me what you need :)", "happy to chat, what would you like to talk about?" } };
    Pools const& p = repeat >= 3 ? insistent : again;
    std::vector<std::string> const& pool = ctx.tone == ConvTone::Rude ? p.rude : ctx.tone == ConvTone::Polite ? p.polite : p.neutral;
    return ApplyStyle(Pick(pool, Mix(sel, repeat) >> 4, pool.size()), ctx);
}

void ConvSetProvider(std::shared_ptr<ReplyProvider> provider)
{
    std::lock_guard<std::mutex> lk(g_provider_mtx);
    g_provider = std::move(provider);
}

bool ConvReply(ConversationContext const& ctx, std::string const& norm, std::string const& message, uint32_t sel, bool addressed,
               std::string& out)
{
    std::string slot;
    ConvIntent const intent = ConvClassify(norm, &slot);

    if (intent == ConvIntent::Unknown)
    {
        if (!addressed)
            return false;
        std::shared_ptr<ReplyProvider> p;
        {
            std::lock_guard<std::mutex> lk(g_provider_mtx);
            p = g_provider;
        }
        std::string text;
        if (p && p->TryReply(ctx, message, text) && !text.empty())
        {
            out = std::move(text);
            return true;
        }
        return false;
    }

    // A stranger's hello keeps the caller's own pools (ike3 "hi %s, do i know you?"); only someone the bot knows is greeted here.
    if (intent == ConvIntent::Greeting && !ctx.knows_sender)
        return false;

    // Mix the per-bot seed in so two bots asked the same thing do not answer alike.
    uint32_t const s = Mix(sel, ctx.bot_seed);

    std::string text;
    if (intent == ConvIntent::Joke)
    {
        text = Jokes()[s % Jokes().size()];
    }
    else
    {
        bool variant = false;
        switch (intent)
        {
            case ConvIntent::GroupInvite: variant = ctx.in_group; break;
            case ConvIntent::QuestHelp:   variant = ctx.quest_title.empty(); break;
            case ConvIntent::Trade:       variant = ctx.professions.empty(); break;
            case ConvIntent::Guild:       variant = ctx.guild_name.empty(); break;
            case ConvIntent::Directions:
            {
                std::string const zone_l = Lower(ctx.zone);
                variant = !zone_l.empty() && zone_l == slot;
                break;
            }
            default: break;
        }
        Pools const& pools = PoolsFor(intent, variant);
        std::vector<std::string> const* pool = &pools.neutral;
        if (ctx.tone == ConvTone::Rude && !pools.rude.empty())
            pool = &pools.rude;
        else if (ctx.tone == ConvTone::Polite && !pools.polite.empty())
            pool = &pools.polite;
        if (pool->empty())
            return false;
        size_t const limit = ctx.length == ConvLength::Terse ? (pool->size() + 1) / 2 : pool->size();
        text = Pick(*pool, s >> 4, limit);

        std::string const zone = ctx.zone.empty() ? "around here" : ctx.zone;
        std::string const doing = ctx.activity.empty() ? "just hanging around" : ctx.activity;
        std::string const cls = ctx.class_name.empty() ? "adventurer" : ctx.class_name;
        Replace(text, "{zone}", zone);
        Replace(text, "{doing}", doing);
        Replace(text, "{cls}", cls);
        Replace(text, "{lvl}", std::to_string(ctx.level));
        Replace(text, "{quest}", ctx.quest_title);
        Replace(text, "{prof}", JoinProfessions(ctx.professions));
        Replace(text, "{guild}", ctx.guild_name);
        Replace(text, "{self}", ctx.self_name);
        Replace(text, "{sender}", ctx.sender_name);
        if (intent == ConvIntent::Directions)
        {
            if (Destination const* d = FindDestination(norm))
            {
                Replace(text, "{dest}", d->name);
                Replace(text, "{route}", d->route);
            }
        }

        // Chatty bots sometimes hand the question back, the way a person making conversation does.
        if (ctx.length == ConvLength::Chatty && ctx.tone != ConvTone::Rude && text.back() != '?' &&
            (intent == ConvIntent::HowAreYou || intent == ConvIntent::WhatDoing || intent == ConvIntent::WhereFrom ||
             intent == ConvIntent::Level) &&
            (Mix(s, 7) & 1u))
        {
            while (!text.empty() && (text.back() == '!' || text.back() == '.'))
                text.pop_back();
            text += ", " + Pick(FollowUps(), Mix(s, 8), FollowUps().size());
        }
    }

    out = ApplyStyle(std::move(text), ctx);
    return !out.empty();
}

} // namespace Playerbot::V2
