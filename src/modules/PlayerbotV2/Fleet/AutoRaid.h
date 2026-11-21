// The raids the module made itself: a party turned into a raid because one more companion did not fit (the sixth member called, a
// `.bot` group add). Only these go back to a party when they fit one again (PlayerbotV2.cpp, BotGroupRules::RevertBotRaidToParty); a raid
// its leader made (the party menu's Convert to Raid) stays one. Discord 2026-11-18 (ZMNC): a player who made a party of bots a raid
// saw it turn back into a party a few seconds later, every time.
// Kept in memory only: after a restart no raid counts as the module's, so none is turned back.
#pragma once

#include "ObjectGuid.h"
#include <mutex>
#include <unordered_set>

namespace Playerbot::V2::Fleet::AutoRaid
{

struct Store
{
    std::mutex mu;
    std::unordered_set<ObjectGuid> groups;
};

inline Store& Get()
{
    static Store s;
    return s;
}

// The module turned this group into a raid.
inline void Mark(ObjectGuid group)
{
    std::lock_guard<std::mutex> lock(Get().mu);
    Get().groups.insert(group);
}

// The module made this raid (not its leader).
inline bool Made(ObjectGuid group)
{
    std::lock_guard<std::mutex> lock(Get().mu);
    return Get().groups.count(group) != 0;
}

// The group is a party again, or gone.
inline void Forget(ObjectGuid group)
{
    std::lock_guard<std::mutex> lock(Get().mu);
    Get().groups.erase(group);
}

} // namespace Playerbot::V2::Fleet::AutoRaid
