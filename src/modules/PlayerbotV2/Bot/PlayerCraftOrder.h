// PlayerCraftOrder - the pure part of "craft <item link or name> [x5]": a player in a bot's group asks that bot to make something with
// the recipes and reagents it has, and gets the result by mail. Parsing, recipe matching and the cast plan live here (no engine types),
// tested in tests/game/ClassicPlayerCraftOrder.cpp. The game side (recipe scan, casts, mail) is Fleet/PlayerCraftOrders.cpp.
//
// The request grammar, the matching order (link, exact name, prefix, substring) and the cap on a plan follow
// Trixx34/ForeverBots (src/server/game/Bots/BotCraftOrder.h, GPL-2.0), re-implemented for this engine.

#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Playerbot::V2::PlayerCraft {

enum class Kind : std::uint8_t { None, List, Craft };

struct Request
{
    Kind type = Kind::None;
    std::uint32_t count = 1;       // items wanted (not casts), 1..max_count
    std::uint32_t item_entry = 0;  // from an item link, else 0
    std::string name;              // lower case free text, empty for a link or a list
};

namespace detail {

// "x5", "5x" or "5": the count, 0 when the word is not one.
inline std::uint32_t CountWord(std::string_view w)
{
    if (w.size() > 1 && (w.front() == 'x' || w.back() == 'x'))
        w = w.front() == 'x' ? w.substr(1) : w.substr(0, w.size() - 1);
    if (w.empty() || w.size() > 5)
        return 0;
    std::uint32_t n = 0;
    for (char c : w)
    {
        if (c < '0' || c > '9')
            return 0;
        n = n * 10u + std::uint32_t(c - '0');
    }
    return n;
}

inline std::string Squeeze(std::string_view s)
{
    std::string out;
    bool gap = false;
    for (char c : s)
    {
        if (c == ' ' || c == '\t')
        {
            gap = !out.empty();
            continue;
        }
        if (gap)
            out.push_back(' ');
        gap = false;
        out.push_back(c);
    }
    return out;
}

} // namespace detail

// `text` is the whole order, starting with the word "craft": "craft list" | "craft |hitem:2996::::|h[Bolt of Linen Cloth]|h x5" |
// "craft linen bandage 5". Case-insensitive. The count is the first or the last word ("x5", "5x" or "5"), clamped to 1..max_count.
// Kind::None when the first word is not "craft" or nothing was asked for.
inline Request ParseRequest(std::string_view text, std::uint32_t max_count)
{
    Request r;
    std::string s;
    s.reserve(text.size());
    for (char c : text)
        s.push_back(c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c);
    s = detail::Squeeze(s);
    if (s.rfind("craft", 0) != 0 || (s.size() > 5 && s[5] != ' '))
        return r;
    s = s.size() > 5 ? s.substr(6) : std::string();
    if (s.empty())
        return r;
    if (s == "list")
    {
        r.type = Kind::List;
        return r;
    }

    // an item link has spaces in its name: take it out first. Shape: [|cffRRGGBB]|hitem:<entry>:...|h[Name]|h[|r]
    if (auto h = s.find("|hitem:"); h != std::string::npos)
    {
        std::size_t i = h + 7;
        std::uint32_t entry = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && entry < 100000000u)
            entry = entry * 10u + std::uint32_t(s[i++] - '0');
        std::size_t from = h;
        if (h >= 10 && s.compare(h - 10, 2, "|c") == 0)
            from = h - 10;
        std::size_t to = s.size();
        if (auto open = s.find("|h", h + 7); open != std::string::npos)         // end of the link target: |h[Name]
            if (auto close = s.find("|h", open + 2); close != std::string::npos)  // end of the visible text
            {
                to = close + 2;
                if (s.compare(to, 2, "|r") == 0)
                    to += 2;
            }
        std::string const rest = detail::Squeeze(s.substr(0, from) + " " + s.substr(to));
        if (entry == 0)
            return r;
        r.type = Kind::Craft;
        r.item_entry = entry;
        std::uint32_t const n = detail::CountWord(rest);
        r.count = std::clamp<std::uint32_t>(n ? n : 1u, 1u, std::max<std::uint32_t>(max_count, 1u));
        return r;
    }

    // words: optional count first or last, the rest is the name
    std::string first, last;
    auto const sp = s.find(' ');
    first = s.substr(0, sp);
    auto const rsp = s.rfind(' ');
    last = rsp == std::string::npos ? s : s.substr(rsp + 1);
    std::string name = s;
    std::uint32_t n = 0;
    if (sp != std::string::npos)
    {
        if ((n = detail::CountWord(first)) != 0)
            name = s.substr(sp + 1);
        else if ((n = detail::CountWord(last)) != 0)
            name = s.substr(0, rsp);
    }
    name = detail::Squeeze(name);
    if (name.empty())
        return r;
    r.type = Kind::Craft;
    r.name = name;
    r.count = std::clamp<std::uint32_t>(n ? n : 1u, 1u, std::max<std::uint32_t>(max_count, 1u));
    return r;
}

