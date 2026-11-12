// BotListedGroupPlan.h: the decisions behind the bot-made groups in the Group Finder list. Header only, no game types.
#pragma once

#include "Define.h"
#include "LfgRoleGap.h"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <vector>

namespace Playerbot::V2
{

// A dungeon the bots may list: the Group Finder activity id, its map, and the level range of the bots that form the group.
struct ListedActivity
{
    uint32 activity = 0;
    uint32 map = 0;
    uint8  levelMin = 1, levelMax = 60;
    uint8  size = 5;                // places of the party: 5 for every dungeon, 10 for Upper Blackrock Spire
    bool operator==(ListedActivity const&) const = default;
};

namespace Detail
{
    inline bool ParseUint(std::string_view text, uint32& out)
    {
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
        if (text.empty())
            return false;
        auto const [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
        return ec == std::errc() && end == text.data() + text.size();
    }
}

// "activity:map:levelMin:levelMax[:size],..." An entry that does not read is skipped; levels are clamped to 1-80 and a swapped
// pair is put the right way round; the party size (5 when left out) is held to 5-10.
inline std::vector<ListedActivity> ParseListedActivities(std::string_view text)
{
    std::vector<ListedActivity> out;
    while (!text.empty())
    {
        size_t const comma = text.find(',');
        std::string_view entry = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);

        uint32 field[5] = {};
        size_t n = 0;
        while (n < 5)
        {
            size_t const colon = entry.find(':');
            if (!Detail::ParseUint(entry.substr(0, colon), field[n]))
                break;
            ++n;
            if (colon == std::string_view::npos)
            {
                entry = std::string_view();
                break;
            }
            entry = entry.substr(colon + 1);
        }
        if ((n != 4 && n != 5) || !entry.empty() || !field[0] || !field[1])
            continue;
        uint32 lo = std::clamp<uint32>(field[2], 1, 80), hi = std::clamp<uint32>(field[3], 1, 80);
        if (lo > hi)
            std::swap(lo, hi);
        out.push_back(ListedActivity{ field[0], field[1], uint8(lo), uint8(hi), uint8(n == 5 ? std::clamp<uint32>(field[4], 5, 10) : 5) });
    }
    return out;
}

// The order the dungeons are tried in for a new listed group: those with no listed group of the faction yet come first, so every
// dungeon gets one before any gets a second; each half is shuffled. `listed` holds the activity id of each group listed for the
// faction; `rand(n)` returns 0..n-1. The result is indices into `all`.
template <class Rand>
inline std::vector<size_t> ListedActivityOrder(std::vector<ListedActivity> const& all, std::vector<uint32> const& listed, Rand&& rand)
{
    std::vector<size_t> fresh, taken;
    for (size_t i = 0; i < all.size(); ++i)
        (std::ranges::find(listed, all[i].activity) == listed.end() ? fresh : taken).push_back(i);
    for (std::vector<size_t>* half : { &fresh, &taken })
        for (size_t i = half->size(); i > 1; --i)
            std::swap((*half)[i - 1], (*half)[size_t(rand(uint32(i)))]);
    fresh.insert(fresh.end(), taken.begin(), taken.end());
    return fresh;
}

// The party size of a listed group from a player limit, held to 5-10 (raid-size listings come later); the listed dungeons carry their own size.
constexpr uint32 kListedGroupMaxSize = 10;
inline uint32 ListedGroupSize(uint32 mapMaxPlayers)
{
    return std::clamp<uint32>(mapMaxPlayers, 5, kListedGroupMaxSize);
}

// How long a listed group stays up: `roll` (0..1) picks a point between the minimum and maximum lifetime.
inline uint32 ListedGroupLifetimeMs(uint32 minSeconds, uint32 maxSeconds, float roll)
{
    if (maxSeconds < minSeconds)
        maxSeconds = minSeconds;
    float const r = std::clamp(roll, 0.0f, 1.0f);
    return uint32((float(minSeconds) + r * float(maxSeconds - minSeconds)) * 1000.0f);
}

// What a listed group does at a check. A human in the group takes the lead and the listing ends (Handover); a group that is
// gone or shrank below two is Dropped; one past its lifetime Expires.
enum class ListedGroupAction : uint8 { Keep, Handover, Expire, Drop };

inline ListedGroupAction DecideListedGroup(bool groupExists, uint32 members, bool humanInGroup, uint32 ageMs, uint32 lifetimeMs)
{
    if (!groupExists || members < 2)
        return ListedGroupAction::Drop;
    if (humanInGroup)
        return ListedGroupAction::Handover;
    return ageMs >= lifetimeMs ? ListedGroupAction::Expire : ListedGroupAction::Keep;
}

// How many groups the list holds at a time of day: `hour` is the local hour 0-23. Busiest in the evening, quietest in the small hours.
inline float ListedGroupDaylight(uint32 hour)
{
    constexpr float byHour[24] = {
        0.55f, 0.40f, 0.30f, 0.25f, 0.25f, 0.25f, 0.30f, 0.40f, 0.50f, 0.55f, 0.60f, 0.65f,
        0.70f, 0.70f, 0.70f, 0.75f, 0.80f, 0.90f, 1.00f, 1.00f, 1.00f, 1.00f, 0.90f, 0.70f };
    return byHour[hour % 24];
}

// The number of groups wanted for one faction: `perFaction` at the busiest hour, never fewer than one while a player is online.
inline uint32 ListedGroupTarget(uint32 perFaction, float daylight)
{
    if (!perFaction)
        return 0;
    uint32 const wanted = uint32(float(perFaction) * daylight + 0.5f);
    return wanted ? wanted : 1;
}

// A listed group is rarely full: it lacks one or two of a scarce role (healers most often) and some damage places. `rollRole`,
// `rollCount` and `rollOpen` are rolls of 0..1. `have` is the role mix of the members that are there; `members` their number.
struct ListedGroupShape
{
    LfgRoleGap have{};
    uint32 members = 0;
    LfgRoleGap missing{};
};

inline ListedGroupShape PlanListedGroup(uint32 size, float rollRole, float rollCount, float rollOpen)
{
    LfgRoleGap const mix = LfgRoleMix(size);
    ListedGroupShape shape;
    shape.have = mix;
    // the role the group is looking for: healer 45%, tank 35%, damage 20%
    uint8 const count = uint8(rollCount < 0.4f ? 2 : 1);
    if (rollRole < 0.45f)
    {
        shape.missing.healer = std::min<uint8>(count, mix.healer);
        shape.have.healer = uint8(mix.healer - shape.missing.healer);
    }
    else if (rollRole < 0.80f)
    {
        shape.missing.tank = std::min<uint8>(count, mix.tank);
        shape.have.tank = uint8(mix.tank - shape.missing.tank);
    }
    else
    {
        shape.missing.dps = std::min<uint8>(count, mix.dps);
        shape.have.dps = uint8(mix.dps - shape.missing.dps);
    }
    // up to a third of the places stay open as damage, but at least half of the places (3 of 5) stay filled
    uint32 const minMembers = size / 2 + 1;
    uint32 const extraOpen = std::min<uint32>(uint32(std::clamp(rollOpen, 0.0f, 0.999f) * float(size / 3 + 1)), shape.have.dps);
    shape.have.dps = uint8(shape.have.dps - extraOpen);
    shape.missing.dps = uint8(shape.missing.dps + extraOpen);
    while (shape.have.Total() < minMembers && shape.missing.dps)
    {
        ++shape.have.dps;
        --shape.missing.dps;
    }
    shape.members = shape.have.Total();
    return shape;
}

} // namespace Playerbot::V2
