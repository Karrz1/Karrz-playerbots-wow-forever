// PartyLink - the rule-side view of Fleet/BotParties.h: is this bot of the world in a small party of world bots, and who are its mates?
#pragma once

#include "Bot/BotSnapshotView.h"
#include "Fleet/BotParties.h"
#include "Services.h"
#include "Util/ConfigReader.h"

namespace Playerbot::V2::Parties
{

// The party link of this bot, or null. `keep` holds the published table alive while the link is read.
inline Link const* LinkOf(BotSnapshotView const& s, std::shared_ptr<std::unordered_map<std::uint64_t, Link> const>& keep)
{
    if (!Services::Config().parties_enable() || !s.is_free_world_bot())
        return nullptr;
    keep = Board::Get().Current();
    if (!keep || keep->empty())
        return nullptr;
    auto const it = keep->find(s.guid().GetCounter());
    return it != keep->end() && it->second.map == s.map_id() ? &it->second : nullptr;
}

// A follower leaves the pulling and the choice of grind spot to its leader.
inline bool Following(BotSnapshotView const& s)
{
    std::shared_ptr<std::unordered_map<std::uint64_t, Link> const> keep;
    Link const* l = LinkOf(s, keep);
    return l && l->slot > 0;
}

} // namespace Playerbot::V2::Parties
