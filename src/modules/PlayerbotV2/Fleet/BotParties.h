// BotParties - small parties of world bots (Playerbot.World.Parties.*): two or three bots of the world, of one faction and
// near in level, who meet nearby, travel and fight together for a while and then go their own ways, the way strangers team
// up for a few kills. No game group is made: the members stay free world bots (spawner, despawn and the other world rules
// see them as before); the party is this table, and two idle rules read it (Bot/States/Rules/AmbientRules.cpp idle:party_*):
// a member whose mate is fighting a creature joins in, a follower who has fallen behind his leader catches up, and a follower
// leaves the pulling and the grind spot to the leader.
//
// Pure part: the forming and dissolving pass and the board. The world thread runs Step once a second
// (WorldBotSpawner::PublishParties) and publishes each member's view of its mates. Tested in tests/game/ClassicBotParties.cpp.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Playerbot::V2::Parties
{

// One free world bot as the world thread sees it.
struct Bot
{
    std::uint64_t guid = 0;
    std::uint32_t map = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    bool alliance = true;
    std::uint8_t level = 1;
    bool alive = true;
    bool in_combat = false;
    std::uint64_t target = 0;   // guid counter of the creature it is fighting, 0 = none
    bool can_join = true;       // may start a party now (not in a city, not mounted, not travelling far)
};

struct Settings
{
    std::uint32_t pct = 30;          // share of the eligible bots that are in a party
    std::uint32_t max_size = 3;      // 2 or 3
    float meet_radius = 60.f;        // two bots this close may team up
    float break_radius = 160.f;      // a member this far from its leader leaves the party
    std::uint8_t level_gap = 3;
    std::uint32_t life_ms = 15u * 60u * 1000u;   // a party lasts this long, give or take a third
};

struct Party
{
    std::uint64_t leader = 0;
    std::vector<std::uint64_t> followers;
    std::uint32_t until_ms = 0;
};

// A member's view of one mate, published for the rules.
struct Mate
{
    std::uint64_t guid = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    bool in_combat = false;
    std::uint64_t target = 0;
};

struct Link
{
    std::uint64_t leader = 0;   // the party's leader (may be the member itself)
    std::uint8_t slot = 0;      // 0 = leader, 1.. = follower position (its place beside the leader)
    std::uint32_t map = 0;
    std::vector<Mate> mates;    // every other member
};

inline float Dist(Bot const& a, Bot const& b) { return std::hypot(a.x - b.x, a.y - b.y); }

class Former
{
public:
    // One pass: dissolve the parties that ended (expired, a member gone, dead or far off), then form new ones until about
    // `pct` of the eligible bots are in a party. Returns each member's link.
    std::unordered_map<std::uint64_t, Link> Step(std::vector<Bot> const& bots, Settings const& st, std::uint32_t now_ms)
    {
        std::unordered_map<std::uint64_t, Bot const*> by_guid;
        by_guid.reserve(bots.size());
        for (Bot const& b : bots)
            by_guid[b.guid] = &b;

        // Dissolve.
        std::vector<Party> kept;
        for (Party& p : parties_)
        {
            auto const lit = by_guid.find(p.leader);
            if (lit == by_guid.end() || !lit->second->alive || std::int32_t(now_ms - p.until_ms) >= 0)
            {
                ++dissolved_;
                continue;
            }
            Bot const& lead = *lit->second;
            p.followers.erase(std::remove_if(p.followers.begin(), p.followers.end(), [&](std::uint64_t g)
            {
                auto const it = by_guid.find(g);
                return it == by_guid.end() || !it->second->alive || it->second->map != lead.map || Dist(*it->second, lead) > st.break_radius;
            }), p.followers.end());
            if (p.followers.empty())
            {
                ++dissolved_;
                continue;
            }
            kept.push_back(std::move(p));
        }
        parties_ = std::move(kept);

        std::unordered_set<std::uint64_t> taken;
        for (Party const& p : parties_)
        {
            taken.insert(p.leader);
            taken.insert(p.followers.begin(), p.followers.end());
        }

        // Form.
        std::uint32_t eligible = 0;
        for (Bot const& b : bots)
            if (b.alive && (b.can_join || taken.count(b.guid)))
                ++eligible;
        std::uint32_t const want = eligible * std::min<std::uint32_t>(st.pct, 100u) / 100u;
        std::uint32_t const max_size = std::clamp<std::uint32_t>(st.max_size, 2u, 3u);
        // The candidates in a stirred order, so the same bots are not always the first to team up.
        std::vector<Bot const*> order;
        for (Bot const& b : bots)
            if (b.alive && b.can_join && !b.in_combat && !taken.count(b.guid))
                order.push_back(&b);
        for (std::size_t i = order.size(); i > 1; --i)
            std::swap(order[i - 1], order[Next() % i]);

        for (Bot const* a : order)
        {
            if (std::uint32_t(taken.size()) + 2u > want) break;
            if (taken.count(a->guid)) continue;
            Bot const* mate = Nearest(*a, order, taken, st, nullptr);
            if (!mate) continue;
            Party p;
            p.leader = a->level >= mate->level ? a->guid : mate->guid;   // the higher level leads
            p.followers.push_back(p.leader == a->guid ? mate->guid : a->guid);
            taken.insert(a->guid);
            taken.insert(mate->guid);
            if (max_size >= 3 && std::uint32_t(taken.size()) + 1u <= want && Next() % 2u == 0u)
                if (Bot const* third = Nearest(*a, order, taken, st, mate))
                {
                    p.followers.push_back(third->guid);
                    taken.insert(third->guid);
                }
            std::uint32_t const spread = st.life_ms / 3u;
            p.until_ms = now_ms + st.life_ms - spread + (spread ? std::uint32_t(Next() % (2u * spread + 1u)) : 0u);
            if (!p.until_ms) p.until_ms = 1u;
            parties_.push_back(std::move(p));
            ++formed_;
        }

        // Links.
        std::unordered_map<std::uint64_t, Link> out;
        for (Party const& p : parties_)
        {
            std::vector<std::uint64_t> members{ p.leader };
            members.insert(members.end(), p.followers.begin(), p.followers.end());
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                Link l;
                l.leader = p.leader;
                l.slot = std::uint8_t(i);
                l.map = by_guid[p.leader]->map;
                for (std::size_t j = 0; j < members.size(); ++j)
                {
                    if (j == i) continue;
                    Bot const& m = *by_guid[members[j]];
                    l.mates.push_back({ m.guid, m.x, m.y, m.z, m.in_combat, m.target });
                }
                out[members[i]] = std::move(l);
            }
        }
        return out;
    }

    std::vector<Party> const& parties() const { return parties_; }
    std::uint32_t formed() const { return formed_; }
    std::uint32_t dissolved() const { return dissolved_; }
    void Seed(std::uint32_t s) { rng_ = s ? s : 1u; }

private:
    static bool Fits(Bot const& a, Bot const& b, Settings const& st)
    {
        return a.map == b.map && a.alliance == b.alliance && Dist(a, b) <= st.meet_radius
            && (a.level > b.level ? a.level - b.level : b.level - a.level) <= st.level_gap;
    }
    Bot const* Nearest(Bot const& a, std::vector<Bot const*> const& order, std::unordered_set<std::uint64_t> const& taken,
                       Settings const& st, Bot const* also_fits) const
    {
        Bot const* best = nullptr;
        float best_d = st.meet_radius + 1.f;
        for (Bot const* b : order)
        {
            if (b == &a || taken.count(b->guid) || !Fits(a, *b, st)) continue;
            if (also_fits && !Fits(*also_fits, *b, st)) continue;
            float const d = Dist(a, *b);
            if (d < best_d) { best_d = d; best = b; }
        }
        return best;
    }
    std::uint32_t Next()
    {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return rng_;
    }

    std::vector<Party> parties_;
    std::uint32_t rng_ = 0x9e3779b9u;
    std::uint32_t formed_ = 0, dissolved_ = 0;
};

class Board
{
public:
    static Board& Get()
    {
        static Board b;
        return b;
    }
    void Publish(std::unordered_map<std::uint64_t, Link> links)
    {
        auto next = std::make_shared<std::unordered_map<std::uint64_t, Link> const>(std::move(links));
        std::lock_guard<std::mutex> g(mtx_);
        cur_ = std::move(next);
    }
    std::shared_ptr<std::unordered_map<std::uint64_t, Link> const> Current() const
    {
        std::lock_guard<std::mutex> g(mtx_);
        return cur_;
    }
    std::atomic<std::uint32_t> assists{0}, catchups{0};

private:
    mutable std::mutex mtx_;
    std::shared_ptr<std::unordered_map<std::uint64_t, Link> const> cur_ = std::make_shared<std::unordered_map<std::uint64_t, Link> const>();
};

} // namespace Playerbot::V2::Parties
