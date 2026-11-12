// CcDeny.h - the owner's crowd-control orders: "no fear", "@warlock no fear", "@mages no sheep", "yes sheep", "off cc", "on cc".
//
// An order names a kind of crowd control (or all of it with "cc") and, optionally, a class. The owner's choice is kept per account
// and class (OwnerRegistry::CcDenied) and mirrored into each bot's snapshot (BotSnapshot::cc_deny); BotIntentEmitter drops a cast of
// a denied kind and MarkCc::PickOption skips it, so a rotation falls through to its next rule and no mob is held for a cast that
// never comes. Stuns, roots, snares and interrupts are not crowd control here: they stay part of the rotations.
// Pure text and spell ids: no Player, no SpellMgr (the emitter adds the mechanic fallback for spells missing from the table).
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Playerbot::CcDeny {

enum Kind : uint16_t
{
    Fear      = 1u << 0,   // Fear, Howl of Terror, Death Coil, Psychic Scream, Intimidating Shout, Scare Beast, Turn Undead
    Polymorph = 1u << 1,
    Banish    = 1u << 2,
    Shackle   = 1u << 3,
    Sap       = 1u << 4,
    Sleep     = 1u << 5,   // Hibernate, Wyvern Sting
    Trap      = 1u << 6,   // Freezing Trap
    Charm     = 1u << 7,   // Seduction, Enslave Demon, Mind Control
    All       = 0xFF,
};

// The kind a crowd-control spell belongs to (every rank listed: the 70532 data does not chain every one), 0 = not crowd control.
inline uint16_t KindOfSpell(uint32_t spell)
{
    switch (spell)
    {
        case 5782: case 6213: case 6215:                  // Fear
        case 5484: case 17928:                            // Howl of Terror
        case 6789: case 17925: case 17926:                // Death Coil (the mob runs in horror)
        case 8122: case 8124: case 10888: case 10890:     // Psychic Scream
        case 5246:                                        // Intimidating Shout
        case 1513: case 14326: case 14327:                // Scare Beast
        case 2878: case 5627: case 10326:                 // Turn Undead
            return Fear;
        case 118: case 12824: case 12825: case 12826: case 28271: case 28272:
            return Polymorph;
        case 710: case 18647:
            return Banish;
        case 9484: case 9485: case 10955:
            return Shackle;
        case 6770: case 2070: case 11297:
            return Sap;
        case 2637: case 18657: case 18658:                // Hibernate
        case 19386: case 24132: case 24133:               // Wyvern Sting
            return Sleep;
        case 1499: case 14310: case 14311:
            return Trap;
        case 6358:                                        // Seduction (Succubus)
        case 1098: case 11725: case 11726:                // Enslave Demon
        case 605: case 10911: case 10912:                 // Mind Control
            return Charm;
        default:
            return 0;
    }
}

// "fear", "sheep", "cc" (= every kind) ... -> the kind, 0 = not a crowd-control word.
inline uint16_t KindOfWord(std::string_view w)
{
    static constexpr std::pair<std::string_view, uint16_t> kWords[] = {
        { "fear", Fear }, { "fears", Fear }, { "howl", Fear }, { "scream", Fear }, { "horror", Fear }, { "scare", Fear },
        { "sheep", Polymorph }, { "sheeps", Polymorph }, { "poly", Polymorph }, { "polys", Polymorph }, { "polymorph", Polymorph },
        { "morph", Polymorph }, { "pig", Polymorph }, { "turtle", Polymorph },
        { "banish", Banish }, { "banishes", Banish },
        { "shackle", Shackle }, { "shackles", Shackle },
        { "sap", Sap }, { "saps", Sap },
        { "sleep", Sleep }, { "hibernate", Sleep }, { "wyvern", Sleep }, { "wyvern sting", Sleep },
        { "trap", Trap }, { "traps", Trap }, { "freezing trap", Trap },
        { "charm", Charm }, { "seduce", Charm }, { "seduction", Charm }, { "enslave", Charm }, { "mc", Charm }, { "mind control", Charm },
        { "cc", All },
    };
    for (auto const& [word, kind] : kWords)
        if (w == word)
            return kind;
    return 0;
}

// The kinds in `mask` as words ("fear, polymorph"), "all crowd control" for every kind, "nothing" for none.
inline std::string Describe(uint16_t mask)
{
    if ((mask & All) == All)
        return "all crowd control";
    static constexpr std::pair<uint16_t, char const*> kNames[] = {
        { Fear, "fear" }, { Polymorph, "polymorph" }, { Banish, "banish" }, { Shackle, "shackle" },
        { Sap, "sap" }, { Sleep, "sleep" }, { Trap, "freezing trap" }, { Charm, "charm" },
    };
    std::string out;
    for (auto const& [bit, name] : kNames)
        if (mask & bit)
            out += (out.empty() ? "" : ", ") + std::string(name);
    return out.empty() ? "nothing" : out;
}

// Class ids (ChrClasses: CLASS_WARRIOR 1 ... CLASS_EVOKER 13) by the name a player types, singular or plural, 0 = none.
inline uint8_t ClassOfWord(std::string_view w)
{
    static constexpr std::pair<std::string_view, uint8_t> kClasses[] = {
        { "warrior", 1 }, { "paladin", 2 }, { "pally", 2 }, { "pallies", 2 }, { "hunter", 3 }, { "rogue", 4 }, { "priest", 5 },
        { "deathknight", 6 }, { "dk", 6 }, { "shaman", 7 }, { "mage", 8 }, { "warlock", 9 }, { "lock", 9 }, { "monk", 10 },
        { "druid", 11 }, { "demonhunter", 12 }, { "dh", 12 }, { "evoker", 13 },
    };
    for (auto const& [name, cls] : kClasses)
        if (w == name || (w.size() == name.size() + 1 && w.back() == 's' && w.substr(0, name.size()) == name))
            return cls;
    return 0;
}

// The plural a reply names a class by ("warlocks"); "every class" for 0.
inline std::string ClassPlural(uint8_t cls)
{
    static constexpr char const* kNames[] = { "every class", "warriors", "paladins", "hunters", "rogues", "priests", "death knights",
        "shamans", "mages", "warlocks", "monks", "druids", "demon hunters", "evokers" };
    return cls < std::size(kNames) ? kNames[cls] : "that class";
}

// A parsed order: deny (true) or allow `kinds` for class `cls` (0 = every class).
struct Order
{
    uint8_t  cls = 0;
    bool     deny = true;
    uint16_t kinds = 0;
    bool operator==(Order const&) const = default;
};

// Reads one chat line as a crowd-control order, else nullopt (any other order or chatter). Accepted, case-insensitive:
//   [@<class> | <class>: | <class>] (no | off | deny | dont | don't) <kind>     e.g. "@warlock no fear", "mages no sheep", "off cc"
//   [...] (yes | on | allow) <kind>                                          e.g. "@mage yes sheep", "on cc"
//   [...] <kind> (off | no | on | yes)                                       e.g. "cc off", "fear on"
// <kind> is a word of KindOfWord ("freezing trap", "mind control" as two words). The panel's "crowdcontrol on|off" is not an order
// here: it is the marked crowd-control switch alone (OwnerRegistry::SetCcEnabled).
inline std::optional<Order> ParseOrder(std::string_view line)
{
    std::string text(line);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::vector<std::string> words;
    for (size_t i = 0; i < text.size();)
    {
        size_t const b = text.find_first_not_of(" \t,", i);
        if (b == std::string::npos) break;
        size_t const e = text.find_first_of(" \t,", b);
        words.push_back(text.substr(b, e == std::string::npos ? std::string::npos : e - b));
        i = e == std::string::npos ? text.size() : e;
    }
    if (words.size() < 2)
        return std::nullopt;
    Order out;
    // The class address: "@warlock", "warlock:", "warlocks".
    {
        std::string first = words.front();
        bool const at = first.front() == '@';
        if (at) first.erase(0, 1);
        if (!first.empty() && first.back() == ':') first.pop_back();
        if (uint8_t const cls = ClassOfWord(first))
        {
            out.cls = cls;
            words.erase(words.begin());
        }
        else if (at)
            return std::nullopt;   // "@skull no ..." and other selectors are not class orders
    }
    if (words.size() < 2 || words.size() > 3)
        return std::nullopt;
    auto const verb = [](std::string const& w) -> int   // 1 deny, 2 allow, 0 neither
    {
        if (w == "no" || w == "off" || w == "deny" || w == "dont" || w == "don't") return 1;
        if (w == "yes" || w == "on" || w == "allow") return 2;
        return 0;
    };
    std::string kind_text;
    int v = verb(words.front());
    if (v)
        kind_text = words.size() == 3 ? words[1] + " " + words[2] : words[1];
    else if ((v = verb(words.back())))
        kind_text = words.size() == 3 ? words[0] + " " + words[1] : words[0];
    else
        return std::nullopt;
    out.kinds = KindOfWord(kind_text);
    if (!out.kinds)
        return std::nullopt;
    out.deny = v == 1;
    return out;
}

}  // namespace Playerbot::CcDeny
