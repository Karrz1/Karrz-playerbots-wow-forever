// BotConversation - see BotConversation.h.

#include "BotConversation.h"
#include "TradeTalk.h"

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
    char const* ru_keys[4];                // the Russian names a player types, folded (lower case, ё as е); a phrase is matched whole
    char const* ru_name;                   // the client's Russian name
    char const* ru_route;
};

// Vanilla geography only; nothing here that is not true of the Classic world. The Russian names are the ruRU client's.
Destination const kDestinations[] = {
    { { "stormwind", "sw" }, "Stormwind", "follow the road out of Elwynn Forest, or take the Deeprun Tram from Ironforge",
      { "штормград", "штормграде", "штормграда", "шторм" }, "Штормград", "иди по дороге из Элвиннского леса или садись на подземку в Стальгорне" },
    { { "ironforge" }, "Ironforge", "it is in Dun Morogh, or take the Deeprun Tram from Stormwind",
      { "стальгорн", "стальгорне", "стальгорна" }, "Стальгорн", "он в Дун Мороге, или садись на подземку в Штормграде" },
    { { "darnassus" }, "Darnassus", "it is up in Teldrassil, and a boat from Auberdine in Darkshore gets you to Rutheran Village",
      { "дарнас", "дарнасе", "дарнаса" }, "Дарнас", "он наверху Тельдрассила, корабль из Аубердина на Темных берегах идет в деревню Рут'теран" },
    { { "orgrimmar", "org" }, "Orgrimmar", "it is in Durotar, and the zeppelin from Tirisfal Glades brings you right to it",
      { "оргриммар", "оргриммаре", "оргриммара", "огри" }, "Оргриммар", "он в Дуротаре, цеппелин из Тирисфальских лесов прилетает прямо туда" },
    { { "thunder bluff", "tb" }, "Thunder Bluff", "it is up on the mesas of Mulgore, take the lift up",
      { "громовой утес", "громового утеса", "громовом утесе" }, "Громовой Утес", "он на столовых горах Мулгора, поднимайся на лифте" },
    { { "undercity", "uc" }, "Undercity", "the entrance is in Tirisfal Glades, by the ruins of Lordaeron",
      { "подгород", "подгороде", "подгорода", "андерсити" }, "Подгород", "вход в Тирисфальских лесах, у руин Лордерона" },
    { { "elwynn forest", "elwynn" }, "Elwynn Forest", "it is the zone around Stormwind, head out of the main gate",
      { "элвиннский лес", "элвин", "элвинн", "элвиннском лесу" }, "Элвиннский лес", "это лес вокруг Штормграда, выходи через главные ворота" },
    { { "westfall" }, "Westfall", "it is just west of Elwynn Forest",
      { "западный край", "западном крае", "западного края" }, "Западный Край", "он сразу к западу от Элвиннского леса" },
    { { "duskwood" }, "Duskwood", "it is south of Elwynn Forest",
      { "сумеречный лес", "сумеречном лесу", "сумеречного леса" }, "Сумеречный лес", "он к югу от Элвиннского леса" },
    { { "redridge mountains", "redridge" }, "Redridge Mountains", "it is east of Elwynn Forest",
      { "красногорье", "красногорья" }, "Красногорье", "оно к востоку от Элвиннского леса" },
    { { "dun morogh" }, "Dun Morogh", "it is the snowy zone around Ironforge",
      { "дун морог", "дун мороге", "дун морога" }, "Дун Морог", "это снежная зона вокруг Стальгорна" },
    { { "loch modan" }, "Loch Modan", "it is east of Dun Morogh",
      { "лок модан", "лок модане", "лок модана" }, "Лок Модан", "он к востоку от Дун Морога" },
    { { "teldrassil" }, "Teldrassil", "take the boat from Auberdine in Darkshore to Rutheran Village",
      { "тельдрассил", "тельдрассиле", "тельдрассила" }, "Тельдрассил", "плыви на корабле из Аубердина на Темных берегах до деревни Рут'теран" },
    { { "darkshore" }, "Darkshore", "it is the coast south of Teldrassil, reach it by boat from Rutheran Village",
      { "темные берега", "темных берегах", "темных берегов" }, "Темные берега", "это побережье к югу от Тельдрассила, туда ходит корабль из деревни Рут'теран" },
    { { "durotar" }, "Durotar", "it is the orc starting zone, Orgrimmar is there, just east of the Barrens",
      { "дуротар", "дуротаре", "дуротара" }, "Дуротар", "это стартовая зона орков, там Оргриммар, сразу к востоку от Степей" },
    { { "mulgore" }, "Mulgore", "it is south-west of the Barrens, Thunder Bluff is there",
      { "мулгор", "мулгоре", "мулгора" }, "Мулгор", "он к юго-западу от Степей, там Громовой Утес" },
    { { "the barrens", "barrens", "crossroads" }, "The Barrens", "it is between Durotar and Mulgore, the Crossroads is the main town",
      { "степи", "степях", "степей", "перекресток" }, "Степи", "они между Дуротаром и Мулгором, главный городок там Перекресток" },
    { { "tirisfal glades", "tirisfal" }, "Tirisfal Glades", "it is the undead starting zone, Undercity is below it",
      { "тирисфальские леса", "тирисфальских лесах", "тирисфаль", "тирисфальских лесов" }, "Тирисфальские леса", "это стартовая зона нежити, под ней Подгород" },
    // The dungeons of the low levels: "where is deadmines" in General went unanswered (GitHub #12, 2026-11-17).
    { { "deadmines", "deadmine" }, "the Deadmines", "it is under Moonbrook in Westfall, the entrance is in the mine there",
      { "мертвые копи", "мертвых копях", "мертвых копей", "дедмайнс" }, "Мертвые копи", "они под Луноречьем в Западном Крае, вход в шахте там" },
    { { "wailing caverns", "wailing" }, "Wailing Caverns", "it is in the Barrens, in a cave by the oasis south-west of the Crossroads",
      { "пещеры стенаний", "пещерах стенаний", "пещер стенаний" }, "Пещеры Стенаний", "они в Степях, в пещере у оазиса к юго-западу от Перекрестка" },
    { { "ragefire chasm", "ragefire", "rfc" }, "Ragefire Chasm", "it is inside Orgrimmar, in the Cleft of Shadow",
      { "огненная пропасть", "огненной пропасти", "огненную пропасть" }, "Огненная пропасть", "она внутри Оргриммара, в Расселине Теней" },
    { { "shadowfang keep", "shadowfang", "sfk" }, "Shadowfang Keep", "it is the castle in Silverpine Forest, near Pyrewood Village",
      { "крепость темного клыка", "крепости темного клыка", "темного клыка" }, "Крепость Темного Клыка", "это замок в Серебряном бору" },
    { { "blackfathom deeps", "blackfathom", "bfd" }, "Blackfathom Deeps", "it is on the Zoram Strand coast of Ashenvale",
      { "непроглядная пучина", "непроглядной пучине", "непроглядную пучину" }, "Непроглядная Пучина", "она на западном побережье Ясеневого леса" },
    { { "gnomeregan", "gnomer" }, "Gnomeregan", "it is in Dun Morogh, west of Ironforge",
      { "гномреган", "гномрегане", "гномрегана" }, "Гномреган", "он в Дун Мороге, к западу от Стальгорна" },
    { { "razorfen kraul", "rfk" }, "Razorfen Kraul", "it is in the far south of the Barrens, by Thousand Needles",
      { "лабиринты иглошкурых", "лабиринтах иглошкурых", "лабиринтов иглошкурых" }, "Лабиринты Иглошкурых", "они на самом юге Степей, у Тысячи Игл" },
    { { "scarlet monastery", "monastery" }, "the Scarlet Monastery", "it is in the north-east of Tirisfal Glades",
      { "монастырь алого ордена", "монастыре алого ордена", "монастырь", "монастыре" }, "Монастырь Алого ордена", "он на северо-востоке Тирисфальских лесов" },
    { { "stockade", "stockades", "stocks" }, "the Stockade", "it is inside Stormwind City",
      { "тюрьма", "тюрьму", "тюрьме", "тюрьмы" }, "Тюрьма", "она внутри Штормграда" },
};

