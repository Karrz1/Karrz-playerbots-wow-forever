#include "ChannelChat.h"

#include "BotAI.h"
#include "BotChatReactor.h"
#include "BotIntent.h"
#include "BotPersonality.h"
#include "BotRegistry.h"
#include "BotTextMgr.h"
#include "BotTrade.h"
#include "BotTradeShop.h"
#include "ChannelTalk.h"
#include "ClassTables.h"
#include "../Api/ClassicSpec.h"
#include "../Fleet/BotPopulationManager.h"
#include "../LfgBotFill.h"
#include "../Services.h"
#include "../Threading/IntentQueue.h"
#include "../Util/ConfigReader.h"

#include "Channel.h"
#include "DB2Stores.h"
#include "GameTime.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Playerbot::V2::ChannelChat
{

namespace
{

namespace CT = ChannelTalk;

constexpr uint32 kGeneral = 1;
constexpr uint32 kTrade = 2;
constexpr uint32 kSenderCooldownMs = 20000;    // one answered line per player per 20 s
constexpr uint32 kChannelCooldownMs = 12000;   // one rule answer (question / greeting) per channel and zone per 12 s
constexpr uint32 kLfgCallGapMs = 20000;        // bots' own LFG calls: one per faction per 20 s

std::mutex g_mtx;
std::unordered_map<uint64, uint32> g_sender_ms;
std::unordered_map<uint64, uint32> g_channel_ms;
std::array<uint32, 2> g_lfg_call_ms{};
CT::RecentLines g_recent_calls;
AnswerHook g_hook = nullptr;

bool Elapsed(uint32 now, uint32 then, uint32 gap) { return then == 0 || now - then >= gap; }

template <class Map>
void Prune(Map& m, uint32 now, uint32 keep)
{
    if (m.size() < 512)
        return;
    for (auto it = m.begin(); it != m.end();)
        it = (now - it->second > keep) ? m.erase(it) : std::next(it);
}

enum class Reach : uint8 { None, Zone, City, Faction };

Reach ReachOf(Channel const* ch, uint32 lfg_id)
{
    uint32 const id = ch->GetChannelId();
    if (!id)
        return Reach::None;   // a custom channel
    if (id == kGeneral)
        return Reach::Zone;
    if (id == kTrade)
        return Reach::City;
    if (id == lfg_id || ch->IsLFG())
        return Reach::Faction;
    return Reach::None;
}

bool InCity(Player const* p)
{
    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(p->GetZoneId());
    return zone && zone->GetFlags().HasFlag(AreaFlags::AllowTradeChannel);
}

uint8 RoleBitOf(Player const* p)
{
    uint8 const cls = p->GetClass();
    uint16 const spec = uint16(::Playerbot::Classic::EffectiveSpecId(p));
    if (IsTankSpec(cls, spec)) return CT::kTank;
    if (IsHealerSpec(cls, spec)) return CT::kHealer;
    return CT::kDps;
}

struct Cand
{
    Player* bot;
    BotId id;
    BotAI* ai;
};

// The world bots that read this channel line: same faction, free (no owner, no group), awake and not busy, in the channel's reach.
std::vector<Cand> Readers(Player* sender, Reach reach)
{
    std::vector<BotId> ids;
    Services::Registry().for_each([&](BotId id, BotRegistryEntry const&) { ids.push_back(id); });
    std::vector<Cand> out;
    auto& pop = Services::Population();
    for (BotId id : ids)
    {
        Player* p = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(uint64(id)));
        if (!p || p == sender || !p->IsInWorld() || !p->IsAlive() || p->IsInCombat() || p->GetGroup() || p->InBattleground()
            || p->InArena() || p->IsBeingTeleported() || !p->GetSocial())
            continue;
        if (p->GetTeam() != sender->GetTeam())
            continue;
        if (Map const* m = p->GetMap(); !m || m->Instanceable())
            continue;
        if (!pop.IsAmbientBot(id, p))
            continue;
        if (reach == Reach::Zone && p->GetZoneId() != sender->GetZoneId())
            continue;
        if (reach == Reach::City && !InCity(p))
            continue;
        BotAI* ai = Services::Registry().ai(id);
        if (!ai || ai->personality().verbosity == Verbosity::Silent)
            continue;
        out.push_back({ p, id, ai });
    }
    // A different order each time, so the same bot is not always the one who answers.
    for (std::size_t i = out.size(); i > 1; --i)
        std::swap(out[i - 1], out[urand(0, uint32(i - 1))]);
    return out;
}

void Whisper(BotId bot, std::string const& to, std::string text, uint32 when_ms)
{
    Intent it{};
    it.bot_id = bot;
    it.body = ChatIntent{ WhisperIntent{ to, std::move(text) } };
    it.defer_until_ms = when_ms;
    Services::Intents(bot).push(std::move(it));
}

bool LevelOk(Player const* bot, CT::Ask const& a, uint32 player_level)
{
    if (a.dungeon != CT::kNoDungeon)
        return CT::LevelFits(a.dungeon, bot->GetLevel(), 1);
    int const d = int(bot->GetLevel()) - int(player_level);
    return d >= -3 && d <= 3;
}

// LFM: one bot per wanted role (two at most) whispers to be invited.
void AnswerLfm(Player* sender, CT::Ask const& a, std::vector<Cand> const& readers, uint32 now)
{
    uint32 offered = 0, delay = 3000u + urand(0, 6000);
    for (uint8 bit : { uint8(CT::kTank), uint8(CT::kHealer), uint8(CT::kDps) })
    {
        if (!(a.roles & bit) || offered >= 2)
            continue;
        for (Cand const& c : readers)
        {
            if (RoleBitOf(c.bot) != bit || !LevelOk(c.bot, a, sender->GetLevel()))
                continue;
            std::string line = CT::OfferLine(bit, a.dungeon, c.bot->GetLevel(), urand(0, 0xFFFF));
            TC_LOG_INFO("playerbot.v2", "[channel] lfm {} -> {}: {}", c.bot->GetName(), sender->GetName(), line);
            Whisper(c.id, sender->GetName(), std::move(line), now + delay);
            delay += 2000u + urand(0, 6000);
            ++offered;
            break;
        }
    }
}

// LFG: a listed bot group with a place for him first; else a free bot of his level offers to come along.
void AnswerLfg(Player* sender, CT::Ask const& a, std::vector<Cand> const& readers, uint32 now)
{
    uint32 const map = a.dungeon != CT::kNoDungeon ? CT::kDungeons[a.dungeon].map : 0u;
    if (Player* member = LfgBotGroupMemberFor(sender, map))
    {
        int dungeon = a.dungeon;
        if (dungeon == CT::kNoDungeon)
            for (std::size_t i = 0; i < CT::kDungeonCount; ++i)
                if (CT::LevelFits(int(i), sender->GetLevel())) { dungeon = int(i); break; }
        std::string line = CT::GroupSpotLine(dungeon, urand(0, 0xFFFF));
        TC_LOG_INFO("playerbot.v2", "[channel] lfg group {} -> {}: {}", member->GetName(), sender->GetName(), line);
        Whisper(BotId(member->GetGUID().GetCounter()), sender->GetName(), std::move(line), now + 4000u + urand(0, 8000));
        return;
    }
    if (urand(0, 99) >= 60)
        return;
    for (Cand const& c : readers)
    {
        uint8 const bit = RoleBitOf(c.bot);
        if ((a.roles & bit) || !LevelOk(c.bot, a, sender->GetLevel()))
            continue;   // a second player of his own role is no help to him
        std::string line = CT::OfferLine(bit, a.dungeon, c.bot->GetLevel(), urand(0, 0xFFFF));
        TC_LOG_INFO("playerbot.v2", "[channel] lfg {} -> {}: {}", c.bot->GetName(), sender->GetName(), line);
        Whisper(c.id, sender->GetName(), std::move(line), now + 5000u + urand(0, 10000));
        return;
    }
}

// WTB: the first bot within reach that sells it whispers the price; "buy 1" and a trade window then run the usual trade talk.
void AnswerWtb(Player* sender, CT::Ask const& a, std::vector<Cand> const& readers, uint32 now)
{
    uint32 checked = 0;
    for (Cand const& c : readers)
    {
        if (++checked > 24)
            break;
        for (TradeShop::Offer const& o : TradeShop::BuildOffers(c.bot, 30))
        {
            bool const match = a.item_entry ? o.entry == a.item_entry
                                            : CT::Lower(o.name).find(a.item_name) != std::string::npos;
            if (!match)
                continue;
            TradeShop::StoreList(c.bot, sender, { o });
            std::string line = "i have " + o.name + (o.count > 1 ? " x" + std::to_string(o.count) : std::string()) + " for "
                + BotTrade::FormatMoney(o.price) + ", whisper me \"buy 1\" and open a trade";
            TC_LOG_INFO("playerbot.v2", "[channel] wtb {} -> {}: {}", c.bot->GetName(), sender->GetName(), line);
            Whisper(c.id, sender->GetName(), std::move(line), now + 6000u + urand(0, 12000));
            return;
        }
    }
}

bool TakeChannelSlot(uint64 key, uint32 now)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_channel_ms.find(key);
    if (it != g_channel_ms.end() && !Elapsed(now, it->second, kChannelCooldownMs))
        return false;
    Prune(g_channel_ms, now, kChannelCooldownMs * 4);
    g_channel_ms[key] = now ? now : 1u;
    return true;
}

} // namespace