struct Option
{
    std::uint32_t spell = 0;
    std::uint32_t item = 0;
    std::uint32_t yield = 1;   // items one cast makes
    std::string name;          // lower case item name
};

// Index of the recipe that answers the request, -1 when none. A link matches by item entry; a name matches exactly, then as a prefix,
// then as a substring (ties: shorter name, then lower spell id).
inline int Match(Request const& request, std::span<Option const> options)
{
    int best = -1;
    int best_rank = 4;
    for (std::size_t i = 0; i < options.size(); ++i)
    {
        Option const& o = options[i];
        int rank = 4;
        if (request.item_entry)
            rank = o.item == request.item_entry ? 0 : 4;
        else if (!request.name.empty())
        {
            if (o.name == request.name) rank = 1;
            else if (o.name.rfind(request.name, 0) == 0) rank = 2;
            else if (o.name.find(request.name) != std::string::npos) rank = 3;
        }
        if (rank == 4)
            continue;
        bool better = best < 0 || rank < best_rank;
        if (!better && rank == best_rank)
        {
            Option const& b = options[std::size_t(best)];
            better = o.name.size() < b.name.size() || (o.name.size() == b.name.size() && o.spell < b.spell);
        }
        if (better)
        {
            best = int(i);
            best_rank = rank;
        }
    }
    return best;
}

struct Facts
{
    std::uint32_t wanted = 0;          // items asked for
    std::uint32_t yield = 1;           // items per cast
    std::uint32_t max_items = 0;       // config cap per order
    std::uint32_t casts_possible = 0;  // casts the bot's reagents allow right now
};

struct Plan
{
    std::uint32_t casts = 0;
    std::uint32_t items = 0;           // casts * yield
    char const* refusal = nullptr;     // "no_reagents" | "too_many" when nothing can be made
    bool trimmed = false;              // fewer than wanted
};

// Casts needed for the wanted items (rounded up), limited by the reagents and by the per order cap.
inline Plan PlanOrder(Facts const& f)
{
    Plan p;
    std::uint32_t const yield = std::max<std::uint32_t>(f.yield, 1u);
    if (f.max_items < yield)
    {
        p.refusal = "too_many";
        return p;
    }
    if (f.casts_possible == 0)
    {
        p.refusal = "no_reagents";
        return p;
    }
    std::uint32_t const wanted = std::min(f.wanted, f.max_items);
    std::uint32_t const need = (wanted + yield - 1u) / yield;
    std::uint32_t const cap = f.max_items / yield;
    p.casts = std::min({ need, f.casts_possible, std::max<std::uint32_t>(cap, 1u) });
    p.items = p.casts * yield;
    p.trimmed = p.items < f.wanted;
    return p;
}

} // namespace Playerbot::V2::PlayerCraft