Destination const* FindDestination(std::string const& norm)
{
    for (Destination const& d : kDestinations)
    {
        for (char const* k : d.keys)
            if (k && HasWord(norm, k))
                return &d;
        for (char const* k : d.ru_keys)
            if (k && HasWord(norm, k))
                return &d;
    }
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
    return TradeTalk::FoldRussian(out);
}

ConvIntent ConvClassify(std::string const& norm, std::string* slot)
{
    if (slot)
        slot->clear();
    if (norm.empty())
        return ConvIntent::Unknown;

    bool const you = IsYouWord(norm) || HasAny(norm, { "ты", "тебя", "тебе", "твой", "твое", "твоя", "вы", "вас", "сам" });

    // Insults first: they should not be read as a question that happens to contain "you".
    if (HasAny(norm, { "stupid", "idiot", "noob", "suck", "sucks", "trash", "useless", "dumb", "moron", "ugly", "stfu" }) ||
        HasWord(norm, "shut up") || HasWord(norm, "fuck you") || HasWord(norm, "f you") ||
        HasAny(norm, { "тупой", "тупая", "дурак", "дура", "идиот", "нуб", "лох", "лошара", "отстой", "бесполезный", "бесполезная",
                       "заткнись", "дебил", "урод", "криворукий" }))
        return ConvIntent::Insult;

    // "are you a bot?" before the rest: "are you a bot or a real player" is not a group question.
    if ((you && HasAny(norm, { "bot", "bots", "ai", "robot", "npc", "human", "program", "script", "scripted" })) ||
        HasWord(norm, "real person") || HasWord(norm, "real player") ||
        (HasAny(norm, { "бот", "боты", "робот", "нейросеть", "нейронка", "ии", "нпс", "живой", "человек", "скрипт" }) && you))
        return ConvIntent::AreYouBot;

    if (HasAny(norm, { "bye", "cya", "goodbye", "gn", "goodnight", "gtg", "g2g", "later", "laters", "farewell", "ttyl", "see you", "see ya" }) ||
        HasWord(norm, "good night") || HasWord(norm, "got to go") || HasWord(norm, "have to go") ||
        HasAny(norm, { "пока", "бб", "бай", "увидимся", "споки", "до встречи", "спокойной ночи", "мне пора", "до свидания" }))
        return ConvIntent::Goodbye;

    if (HasAny(norm, { "thanks", "thank", "thx", "ty", "tyvm", "tysm", "thnx" }) || HasWord(norm, "appreciate it") ||
        HasAny(norm, { "спасибо", "спс", "пасиб", "пасибо", "благодарю", "сенкс", "спасибки" }))
        return ConvIntent::Thanks;

    if (HasWord(norm, "joke") || HasWord(norm, "funny") || HasWord(norm, "make me laugh") ||
        HasAny(norm, { "анекдот", "шутку", "пошути", "рассмеши", "шутка" }))
        return ConvIntent::Joke;

    if (HasAny(norm, { "duel", "duels", "dueling", "1v1", "spar" }) ||
        HasAny(norm, { "дуэль", "дуэли", "дуэльку", "подуэлимся", "дуэлимся", "дуэлька" }))
        return ConvIntent::Duel;

    // Directions: "how do i get to X", "where is X", "way to X", "path to X", "how to get to X" (Russian: "как пройти в X", "где X").
    bool const asks_way = (HasWord(norm, "how") && (HasWord(norm, "get") || HasWord(norm, "go") || HasWord(norm, "reach"))) ||
                          HasWord(norm, "directions") || HasWord(norm, "way to") || HasWord(norm, "path to") ||
                          HasWord(norm, "route") || (HasWord(norm, "where") && (HasWord(norm, "is") || HasWord(norm, "s") || HasWord(norm, "are")) && !you) ||
                          HasWord(norm, "wheres") ||
                          HasAny(norm, { "как пройти", "как добраться", "как попасть", "как дойти", "где находится", "дорогу" }) ||
                          ((HasWord(norm, "где") || HasWord(norm, "куда")) && !you);
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

    // "wanna do [quest] together?" is an offer to group up: answered as one ("sure, invite me" / "too high level for me"), the
    // same as "wanna group?". A plea for help with it ("can you help me with this quest", "where is ...") stays a quest question.
    if (HasWord(norm, "quest") || HasWord(norm, "quests") || HasWord(norm, "q"))
    {
        if (HasAny(norm, { "wanna", "want", "lets", "together", "join", "duo", "wana" }) &&
            !HasAny(norm, { "help", "stuck", "find", "where", "how" }))
            return ConvIntent::GroupInvite;
        if (HasAny(norm, { "help", "stuck", "find", "where", "how", "do", "need", "doing", "on", "got", "have", "any" }))
            return ConvIntent::QuestHelp;
    }
    if (HasAny(norm, { "квест", "квесты", "квестик", "задание", "задания", "квеста", "квесту", "квестом", "квестами", "заданием", "заданию" }))
    {
        bool const plea = HasAny(norm, { "помоги", "помочь", "помощь", "где", "как", "застрял", "найти" });
        if (!plea && HasAny(norm, { "вместе", "го", "давай", "пойдем", "пойдешь", "хочешь", "погнали" }))
            return ConvIntent::GroupInvite;
        if (plea || HasAny(norm, { "делаешь", "делать", "сделать", "есть", "какой" }))
            return ConvIntent::QuestHelp;
    }

    if (HasAny(norm, { "guild", "guilds" }) || HasAny(norm, { "гильдия", "гильдию", "гильдии", "гильда", "гильду", "ги" }))
        return ConvIntent::Guild;

    if (HasAny(norm, { "profession", "professions", "craft", "crafting", "crafter", "enchant", "enchanter", "mats", "recipe", "recipes", "mining",
                       "herbalism", "skinning", "tailoring", "alchemy", "blacksmithing", "engineering", "leatherworking", "cooking", "fishing" }) ||
        HasWord(norm, "make me") || HasWord(norm, "can you make") ||
        HasAny(norm, { "профессия", "профессии", "профу", "профа", "крафт", "скрафтить", "скрафтишь", "скрафти", "рецепт", "рецепты" }))
        return ConvIntent::Trade;

    if (HasAny(norm, { "wtt" }) ||
        (HasWord(norm, "trade") && (HasAny(norm, { "wanna", "want", "can", "lets", "do", "u", "you" }) || norm == "trade")) ||
        HasAny(norm, { "трейд", "обмен", "обменяемся", "обменяться", "поторгуем", "поторговать", "торговать" }))
        return ConvIntent::WantTrade;

    if ((HasWord(norm, "where") && HasAny(norm, { "from", "come from", "born" })) || HasWord(norm, "откуда"))
        return ConvIntent::WhereFrom;

    if ((HasAny(norm, { "name", "called" }) && !HasAny(norm, { "quest", "quests" }) && (you || HasWord(norm, "whats") || HasWord(norm, "what"))) || HasWord(norm, "who are you") ||
        HasWord(norm, "who r u") || HasWord(norm, "who is this") ||
        HasAny(norm, { "как тебя зовут", "как зовут", "твое имя", "кто ты", "ты кто" }))
        return ConvIntent::Name;

    if ((HasWord(norm, "where") && you) || ((HasWord(norm, "где") || HasWord(norm, "куда")) && you))
        return ConvIntent::WhereAreYou;
    if (HasWord(norm, "wherer") || HasWord(norm, "wo bist du"))
        return ConvIntent::WhereAreYou;

    if ((HasAny(norm, { "level", "lvl", "lv" }) && !HasWord(norm, "how to level")) || HasAny(norm, { "уровень", "лвл", "лвл?", "уровня", "левел" }))
        return ConvIntent::Level;

    if (((HasWord(norm, "class") || HasWord(norm, "spec") || HasWord(norm, "talents")) && (you || HasWord(norm, "what") || HasWord(norm, "which"))) ||
        HasAny(norm, { "какой класс", "кем играешь", "спек", "какой спек", "за кого играешь" }))
        return ConvIntent::ClassSpec;

    if (((HasWord(norm, "how") || HasWord(norm, "hows")) && ((HasAny(norm, { "are", "r", "re" }) && you) || HasAny(norm, { "going", "doing", "things", "been", "life" }))) ||
        HasAny(norm, { "как дела", "как ты", "как жизнь", "как сам", "как оно", "как поживаешь", "как настроение" }))
        return ConvIntent::HowAreYou;
    if (HasAny(norm, { "hbu", "wbu", "whats up", "wassup", "wazzup" }) && !HasWord(norm, "doing"))
        return ConvIntent::HowAreYou;

    if ((HasWord(norm, "what") && (HasWord(norm, "doing") || HasWord(norm, "up to"))) || HasWord(norm, "wyd") ||
        (HasWord(norm, "sup") && HasWord(norm, "doing")) || HasWord(norm, "busy") ||
        HasAny(norm, { "что делаешь", "че делаешь", "чо делаешь", "чем занят", "чем занимаешься", "что творишь", "занят" }))
        return ConvIntent::WhatDoing;

    if (HasAny(norm, { "group", "party", "team", "invite", "lfg", "lfm", "dungeon", "dungeons", "run", "join" }))
    {
        if (HasAny(norm, { "want", "wanna", "can", "join", "invite", "lfg", "lfm", "up", "need", "come", "wana", "would", "will" }))
            return ConvIntent::GroupInvite;
    }
    if (HasAny(norm, { "группу", "группа", "группе", "пати", "инвайт", "инв", "данж", "данжен", "подземелье", "рейд" }) &&
        HasAny(norm, { "хочешь", "давай", "го", "можно", "пойдешь", "пойдем", "возьми", "возьмешь", "примешь", "нужен", "нужна", "ищу",
                       "кинь", "кинуть", "инв", "инвайт", "пойти", "вступить", "вместе", "с нами" }))
        return ConvIntent::GroupInvite;

    if (HasWord(norm, "do you like") || HasWord(norm, "do u like") || HasWord(norm, "you like") || HasWord(norm, "u like") ||
        HasWord(norm, "like it") || HasWord(norm, "enjoying") || HasWord(norm, "is it fun") || HasWord(norm, "having fun") ||
        HasAny(norm, { "нравится", "нравятся", "любишь", "по кайфу", "интересно играть" }))
        return ConvIntent::LikeIt;

    if (HasWord(norm, "help me") || HasWord(norm, "can you help") || HasWord(norm, "can u help") || HasWord(norm, "need help") ||
        HasWord(norm, "help pls") || HasWord(norm, "help plz") || norm == "help" ||
        HasAny(norm, { "помоги", "помогите", "помочь", "нужна помощь", "поможешь" }))
        return ConvIntent::Help;

    if (HasAny(norm, { "awesome", "amazing", "nice", "cool", "great", "love", "best", "rock", "rocks", "legend" }) &&
        (you || HasAny(norm, { "job", "bot", "player" })))
        return ConvIntent::Compliment;
    if (HasAny(norm, { "молодец", "красава", "красавчик", "крутой", "крут", "лучший", "респект", "уважаю" }))
        return ConvIntent::Compliment;

    // A short line that opens with a greeting ("hi", "hey Aron", "hello there"); a longer line is something else.
    if (std::count(norm.begin(), norm.end(), ' ') < 3 &&
        (HasAny(norm, { "hi", "hey", "hello", "heya", "hiya", "howdy", "yo", "sup", "hallo", "moin", "hola", "bonjour", "morning" }) ||
         HasAny(norm, { "привет", "прив", "приветик", "здарова", "здорова", "здравствуй", "здравствуйте", "хай", "ку", "салам", "доброе утро",
                        "добрый вечер", "добрый день", "приветствую" })))
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
Pools const& PoolsForEn(ConvIntent intent, int variant)
{
    // variant selects the "no data" form where one exists (no quest, no guild, no profession); for a duel, the DuelAnswer; for a group, the GroupAnswer.
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
    static Pools const group_too_high = {
        { "too low for you", "ur way higher lvl", "nah, lvl gap" },
        { "you're a bit too high level for me, sorry", "i'd just slow you down, you're way higher level", "sorry, our levels are too far apart" },
        { "sorry, you're a fair bit higher level, i'd only hold you back :)", "you're way ahead of me, i'd just slow you down, sorry!" } };
    static Pools const group_not_now = {
        { "not now", "cant rn", "later" },
        { "not right now, sorry", "can't right now, in the middle of something", "maybe in a bit, not now" },
        { "not right now sorry, maybe a bit later? :)", "can't just now, ask me again in a few minutes?" } };
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

    static Pools const duel_yes = {
        { "sure", "ok, send it", "bring it" },
        { "sure, send it", "ok, you're on", "alright, send the duel", "sure, why not" },
        { "sure, send it over :)", "happy to, send the duel", "ok, good luck!" } };
    static Pools const duel_not_now = {
        { "no", "not now", "busy" },
        { "not right now", "maybe later", "cant right now, sorry" },
        { "sorry, not right now, maybe later :)", "can't at the moment, ask me again in a bit?" } };
    static Pools const duel_too_far = {
        { "no, unfair", "lol no", "not a fair fight" },
        { "we're too far apart in level", "not a fair fight, sorry", "ha, no, the levels are too far apart" },
        { "sorry, our levels are too far apart for a fair duel", "that wouldn't be a fair fight, maybe when we're closer in level :)" } };
    static Pools const duel_not_here = {
        { "not here", "cant here" },
        { "can't duel in here", "not in here, sorry" },
        { "sorry, we can't duel in here :)" } };
    static Pools const want_trade = {
        { "what", "for what", "what u got" },
        { "what are you after?", "depends, what do you need?", "sure, what do you need?", "what do you have in mind?" },
        { "sure, what are you looking for?", "happy to, what do you need?", "what did you have in mind? :)" } };
    static Pools const thanks = {
        { "np", "yep", "k" },
        { "np", "no problem", "anytime", "you're welcome" },
        { "no problem at all :)", "anytime!", "you're welcome, have fun" } };
    static Pools const not_a_bot = {
        { "lol no", "no?", "do i sound like one", "no u" },
        { "lol no, just playing", "nope, why?", "haha no", "no, just slow at typing" },
        { "haha no, just a regular player :)", "nope! just enjoying the game", "no, just me at my keyboard :)" } };
    static Pools const like_it = {
        { "its ok", "yeah", "sure" },
        { "yeah, it's fun", "love it", "it's alright, having fun", "yeah, been playing a lot" },
        { "yes, i'm really enjoying it :)", "love it, feels like the old days", "it's great fun, you?" } };
    static Pools const help = {
        { "with what", "what", "what now" },
        { "with what?", "sure, what do you need?", "what's up?", "maybe, what is it?" },
        { "sure, what do you need? :)", "happy to help if i can, what's up?", "what can i do for you?" } };

    switch (intent)
    {
        case ConvIntent::Duel:
            return variant == int(DuelAnswer::Yes) ? duel_yes : variant == int(DuelAnswer::TooFar) ? duel_too_far
                 : variant == int(DuelAnswer::NotHere) ? duel_not_here : duel_not_now;
        case ConvIntent::WantTrade: return want_trade;
        case ConvIntent::Thanks: return thanks;
        case ConvIntent::AreYouBot: return not_a_bot;
        case ConvIntent::LikeIt: return like_it;
        case ConvIntent::Help: return help;
        case ConvIntent::HowAreYou: return how;
        case ConvIntent::WhereFrom: return from;
        case ConvIntent::WhereAreYou: return where;
        case ConvIntent::Level: return level;
        case ConvIntent::ClassSpec: return cls;
        case ConvIntent::WhatDoing: return doing;
        case ConvIntent::GroupInvite:
            return variant == int(GroupAnswer::InGroup) ? group_busy : variant == int(GroupAnswer::TooHigh) ? group_too_high
                 : variant == int(GroupAnswer::NotNow) ? group_not_now : group_free;
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

// The same answers for a player who reads Russian. Casual chat Russian, present tense, nothing that needs the bot's gender (no
// "готов/готова", no past tense). {cls}, {doing} and {prof} come in Russian (ConvReply); {zone}, {quest} and {guild} are the
// server's names.
Pools const& PoolsForRu(ConvIntent intent, int variant)
{
    static Pools const how = {
        { "норм", "нормально", "живой", "пойдет" },
        { "нормально, сам как?", "все хорошо", "неплохо, {doing}", "да норм, {doing}" },
        { "все отлично, спасибо! а у тебя как?", "хорошо, {doing}. а ты как?", "прекрасно, спасибо что спрашиваешь :)" } };
    static Pools const from = {
        { "отсюда", "{zone}", "да кто знает, щас {zone}" },
        { "щас в {zone}", "отовсюду понемногу, сейчас {zone}", "много где бываю, сейчас {zone}" },
        { "я {cls} отсюда, сейчас в {zone}", "отовсюду понемногу, сейчас {zone}, а ты?" } };
    static Pools const where = {
        { "{zone}", "в {zone}", "тут, {zone}" },
        { "я в {zone}", "сейчас {zone}", "в {zone} пока" },
        { "я сейчас в {zone} :)", "в {zone}, заходи!", "найдешь меня в {zone}" } };
    static Pools const level = {
        { "{lvl}", "{lvl} {cls}", "{lvl} лвл" },
        { "{lvl} лвл, {cls}", "{lvl}, {cls}", "{lvl} {cls}, а ты?", "{lvl} уровень" },
        { "{lvl} уровень, {cls} :)", "{lvl} лвл {cls}, а у тебя какой?", "{lvl}, {cls}, качаюсь потихоньку" } };
    static Pools const cls = {
        { "{cls}", "{cls}, а что?", "{cls} же" },
        { "{cls}", "{cls}, {lvl} лвл", "{cls}, а ты кем играешь?" },
        { "{cls} :) {lvl} уровень", "{cls}, очень нравится", "{cls}, а ты кем?" } };
    static Pools const doing = {
        { "{doing}", "да ничего", "{doing}, а что?" },
        { "{doing}", "да так, {doing}", "{doing} в {zone}", "{doing}, а ты?" },
        { "{doing} в {zone}, а ты?", "{doing}, нравится :)", "{doing}, а ты чем занимаешься?" } };
    static Pools const group_free = {
        { "кидай инв", "давай", "инв" },
        { "давай, кидай инв", "го, кидай приглашение", "можно, инвайть", "давай, почему нет, кидай инв" },
        { "конечно, кидай приглашение :)", "с радостью, кидай инв", "давай, приглашай" } };
    static Pools const group_busy = {
        { "я в группе", "занято", "не могу" },
        { "я уже в группе, сорри", "сейчас с пати бегаю", "пока в группе" },
        { "я уже в группе, извини! может позже :)", "сейчас с группой, спроси чуть позже?" } };
    static Pools const group_too_high = {
        { "у нас разные уровни", "ты сильно выше", "разница в уровнях" },
        { "извини, у тебя уровень сильно выше моего", "со мной будет только медленнее, ты намного выше уровнем", "сорри, слишком большая разница в уровнях" },
        { "извини, ты заметно выше уровнем, со мной будет только медленнее :)", "ты намного выше уровнем, я буду только тормозить, извини!" } };
    static Pools const group_not_now = {
        { "не сейчас", "пока не могу", "позже" },
        { "сейчас не могу, извини", "не сейчас, тут дела", "может чуть позже, сейчас никак" },
        { "сейчас никак, извини, может чуть позже? :)", "прямо сейчас не получится, спроси через пару минут?" } };
    static Pools const quest_have = {
        { "{quest}", "делаю {quest}", "на {quest}" },
        { "сам делаю {quest}", "делаю {quest}, а тебе что нужно?", "{quest}, а у тебя?" },
        { "сейчас делаю {quest}, могу помочь, скажи где ты :)", "я на {quest}, скажи на чем застрял" } };
    static Pools const quest_none = {
        { "какой квест", "что за квест", "название?" },
        { "какой квест?", "что за квест?", "как квест называется?" },
        { "давай, а какой квест?", "помогу, как он называется?", "на каком квесте застрял? скажи где ты :)" } };
    static Pools const trade_have = {
        { "{prof}", "у меня {prof}", "{prof}, а что?" },
        { "у меня {prof}", "{prof}, что нужно?", "качаю {prof}, что ищешь?" },
        { "у меня {prof}, что нужно?", "{prof}, помогу если смогу :)", "{prof}, скажи что нужно!" } };
    static Pools const trade_none = {
        { "нет", "никаких", "неа" },
        { "пока без профессий", "да нет, еще качаюсь", "пока ничего" },
        { "профессий пока нет, извини! еще качаюсь :)", "пока ничего не взято, может потом!" } };
    static Pools const guild_have = {
        { "{guild}", "в {guild}", "<{guild}>" },
        { "я в {guild}", "{guild}, а ты?", "состою в <{guild}>" },
        { "я в <{guild}>, отличные ребята :)", "<{guild}>, а ты ищешь гильдию?" } };
    static Pools const guild_none = {
        { "нет", "неа", "никакой" },
        { "без гильдии", "пока ни в какой", "сейчас без ги" },
        { "пока без гильдии, вы набираете? :)", "пока не в гильдии, посоветуешь хорошую?" } };
    static Pools const name = {
        { "{self}", "{self} же", "я {self}" },
        { "я {self}", "{self}, а ты?", "меня зовут {self}" },
        { "я {self}, приятно познакомиться :)", "{self}, а тебя как?", "зови меня {self}" } };
    static Pools const insult = {
        { "ну и ладно", "ок", "пф" },
        { "ну ок", "грубо", "ладно", "ну спасибо" },
        { "это было не очень приятно", "зачем так", "ладно... проехали :)" } };
    static Pools const compliment = {
        { "ок", "спс", "ага" },
        { "спасибо!", "спс", "спасибо, ты тоже", "приятно" },
        { "ой, спасибо :)", "спасибо, очень приятно!", "спасибо, ты тоже ничего :)" } };
    static Pools const bye = {
        { "пока", "бб", "давай" },
        { "пока!", "бб", "до встречи", "увидимся" },
        { "пока, удачи :)", "до встречи!", "хорошей игры, пока!" } };
    static Pools const greet_known = {
        { "ку", "хай", "привет {sender}" },
        { "привет {sender}", "о, {sender}!", "привет, как оно?", "здарова {sender}" },
        { "привет {sender}! хорошо что ты тут :)", "привет {sender}, я тут", "привет {sender}, чем помочь?" } };
    static Pools const dir_here = {
        { "это тут", "ты уже тут", "здесь" },
        { "я как раз тут", "ты рядом, я тоже тут" },
        { "я как раз здесь :)", "ты почти на месте, я в этой же зоне" } };
    static Pools const dir = {
        { "{route}", "{dest}: {route}" },
        { "{dest}? {route}", "{route}", "в {dest}? {route}" },
        { "{dest}: {route}", "{dest}? {route} :)", "{dest}: {route}. если что, жми M для карты" } };
    static Pools const duel_yes = {
        { "давай", "го", "кидай" },
        { "давай, кидай дуэль", "го", "ок, кидай", "давай, почему нет" },
        { "давай, кидай :)", "с удовольствием, кидай дуэль", "давай, удачи!" } };
    static Pools const duel_not_now = {
        { "нет", "не сейчас", "некогда" },
        { "не сейчас", "может позже", "сейчас не могу, сорри" },
        { "извини, сейчас не могу, может позже :)", "сейчас не получится, спроси чуть позже?" } };
    static Pools const duel_too_far = {
        { "нет, нечестно", "лол нет", "нечестно будет" },
        { "у нас слишком большая разница в уровнях", "нечестный бой будет, сорри", "не, уровни слишком разные" },
        { "извини, уровни слишком разные для честной дуэли", "это будет нечестно, давай когда уровни будут ближе :)" } };
    static Pools const duel_not_here = {
        { "не тут", "тут нельзя" },
        { "тут нельзя дуэлиться", "не здесь, сорри" },
        { "извини, тут дуэли нельзя :)" } };
    static Pools const want_trade = {
        { "что", "на что", "а что есть" },
        { "а что нужно?", "смотря что нужно", "давай, а что надо?", "что ищешь?" },
        { "давай, а что ищешь?", "можно, что нужно?", "а что тебе нужно? :)" } };
    static Pools const thanks = {
        { "ага", "нзч", "угу" },
        { "не за что", "обращайся", "да не за что", "пожалуйста" },
        { "да не за что :)", "обращайся!", "пожалуйста, удачи" } };
    static Pools const not_a_bot = {
        { "лол нет", "нет?", "похоже что ли", "сам ты бот" },
        { "лол нет, просто играю", "неа, а что?", "ахах нет", "нет, просто медленно печатаю" },
        { "ахах нет, обычный игрок :)", "нет! просто кайфую от игры", "нет, это я за клавиатурой :)" } };
    static Pools const like_it = {
        { "норм", "ага", "ну да" },
        { "да, прикольно", "очень", "нормально, мне нравится", "да, затягивает" },
        { "да, очень нравится :)", "обожаю, как в старые добрые", "очень нравится, а тебе?" } };
    static Pools const help = {
        { "с чем", "чего", "что надо" },
        { "с чем?", "давай, что нужно?", "что случилось?", "может, а что такое?" },
        { "конечно, что нужно? :)", "помогу если смогу, что случилось?", "чем могу помочь?" } };

    switch (intent)
    {
        case ConvIntent::Duel:
            return variant == int(DuelAnswer::Yes) ? duel_yes : variant == int(DuelAnswer::TooFar) ? duel_too_far
                 : variant == int(DuelAnswer::NotHere) ? duel_not_here : duel_not_now;
        case ConvIntent::WantTrade: return want_trade;
        case ConvIntent::Thanks: return thanks;
        case ConvIntent::AreYouBot: return not_a_bot;
        case ConvIntent::LikeIt: return like_it;
        case ConvIntent::Help: return help;
        case ConvIntent::HowAreYou: return how;
        case ConvIntent::WhereFrom: return from;
        case ConvIntent::WhereAreYou: return where;
        case ConvIntent::Level: return level;
        case ConvIntent::ClassSpec: return cls;
        case ConvIntent::WhatDoing: return doing;
        case ConvIntent::GroupInvite:
            return variant == int(GroupAnswer::InGroup) ? group_busy : variant == int(GroupAnswer::TooHigh) ? group_too_high
                 : variant == int(GroupAnswer::NotNow) ? group_not_now : group_free;
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

Pools const& PoolsFor(ConvIntent intent, int variant, bool russian)
{
    return russian ? PoolsForRu(intent, variant) : PoolsForEn(intent, variant);
}

// A question the rules did not understand, asked of this bot by name or in a whisper, with no AI to answer it: ask back, the way
// a player does, instead of the old build's "dunno" / "how should i know" (GitHub #12: every whisper got those).
Pools const& AskBackPools(bool russian)
{
    static Pools const en = {
        { "what?", "huh", "say what" },
        { "what do you mean?", "hm, not sure what you mean", "say that again?", "huh? what do you mean" },
        { "sorry, what do you mean?", "not sure i follow, can you say it another way?", "hm? tell me a bit more" } };
    static Pools const ru = {
        { "чего?", "а?", "в смысле" },
        { "в смысле?", "не понимаю, о чем ты", "а можно еще раз?", "хм, что ты имеешь в виду?" },
        { "извини, а что ты имеешь в виду?", "не совсем понимаю, скажи по-другому?", "хм? расскажи подробнее" } };
    return russian ? ru : en;
}

// Light Warcraft humour, kept inoffensive. Tone-independent.
std::vector<std::string> const& JokesRu()
{
    static std::vector<std::string> const j = {
        "почему мурлок перешел дорогу? мргллгл",
        "сколько паладинов нужно, чтобы вкрутить лампочку? ни одного, они уходят в пузырь и камень возвращения",
        "воин заходит в таверну. провоцирует всех. его выгоняют",
        "попросил мага о выпивке. теперь у меня гора воды",
        "разбойник без незаметности это просто воин",
        "у меня с камнем возвращения много общего, мы оба вечно на перезарядке",
        "лекари не умирают от скуки, только от того, кто спулил следующую пачку",
    };
    return j;
}

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

std::vector<std::string> const& FollowUps(bool russian)
{
    static std::vector<std::string> const f = { "you?", "and you?", "what about you?", "how about yourself?" };
    static std::vector<std::string> const ru = { "а ты?", "а у тебя?", "а сам?", "а ты как?" };
    return russian ? ru : f;
}

// The Russian words for what ConversationContext carries in English (MakeConvContext's class, activity and profession words).
std::string ToRussian(std::string const& en)
{
    static constexpr std::pair<char const*, char const*> kWords[] = {
        { "warrior", "воин" }, { "paladin", "паладин" }, { "hunter", "охотник" }, { "rogue", "разбойник" }, { "priest", "жрец" },
        { "death knight", "рыцарь смерти" }, { "shaman", "шаман" }, { "mage", "маг" }, { "warlock", "чернокнижник" }, { "monk", "монах" },
        { "druid", "друид" }, { "demon hunter", "охотник на демонов" }, { "evoker", "пробудитель" }, { "adventurer", "искатель приключений" },
        { "out questing", "делаю квесты" }, { "running errands in town", "бегаю по делам в городе" }, { "just wandering around", "просто гуляю" },
        { "running with a group", "бегаю с группой" }, { "just hanging around", "просто отдыхаю" },
        { "alchemy", "алхимия" }, { "blacksmithing", "кузнечное дело" }, { "enchanting", "наложение чар" }, { "engineering", "инженерное дело" },
        { "herbalism", "травничество" }, { "leatherworking", "кожевничество" }, { "mining", "горное дело" }, { "skinning", "снятие шкур" },
        { "tailoring", "портняжное дело" },
    };
    for (auto const& [e, r] : kWords)
        if (en == e)
            return r;
    return en;
}

// A line a player means as a question: a "?" or a question word first.
bool LooksLikeQuestion(std::string const& message, std::string const& norm)
{
    if (message.find('?') != std::string::npos)
        return true;
    std::string const first = norm.substr(0, norm.find(' '));
    for (char const* w : { "what", "why", "how", "where", "who", "when", "is", "are", "do", "does", "can", "will", "would", "could",
                           "что", "чо", "че", "почему", "зачем", "как", "где", "кто", "когда", "куда", "можно", "есть", "ты" })
        if (first == w)
            return true;
    return false;
}

// Upper-cases the first letter, Latin or Cyrillic (UTF-8).
void UpperFirst(std::string& t)
{
    if (t.empty())
        return;
    unsigned char const c = static_cast<unsigned char>(t[0]);
    if (c < 0x80)
        t[0] = char(std::toupper(c));
    else if (t.size() >= 2 && c == 0xD0 && static_cast<unsigned char>(t[1]) >= 0xB0 && static_cast<unsigned char>(t[1]) <= 0xBF)
        t[1] = char(static_cast<unsigned char>(t[1]) - 0x20);                       // а-п -> А-П
    else if (t.size() >= 2 && c == 0xD1 && static_cast<unsigned char>(t[1]) >= 0x80 && static_cast<unsigned char>(t[1]) <= 0x8F)
    {
        t[0] = char(0xD0);                                                          // р-я -> Р-Я
        t[1] = char(static_cast<unsigned char>(t[1]) + 0x20);
    }
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
    if (!ctx.russian)
        text = FixPronoun(text);
    UpperFirst(text);
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
        case ConvIntent::Duel: return "duel";
        case ConvIntent::WantTrade: return "want_trade";
        case ConvIntent::Thanks: return "thanks";
        case ConvIntent::AreYouBot: return "are_you_bot";
        case ConvIntent::LikeIt: return "like_it";
        case ConvIntent::Help: return "help";
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
    static Pools const again_ru = {
        { "опять?", "слышу", "да да" },
        { "это уже было", "да слышу я", "я тут, что-то еще?", "ага, слушаю" },
        { "ты это уже писал :) что такое?", "я тут! чем помочь?", "все понятно с первого раза, спасибо :)" } };
    static Pools const insistent_ru = {
        { "...", "что надо", "хватит спамить" },
        { "да тут я, тут", "ты это повторяешь, что нужно?", "никуда не ухожу, спрашивай" },
        { "я рядом, просто скажи что нужно :)", "давай поболтаем, о чем хочешь?" } };
    Pools const& p = ctx.russian ? (repeat >= 3 ? insistent_ru : again_ru) : repeat >= 3 ? insistent : again;
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
        if (!LooksLikeQuestion(message, norm))
            return false;
        Pools const& ask = AskBackPools(ctx.russian);
        std::vector<std::string> const& pool = ctx.tone == ConvTone::Rude ? ask.rude : ctx.tone == ConvTone::Polite ? ask.polite : ask.neutral;
        out = ApplyStyle(Pick(pool, Mix(sel, ctx.bot_seed) >> 4, pool.size()), ctx);
        return !out.empty();
    }

    // A stranger's hello keeps the caller's own pools (ike3 "hi %s, do i know you?"); only someone the bot knows is greeted here.
    if (intent == ConvIntent::Greeting && !ctx.knows_sender)
        return false;

    // Mix the per-bot seed in so two bots asked the same thing do not answer alike.
    uint32_t const s = Mix(sel, ctx.bot_seed);

    std::string text;
    if (intent == ConvIntent::Joke)
    {
        std::vector<std::string> const& jokes = ctx.russian ? JokesRu() : Jokes();
        text = jokes[s % jokes.size()];
    }
    else
    {
        int variant = 0;
        switch (intent)
        {
            case ConvIntent::Duel:        variant = int(ctx.duel); break;
            case ConvIntent::GroupInvite: variant = int(ctx.in_group ? GroupAnswer::InGroup : ctx.group); break;
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
        Pools const& pools = PoolsFor(intent, variant, ctx.russian);
        std::vector<std::string> const* pool = &pools.neutral;
        if (ctx.tone == ConvTone::Rude && !pools.rude.empty())
            pool = &pools.rude;
        else if (ctx.tone == ConvTone::Polite && !pools.polite.empty())
            pool = &pools.polite;
        if (pool->empty())
            return false;
        size_t const limit = ctx.length == ConvLength::Terse ? (pool->size() + 1) / 2 : pool->size();
        text = Pick(*pool, s >> 4, limit);

        std::string const zone = ctx.zone.empty() ? (ctx.russian ? "тут" : "around here") : ctx.zone;
        std::string doing = ctx.activity.empty() ? "just hanging around" : ctx.activity;
        std::string cls = ctx.class_name.empty() ? "adventurer" : ctx.class_name;
        std::vector<std::string> profs = ctx.professions;
        if (ctx.russian)
        {
            doing = ToRussian(doing);
            cls = ToRussian(cls);
            for (std::string& pr : profs)
                pr = ToRussian(pr);
        }
        Replace(text, "{zone}", zone);
        Replace(text, "{doing}", doing);
        Replace(text, "{cls}", cls);
        Replace(text, "{lvl}", std::to_string(ctx.level));
        Replace(text, "{quest}", ctx.quest_title);
        Replace(text, "{prof}", ctx.russian && profs.size() > 1 ? profs[0] + " и " + profs[1] : JoinProfessions(profs));
        Replace(text, "{guild}", ctx.guild_name);
        Replace(text, "{self}", ctx.self_name);
        Replace(text, "{sender}", ctx.sender_name);
        if (intent == ConvIntent::Directions)
        {
            if (Destination const* d = FindDestination(norm))
            {
                Replace(text, "{dest}", ctx.russian ? d->ru_name : d->name);
                Replace(text, "{route}", ctx.russian ? d->ru_route : d->route);
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
            text += ", " + Pick(FollowUps(ctx.russian), Mix(s, 8), FollowUps(ctx.russian).size());
        }
    }

    out = ApplyStyle(std::move(text), ctx);
    return !out.empty();
}

} // namespace Playerbot::V2