void SetAnswerHook(AnswerHook hook)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    g_hook = hook;
}

void Post(BotId bot, uint32 channel_id, std::string text, uint32 when_ms)
{
    if (text.empty() || channel_id == 0 || channel_id > 255)
        return;
    Intent it{};
    it.bot_id = bot;
    it.body = ChatIntent{ ChannelChatIntent{ uint8(channel_id), std::move(text) } };
    it.defer_until_ms = when_ms;
    Services::Intents(bot).push(std::move(it));
}

uint32 LfgChannelId()
{
    static uint32 const id = []
    {
        for (ChatChannelsEntry const* e : sChatChannelsStore)
            if (e && e->GetFlags().HasFlag(ChatChannelFlags::LookingForGroup) && e->ID <= 255)
                return uint32(e->ID);
        return 0u;
    }();
    return id;
}

bool LfgCallSlot(bool alliance, std::string const& line, uint32 now_ms)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    uint32& last = g_lfg_call_ms[alliance ? 0 : 1];
    if (!Elapsed(now_ms, last, kLfgCallGapMs) || g_recent_calls.Seen(line))
        return false;
    last = now_ms ? now_ms : 1u;
    g_recent_calls.Note(line);
    return true;
}

void OnPlayerLine(Player* sender, Channel* ch, std::string const& msg)
{
    if (!sender || !ch || msg.empty() || !Services::Initialized() || !Services::Config().chatter_channel_replies())
        return;
    if (!sender->GetSession() || sender->GetSession()->IsBot())
        return;
    if (msg.front() == '.' || msg.front() == ';' || msg.front() == '!')
        return;
    Reach const reach = ReachOf(ch, LfgChannelId());
    if (reach == Reach::None)
        return;
    CT::Ask const a = CT::Parse(msg);
    if (a.kind == CT::Kind::None)
        return;

    uint32 const now = GameTime::GetGameTimeMS();
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        uint64 const key = sender->GetGUID().GetCounter();
        auto it = g_sender_ms.find(key);
        if (it != g_sender_ms.end() && !Elapsed(now, it->second, kSenderCooldownMs))
            return;
        Prune(g_sender_ms, now, kSenderCooldownMs * 4);
        g_sender_ms[key] = now ? now : 1u;
    }

    std::vector<Cand> const readers = Readers(sender, reach);
    if (readers.empty())
        return;

    switch (a.kind)
    {
        case CT::Kind::LookingForMore:  AnswerLfm(sender, a, readers, now); return;
        case CT::Kind::LookingForGroup: AnswerLfg(sender, a, readers, now); return;
        case CT::Kind::WantToBuy:       AnswerWtb(sender, a, readers, now); return;
        default: break;
    }

    // A question or a greeting is answered in the channel, by one bot, now and then.
    uint32 const pct = a.kind == CT::Kind::Question ? Services::Config().chatter_channel_question_pct()
                                                    : Services::Config().chatter_channel_greet_pct();
    if (urand(0, 99) >= pct)
        return;
    uint64 const slot = (uint64(sender->GetTeam()) << 48) | (uint64(ch->GetChannelId()) << 32) | (reach == Reach::Zone ? sender->GetZoneId() : 0u);
    if (!TakeChannelSlot(slot, now))
        return;
    Cand const* chosen = nullptr;
    for (Cand const& c : readers)
        if (c.ai->personality().verbosity >= Verbosity::Normal) { chosen = &c; break; }
    if (!chosen)
        return;

    std::string reply;
    if (a.kind == CT::Kind::Greeting)
    {
        if (BotTextMgr::instance().Enabled())
            reply = BotTextMgr::instance().GetReply(ReplyType::Hello, sender->GetName());
        if (reply.empty())
        {
            static constexpr char const* kHi[] = { "hey", "o/", "hi", "hello", "heya", "hey there", "yo" };
            reply = kHi[urand(0, uint32(std::size(kHi)) - 1)];
        }
    }
    else if (!BotChatReactor::SmallTalk(chosen->bot, sender, msg, reply))
    {
        AnswerHook hook;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            hook = g_hook;
        }
        if (hook && hook(chosen->bot, sender, ch->GetChannelId(), msg))
            TC_LOG_INFO("playerbot.v2", "[channel] {} hands {}'s line to the answer hook", chosen->bot->GetName(), sender->GetName());
        return;
    }
    if (reply.empty() || CT::HasUnfilledPlaceholder(reply))
        return;
    TC_LOG_INFO("playerbot.v2", "[channel] {} answers {} in channel {}: {}", chosen->bot->GetName(), sender->GetName(), ch->GetChannelId(), reply);
    Post(chosen->id, ch->GetChannelId(), std::move(reply), now + 2500u + urand(0, 5000));
}

} // namespace Playerbot::V2::ChannelChat
