// DungeonFinderPlan.h: the choices of the bot dungeon finder (`.bot dungeon`) with no game types: which dungeon a typed word
// names, which dungeons fit a level, the door a dungeon is entered by, and when a forming group is sent in. Header only.
#pragma once

#include "Define.h"
#include "BotListedGroupPlan.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Playerbot::V2
{

struct FinderDungeonInfo
{
    uint32 activity = 0;            // Group Finder activity id x 256, the numbers of Playerbot.Lfg.BotGroups.Activities
    char const* name = "";          // as the 1.60 client names it (GroupFinderActivity.db2)
    char const* keys = "";          // short words players type, space separated
    uint32 entrance = 0;            // areatrigger_teleport id of the dungeon's own door (a wing's door for Scarlet Monastery / Dire Maul)
    uint32 landing = 0;             // world_safe_locs id of the landing inside, for a WoW Forever dungeon: its door is a server
                                    // areatrigger (areatrigger_template_actions, teleport to this landing), not areatrigger_teleport
};

// Every dungeon of the default Playerbot.Lfg.BotGroups.Activities table. The activity ids were read from the client's
// GroupFinderActivity.db2 (813-815 Dire Maul East/West/North, 816 Stratholme Main Gate, 827-829 Scarlet Monastery Armory/
// Cathedral/Library). The entrance is named here because ObjectMgr::GetMapEntranceTrigger returns any one of several doors of
// a map (Scarlet Monastery has four, Dire Maul six, Gnomeregan, Uldaman, Stratholme and Maraudon two or three).
// The WoW Forever dungeons follow (activity ids from the 70938 client's GroupFinderActivity.db2: 894-897, and 1978-1980, 1985 and
// 1986 in its section the extractor decrypted). They are entered by a server areatrigger whose action teleports to the landing named
// here, the same spot a player's own walk through the door lands on. City of Dalaran (896) is left out: it has no way in yet.
inline constexpr std::array<FinderDungeonInfo, 33> kFinderDungeons = {{
    { 203776, "Wailing Caverns",               "wc",                       228 },
    { 204032, "Scholomance",                   "scholo",                  2567 },
    { 204288, "Ragefire Chasm",                "rfc",                     2230 },
    { 204544, "Deadmines",                     "vc dm",                     78 },
    { 204800, "Shadowfang Keep",               "sfk",                      145 },
    { 205056, "Blackfathom Deeps",             "bfd",                      257 },
    { 205312, "Stormwind Stockades",           "stocks stockade",          101 },
    { 205568, "Gnomeregan",                    "gnomer",                   324 },
    { 205824, "Razorfen Kraul",                "rfk",                      244 },
    { 206080, "Scarlet Monastery - Graveyard", "smgy gy graveyard",         45 },
    { 206336, "Razorfen Downs",                "rfd",                      442 },
    { 206592, "Uldaman",                       "ulda uld",                 286 },
    { 206848, "Zul'Farrak",                    "zf",                       924 },
    { 207104, "Maraudon",                      "mara",                    3133 },
    { 207360, "Sunken Temple",                 "st",                       446 },
    { 207616, "Blackrock Depths",              "brd",                     1466 },
    { 207872, "Lower Blackrock Spire",         "lbrs",                    1468 },
    { 208128, "Dire Maul (East)",              "dme dmeast",              3183 },
    { 208384, "Dire Maul (West)",              "dmw dmwest",              3186 },
    { 208640, "Dire Maul (North)",             "dmn dmnorth tribute",     3189 },
    { 208896, "Stratholme (Main Gate)",        "strat",                   2216 },
    { 211712, "Scarlet Monastery - Armory",    "smarm arm armory",         612 },
    { 211968, "Scarlet Monastery - Cathedral", "smcath cath cathedral",    610 },
    { 212224, "Scarlet Monastery - Library",   "smlib lib library",        614 },
    { 214272, "Upper Blackrock Spire",         "ubrs",                    1468 },
    { 228864, "Excavation Site: Wetlands",     "excav excavation dig",       0, 1104104 },
    { 229120, "Ruins of Lordaeron",            "rol lordaeron",              0, 1104100 },
    { 229632, "Hall of Thanes",                "hot thanes",                 0, 1104102 },
    { 506368, "The Shaper's Terrace",          "shapers terrace",            0, 1104128 },
    { 506624, "Blackmaw Hold",                 "blackmaw bmh",               0, 1104126 },
    { 506880, "Alcaz Prison",                  "alcaz",                      0, 1104124 },
    { 508160, "The Drowned City",              "drowned dc",                 0, 1104120 },
    { 508416, "Krol'dok Stronghold",           "kroldok krol",               0, 1104122 },
}};

inline FinderDungeonInfo const* FinderDungeonFor(uint32 activity)
{
    for (FinderDungeonInfo const& d : kFinderDungeons)
        if (d.activity == activity)
            return &d;
    return nullptr;
}

// Lower case letters and digits only: "Zul'Farrak" -> "zulfarrak", "Dire Maul (East)" -> "diremauleast".
inline std::string FinderNormalize(std::string_view text)
{
    std::string out;
    for (char c : text)
    {
        if (c >= 'A' && c <= 'Z')
            out += char(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out += c;
    }
    return out;
}

// What a typed word names among `dungeons` (indices into it). A short word of a dungeon or its whole name wins at once; otherwise
// every dungeon whose name holds the word ("scarlet" -> the four wings, "maul" -> the three Dire Maul wings). Empty = nothing.
inline std::vector<size_t> MatchFinderDungeon(std::vector<ListedActivity> const& dungeons, std::string_view word)
{
    std::string const query = FinderNormalize(word);
    std::vector<size_t> exact, partial;
    if (query.empty())
        return exact;
    for (size_t i = 0; i < dungeons.size(); ++i)
    {
        FinderDungeonInfo const* info = FinderDungeonFor(dungeons[i].activity);
        if (!info)
            continue;
        std::string const name = FinderNormalize(info->name);
        bool key = false;
        std::string_view keys = info->keys;
        while (!keys.empty() && !key)
        {
            size_t const space = keys.find(' ');
            key = FinderNormalize(keys.substr(0, space)) == query;
            keys = space == std::string_view::npos ? std::string_view() : keys.substr(space + 1);
        }
        if (key || name == query)
            exact.push_back(i);
        else if (name.find(query) != std::string::npos)
            partial.push_back(i);
    }
    return exact.empty() ? partial : exact;
}

// The 5-player dungeons for a player of `level`: those whose level range holds it; when none does (below the first dungeon, or a
// gap), the ones nearest to it. Upper Blackrock Spire and other bigger groups are only had by name.
inline std::vector<size_t> FinderDungeonsForLevel(std::vector<ListedActivity> const& dungeons, uint32 level)
{
    std::vector<size_t> fit;
    uint32 best = ~0u;
    std::vector<size_t> nearest;
    for (size_t i = 0; i < dungeons.size(); ++i)
    {
        ListedActivity const& d = dungeons[i];
        if (d.size > 5)
            continue;
        if (level >= d.levelMin && level <= d.levelMax)
        {
            fit.push_back(i);
            continue;
        }
        uint32 const off = level < d.levelMin ? d.levelMin - level : level - d.levelMax;
        if (off < best)
        {
            best = off;
            nearest.clear();
        }
        if (off == best)
            nearest.push_back(i);
    }
    return fit.empty() ? nearest : fit;
}

// "Playerbot.Lfg.Finder.Entrances": "activity:areatrigger,..." overrides of the door. An entry that does not read is skipped.
struct FinderEntrance { uint32 activity = 0, trigger = 0; };

inline std::vector<FinderEntrance> ParseFinderEntrances(std::string_view text)
{
    std::vector<FinderEntrance> out;
    while (!text.empty())
    {
        size_t const comma = text.find(',');
        std::string_view const entry = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
        size_t const colon = entry.find(':');
        FinderEntrance e;
        if (colon == std::string_view::npos || !Detail::ParseUint(entry.substr(0, colon), e.activity) ||
            !Detail::ParseUint(entry.substr(colon + 1), e.trigger) || !e.activity || !e.trigger)
            continue;
        out.push_back(e);
    }
    return out;
}

// The door of a dungeon: the configured one, else the one named above, else 0 (the landing below, else the map's first known entrance).
// A configured door also wins over a Forever dungeon's landing.
inline uint32 FinderEntranceTrigger(uint32 activity, std::vector<FinderEntrance> const& overrides)
{
    for (FinderEntrance const& e : overrides)
        if (e.activity == activity)
            return e.trigger;
    FinderDungeonInfo const* info = FinderDungeonFor(activity);
    return info ? info->entrance : 0;
}

// The landing inside a WoW Forever dungeon (a world_safe_locs id), used when no door of its own is named or configured; else 0.
inline uint32 FinderLandingSafeLoc(uint32 activity, std::vector<FinderEntrance> const& overrides)
{
    if (FinderEntranceTrigger(activity, overrides))
        return 0;
    FinderDungeonInfo const* info = FinderDungeonFor(activity);
    return info ? info->landing : 0;
}

// A queued group waits for its bots to join; it goes in when it is complete, or when the wait is over with at least one bot
// in it (it goes in with what came, the leader is told). Nobody at all after the wait: the queue is given up.
enum class FinderFormAction : uint8 { Wait, Enter, GiveUp };

inline FinderFormAction DecideFinderEnter(uint32 members, uint32 partySize, uint32 msWaited, uint32 formMs)
{
    if (members >= partySize)
        return FinderFormAction::Enter;
    if (msWaited < formMs)
        return FinderFormAction::Wait;
    return members > 1 ? FinderFormAction::Enter : FinderFormAction::GiveUp;
}

} // namespace Playerbot::V2
