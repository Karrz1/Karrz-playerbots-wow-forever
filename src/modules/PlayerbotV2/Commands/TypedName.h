// TypedName - the character a player means when they type a name into a bot command (`.playerbot summon`, `.bot dismiss`).
// Classic characters carry a surname that character select and the party frames show after the name ("Kar kar"), so players
// type both. Pure; tested in tests/game/ClassicTypedName.cpp.
#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace Playerbot::V2::TypedName {

inline std::string Trim(std::string_view s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return std::string(s.substr(a, b - a));
}

// Lower case with the blanks taken out: "Kar  Kar" and "karkar" compare equal.
inline std::string Folded(std::string_view s)
{
    std::string out;
    for (char c : s)
        if (c != ' ' && c != '\t')
            out.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// The name to look up for `typed`: one word as it is; "Name Surname" the first word when the rest is the surname of the
// character with that name (`surname_of(first word)`, empty for no such character or no surname); anything else the words
// joined ("First Last" = "FirstLast"). Letter case is the caller's (normalizePlayerName).
template <class SurnameOf>
std::string Pick(std::string_view typed, SurnameOf&& surname_of)
{
    std::string const text = Trim(typed);
    size_t const space = text.find_first_of(" \t");
    if (space == std::string::npos)
        return text;
    std::string const first = text.substr(0, space);
    std::string const surname = surname_of(first);
    if (!surname.empty() && Folded(surname) == Folded(std::string_view(text).substr(space)))
        return first;
    std::string joined;
    for (char c : text)
        if (c != ' ' && c != '\t')
            joined.push_back(c);
    return joined;
}

} // namespace Playerbot::V2::TypedName
