#include "PlayerCraftOrders.h"

#include "../Bot/BotIntent.h"
#include "../Bot/BotIntentEmitter.h"
#include "../Bot/BotRegistry.h"
#include "../Bot/BotSnapshot.h"
#include "../Bot/PlayerCraftOrder.h"
#include "../Services.h"
#include "../Threading/IntentQueue.h"
#include "../Threading/SnapshotPublisher.h"

#include "GameTime.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "World.h"

#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <vector>

namespace Playerbot::V2::PlayerCraftOrders {
namespace {

namespace pc = ::Playerbot::V2::PlayerCraft;

constexpr std::uint32_t kMaxItems = 20;            // per order (fits one mail stack of most crafts)
constexpr std::uint32_t kMaxListed = 12;
constexpr std::uint32_t kCastGapMs = 2500;         // between two casts, long enough for a profession cast to finish and report
constexpr std::uint32_t kOrderDeadlineMs = 10u * 60u * 1000u;
constexpr std::uint32_t kMailWaitMs = 15000;       // for the mail intent to take the items out of the bags
constexpr std::uint32_t kExtraCasts = 3;           // casts beyond the plan before the order is given up (a cast can fail and be retried)

enum class Phase : std::uint8_t { Crafting, Mailing };

struct Job
{
    std::uint64_t bot = 0;                // guid counter
    std::string requester;
    std::uint32_t spell = 0;
    std::uint32_t item = 0;
    std::uint32_t yield = 1;
    std::uint32_t wanted = 0;             // items the plan promised
    std::uint32_t baseline = 0;           // the bot's stock of the item when the order was taken
    std::uint32_t casts_planned = 0;
    std::uint32_t casts_pushed = 0;
    std::uint32_t started_ms = 0;
    std::uint32_t last_cast_ms = 0;
    std::uint32_t mail_ms = 0;
    std::uint32_t mailing = 0;            // items handed to the mail intents
    std::uint32_t before_mail = 0;        // the bot's stock when the mail intents were pushed
    Phase phase = Phase::Crafting;
};

std::vector<Job> g_jobs;

template <class T>
bool Push(Player const* bot, T body)
{
    BotId const id = bot->GetGUID().GetCounter();
    IntentQueue* q = Services::Registry().intents(id);
    IntentId* next = Services::Registry().next_intent_id(id);
    if (!q || !next)
        return false;
    BotIntentEmitter emit(q, id, /*source*/ 0, next);
    return emit.emit(WrapForIntentBody(std::move(body)));
}

void Say(Player const* bot, std::string const& to, std::string text)
{
    Push(bot, WhisperIntent{ to, std::move(text) });
}

std::string Lower(std::string s)
{
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string ItemLink(ItemTemplate const* t)
{
    return fmt::format("[{}]", t->GetName(sWorld->GetDefaultDbcLocale()));
}

// Casts the bot's bags allow for this recipe right now.
std::uint32_t CastsPossible(Player const* bot, SpellInfo const* si)
{
    std::uint32_t casts = 1000;
    bool any = false;
    for (std::size_t i = 0; i < si->Reagent.size(); ++i)
    {
        std::int32_t const entry = si->Reagent[i];
        std::int16_t const need = si->ReagentCount[i];
        if (entry <= 0 || need <= 0)
            continue;
        any = true;
        casts = std::min<std::uint32_t>(casts, bot->GetItemCount(std::uint32_t(entry), false) / std::uint32_t(need));
    }
    return any ? casts : 1000u;   // no reagents: bounded by the item cap in PlanOrder
}

// Every item the bot can make with a recipe it knows.
std::vector<pc::Option> Options(Player const* bot)
{
    std::vector<pc::Option> out;
    BotId const id = bot->GetGUID().GetCounter();
    auto snap = Services::Snapshots().latest(id);
    if (!snap)
        return out;
    for (std::uint32_t spell : snap->spellbook.known_recipes)
    {
        SpellInfo const* si = sSpellMgr->GetSpellInfo(spell, DIFFICULTY_NONE);
        if (!si)
            continue;
        for (SpellEffectInfo const& eff : si->GetEffects())
        {
            if (eff.Effect != SPELL_EFFECT_CREATE_ITEM || eff.ItemType == 0)
                continue;
            ItemTemplate const* t = sObjectMgr->GetItemTemplate(eff.ItemType);
            if (!t)
                continue;
            pc::Option o;
            o.spell = spell;
            o.item = eff.ItemType;
            o.yield = std::max<std::uint32_t>(1u, std::uint32_t(std::max(eff.CalcValueAsInt(bot), 1)));
            o.name = Lower(t->GetName(sWorld->GetDefaultDbcLocale()));
            out.push_back(std::move(o));
            break;
        }
    }
    return out;
}

void Finish(Job const& j, Player const* bot, std::string const& text)
{
    if (bot)
        Say(bot, j.requester, text);
    TC_LOG_INFO("playerbot.v2", "[craft_order] bot {} for {}: {}", j.bot, j.requester, text);
}

} // namespace

bool Order(Player* requester, Player* bot, std::string const& text)
{
    pc::Request const req = pc::ParseRequest(text, kMaxItems);
    if (req.type == pc::Kind::None)
        return false;
    std::string const to = requester->GetName();

    std::vector<pc::Option> const opts = Options(bot);
    if (req.type == pc::Kind::List)
    {
        std::uint32_t listed = 0, total = 0;
        for (pc::Option const& o : opts)
        {
            SpellInfo const* si = sSpellMgr->GetSpellInfo(o.spell, DIFFICULTY_NONE);
            if (!si || CastsPossible(bot, si) == 0)
                continue;
            ++total;
            if (listed >= kMaxListed)
                continue;
            ItemTemplate const* t = sObjectMgr->GetItemTemplate(o.item);
            Say(bot, to, fmt::format("I can make {} (up to {} now)", t ? ItemLink(t) : std::string("?"), std::min(CastsPossible(bot, si) * o.yield, kMaxItems)));
            ++listed;
        }
        if (total == 0)
            Say(bot, to, "I can't make anything with what I carry.");
        else if (total > listed)
            Say(bot, to, fmt::format("...and {} more", total - listed));
        return true;
    }

    int const idx = pc::Match(req, opts);
    if (idx < 0)
    {
        Say(bot, to, "I don't know a recipe for that.");
        return true;
    }
    pc::Option const& o = opts[std::size_t(idx)];
    SpellInfo const* si = sSpellMgr->GetSpellInfo(o.spell, DIFFICULTY_NONE);
    ItemTemplate const* t = sObjectMgr->GetItemTemplate(o.item);
    if (!si || !t)
    {
        Say(bot, to, "I can't make that right now.");
        return true;
    }
    for (Job const& j : g_jobs)
        if (j.bot == bot->GetGUID().GetCounter())
        {
            Say(bot, to, "I'm still busy with an order, ask me again in a moment.");
            return true;
        }

    pc::Plan const plan = pc::PlanOrder({ req.count, o.yield, kMaxItems, CastsPossible(bot, si) });
    if (plan.refusal)
    {
        if (std::string_view(plan.refusal) == "no_reagents")
        {
            std::string need;
            for (std::size_t i = 0; i < si->Reagent.size(); ++i)
                if (si->Reagent[i] > 0 && si->ReagentCount[i] > 0)
                    if (ItemTemplate const* r = sObjectMgr->GetItemTemplate(std::uint32_t(si->Reagent[i])))
                        need += fmt::format("{}{} x{}", need.empty() ? "" : ", ", ItemLink(r), si->ReagentCount[i]);
            Say(bot, to, fmt::format("I'm out of reagents for {}. One needs: {}. Trade them to me and ask again.", ItemLink(t), need));
        }
        else
            Say(bot, to, "That's more than I make in one order.");
        return true;
    }

    Job j;
    j.bot = bot->GetGUID().GetCounter();
    j.requester = to;
    j.spell = o.spell;
    j.item = o.item;
    j.yield = o.yield;
    j.wanted = plan.items;
    j.baseline = bot->GetItemCount(o.item, false);
    j.casts_planned = plan.casts;
    j.started_ms = GameTime::GetGameTimeMS();
    g_jobs.push_back(j);
    Say(bot, to, plan.trimmed
        ? fmt::format("I can make {} of the {} you asked for with my reagents; I'll mail {} to you when done.", plan.items, ItemLink(t), plan.items)
        : fmt::format("Making {} x{}, I'll mail it to you when done.", ItemLink(t), plan.items));
    TC_LOG_INFO("playerbot.v2", "[craft_order] queued: {} for {} item {} x{} (spell {}, {} casts)", bot->GetName(), to, o.item, plan.items, o.spell, plan.casts);
    return true;
}

void Tick(std::uint32_t now_ms)
{
    for (std::size_t i = 0; i < g_jobs.size();)
    {
        Job& j = g_jobs[i];
        Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(j.bot));
        bool done = false;
        if (!bot)
        {
            if (now_ms - j.started_ms > kOrderDeadlineMs)
            {
                Finish(j, nullptr, "bot gone, order dropped");
                done = true;
            }
        }
        else
        {
            std::uint32_t const have = bot->GetItemCount(j.item, false);
            std::uint32_t const crafted = have > j.baseline ? have - j.baseline : 0;
            if (j.phase == Phase::Crafting)
            {
                bool const out_of_time = now_ms - j.started_ms > kOrderDeadlineMs;
                bool const gave_up = j.casts_pushed >= j.casts_planned + kExtraCasts && !bot->IsNonMeleeSpellCast(false)
                    && now_ms - j.last_cast_ms >= kCastGapMs;
                if (crafted >= j.wanted || out_of_time || gave_up)
                {
                    if (crafted == 0)
                    {
                        Finish(j, bot, "I couldn't make that (no item came out), sorry.");
                        done = true;
                    }
                    else
                    {
                        // mail exactly what was made, stack by stack
                        std::uint32_t left = std::min(crafted, j.wanted);
                        j.before_mail = have;
                        for (Item* it : bot->GetItemListByEntry(j.item, false))
                        {
                            if (!left)
                                break;
                            std::uint32_t const n = std::min(left, it->GetCount());
                            if (Push(bot, MailSendItemIntent{ j.requester, it->GetGUID(), n, 0, 0, "Your crafted order", "Made to order." }))
                            {
                                left -= n;
                                j.mailing += n;
                            }
                        }
                        j.phase = Phase::Mailing;
                        j.mail_ms = now_ms;
                    }
                }
                else if (!bot->IsNonMeleeSpellCast(false) && !bot->IsInCombat() && bot->IsAlive() && now_ms - j.last_cast_ms >= kCastGapMs)
                {
                    Push(bot, CastSpellIntent{ j.spell, ObjectGuid::Empty });
                    ++j.casts_pushed;
                    j.last_cast_ms = now_ms;
                }
            }
            else
            {
                std::uint32_t const gone = j.before_mail > have ? j.before_mail - have : 0;
                if (gone >= j.mailing)
                {
                    Finish(j, bot, fmt::format("Mailed {} to you.", j.mailing));
                    done = true;
                }
                else if (now_ms - j.mail_ms > kMailWaitMs)
                {
                    Finish(j, bot, fmt::format("I made it but the mail failed ({} of {} left my bags); trade me for it.", gone, j.mailing));
                    done = true;
                }
            }
        }
        if (done)
            g_jobs.erase(g_jobs.begin() + std::ptrdiff_t(i));
        else
            ++i;
    }
}

std::uint32_t Pending()
{
    return std::uint32_t(g_jobs.size());
}

} // namespace Playerbot::V2::PlayerCraftOrders
