#include "BotTradeShop.h"
#include "BotItemScorer.h"
#include "BotTrade.h"
#include "PlayerbotAPI.h"
#include "AuctionHouseMgr.h"
#include "GameTime.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TradeData.h"
#include "TradePackets.h"
#include "WorldSession.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace Playerbot::TradeShop {

namespace {

constexpr uint32 kShelfLifeMs = 10u * 60u * 1000u;
constexpr uint32 kSayRepeatMs = 20000u;

struct Shelf
{
    uint32 stamp_ms = 0;
    std::vector<Offer> list;
    bool has_order = false;
    Offer order;
};

struct Delivery
{
    ObjectGuid requester;
    uint32 entry = 0;
    uint32 stamp_ms = 0;
};

std::mutex g_mtx;
std::unordered_map<uint64, Delivery> g_deliveries;                     // bot -> the craft order waiting to be handed over
std::unordered_map<uint64, Shelf> g_shelves;                           // (bot << 32) | buyer
std::unordered_map<uint64, std::pair<uint64, uint32>> g_last_say;      // bot -> (what was said, when)

uint64 Key(Player const* bot, Player const* buyer)
{
    return (uint64(bot->GetGUID().GetCounter()) << 32) | uint64(buyer->GetGUID().GetCounter());
}

std::string NameOf(ItemTemplate const* t)
{
    return t ? t->GetName(DEFAULT_LOCALE) : std::string("?");
}

bool IsTradeable(Item* it)
{
    return it && !it->IsNotEmptyBag() && it->CanBeTraded(false, true);
}

// Items of the classes a bot lives on or that belong to a quest are never for sale.
bool KeptByBot(ItemTemplate const* t)
{
    switch (t->GetClass())
    {
        case ITEM_CLASS_CONSUMABLE:
        case ITEM_CLASS_REAGENT:
        case ITEM_CLASS_PROJECTILE:
        case ITEM_CLASS_QUEST:
        case ITEM_CLASS_KEY:
            return true;
        default:
            return t->GetStartQuest() != 0 || t->GetBonding() == BIND_QUEST;
    }
}

void Say(Player* bot, Player* to, BotTrade::Say what, uint64 money, std::string const& text)
{
    if (text.empty()) return;
    uint64 const key = (uint64(what) << 56) ^ money;
    uint32 const now = GameTime::GetGameTimeMS();
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto& last = g_last_say[bot->GetGUID().GetCounter()];
        if (last.first == key && now - last.second < kSayRepeatMs) return;
        last = { key, now };
    }
    bot->Whisper(text, LANG_UNIVERSAL, to);
}

} // namespace

std::vector<uint64> UnitPrices(Player const* viewer, std::vector<uint32> const& entries)
{
    std::unordered_set<uint32> const wanted(entries.begin(), entries.end());
    std::unordered_map<uint32, std::vector<uint64>> listed;
    if (viewer && !wanted.empty())
        if (AuctionHouseObject* ah = sAuctionMgr->GetAuctionsMap(viewer->GetFaction()))
            for (auto it = ah->GetAuctionsBegin(); it != ah->GetAuctionsEnd(); ++it)
            {
                AuctionPosting const& a = it->second;
                if (a.Items.empty() || !a.Items[0] || !wanted.count(a.Items[0]->GetEntry()) || a.BuyoutOrUnitPrice == 0) continue;
                uint32 const n = a.GetTotalItemCount();
                if (!n) continue;
                listed[a.Items[0]->GetEntry()].push_back(a.IsCommodity() ? a.BuyoutOrUnitPrice : a.BuyoutOrUnitPrice / n);
            }

    std::vector<uint64> out;
    out.reserve(entries.size());
    for (uint32 entry : entries)
    {
        ItemTemplate const* t = sObjectMgr->GetItemTemplate(entry);
        if (!t) { out.push_back(0); continue; }
        BotTrade::PriceInputs in;
        auto f = listed.find(entry);
        if (f != listed.end()) in.ah_unit = BotTrade::MedianPrice(f->second);
        in.vendor_sell = t->GetSellPrice();
        in.vendor_buy = t->GetBuyPrice() / t->GetBuyCount();
        in.poor = t->GetQuality() == ITEM_QUALITY_POOR;
        out.push_back(BotTrade::FairUnitPrice(in));
    }
    return out;
}

bool IsUpgrade(Player* bot, ItemTemplate const* t)
{
    if (!bot || !t || (t->GetClass() != ITEM_CLASS_WEAPON && t->GetClass() != ITEM_CLASS_ARMOR)) return false;
    uint16 dest = 0;
    if (bot->CanEquipNewItem(NULL_SLOT, dest, t->GetId(), false) != EQUIP_ERR_OK) return false;
    uint8 const slot = dest & 0xFF;
    if (slot >= EQUIPMENT_SLOT_END) return false;
    Item const* cur = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!cur) return true;
    return Gear::ScoreItemForBot(t, bot) > Gear::ScoreItemForBot(cur->GetTemplate(), bot);
}

std::vector<Offer> BuildOffers(Player* bot, uint32 max)
{
    std::vector<Offer> out;
    if (!bot) return out;
    std::vector<Item*> stacks;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* it)
    {
        ItemTemplate const* t = it ? it->GetTemplate() : nullptr;
        if (t && IsTradeable(it) && !KeptByBot(t) && !IsUpgrade(bot, t))
            stacks.push_back(it);
        return ItemSearchCallbackResult::Continue;
    });
    std::vector<uint32> entries;
    for (Item* it : stacks) entries.push_back(it->GetEntry());
    std::vector<uint64> const unit = UnitPrices(bot, entries);
    for (size_t i = 0; i < stacks.size(); ++i)
    {
        if (!unit[i]) continue;
        Offer o;
        o.entry = stacks[i]->GetEntry();
        o.count = stacks[i]->GetCount();
        o.price = unit[i] * o.count;
        o.item = stacks[i]->GetGUID();
        o.name = NameOf(stacks[i]->GetTemplate());
        out.push_back(std::move(o));
    }
    std::sort(out.begin(), out.end(), [](Offer const& a, Offer const& b) { return a.price > b.price; });
    if (out.size() > max) out.resize(max);
    return out;
}

void StoreList(Player const* bot, Player const* buyer, std::vector<Offer> list)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_shelves.size() > 4096) g_shelves.clear();
    Shelf& s = g_shelves[Key(bot, buyer)];
    s = Shelf{};
    s.stamp_ms = GameTime::GetGameTimeMS();
    s.list = std::move(list);
}

bool GetList(Player const* bot, Player const* buyer, std::vector<Offer>& out)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_shelves.find(Key(bot, buyer));
    if (it == g_shelves.end() || GameTime::GetGameTimeMS() - it->second.stamp_ms > kShelfLifeMs) return false;
    out = it->second.list;
    return true;
}

bool PlaceOrder(Player const* bot, Player const* buyer, size_t index, Offer& out)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_shelves.find(Key(bot, buyer));
    if (it == g_shelves.end() || index >= it->second.list.size()) return false;
    it->second.order = it->second.list[index];
    it->second.has_order = true;
    it->second.stamp_ms = GameTime::GetGameTimeMS();
    out = it->second.order;
    return true;
}

void StoreDelivery(Player const* bot, ObjectGuid requester, uint32 entry)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_deliveries.size() > 4096) g_deliveries.clear();
    g_deliveries[bot->GetGUID().GetCounter()] = Delivery{ requester, entry, GameTime::GetGameTimeMS() };
}

std::vector<std::string> Describe(std::vector<Offer> const& list)
{
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < list.size(); ++i)
    {
        std::string line = std::to_string(i + 1) + ") " + list[i].name;
        if (list[i].count > 1) line += " x" + std::to_string(list[i].count);
        line += " - " + BotTrade::FormatMoney(list[i].price);
        if (!cur.empty() && cur.size() + line.size() + 2 > 200)
        {
            out.push_back(std::move(cur));
            cur.clear();
        }
        cur += cur.empty() ? line : "; " + line;
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

namespace {

// Is `entry` a reagent of a recipe (a spell that makes an item) the bot knows? Reagents the owner hands over for a craft order are a gift.
bool IsRecipeReagent(Player const* bot, uint32 entry)
{
    for (auto const& [spell_id, ps] : bot->GetSpellMap())
    {
        if (ps.state == PLAYERSPELL_REMOVED || !ps.active || ps.disabled)
            continue;
        SpellInfo const* si = sSpellMgr->GetSpellInfo(spell_id, DIFFICULTY_NONE);
        if (!si || !si->HasEffect(SPELL_EFFECT_CREATE_ITEM))
            continue;
        for (std::size_t i = 0; i < si->Reagent.size(); ++i)
            if (si->Reagent[i] > 0 && si->ReagentCount[i] > 0 && uint32(si->Reagent[i]) == entry)
                return true;
    }
    return false;
}

BotTrade::Line LineOf(Player const* viewer_for_price, Item* it, uint64 unit)
{
    (void)viewer_for_price;
    BotTrade::Line l;
    l.entry = it->GetEntry();
    l.count = it->GetCount();
    l.value = unit * it->GetCount();
    return l;
}

// The lines of one side of the window.
std::vector<BotTrade::Line> Lines(Player* bot, TradeData* side, bool mark_upgrades, std::vector<Item*>* items_out)
{
    std::vector<Item*> items;
    for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
        if (Item* it = side->GetItem(TradeSlots(i)))
            items.push_back(it);
    std::vector<uint32> entries;
    for (Item* it : items) entries.push_back(it->GetEntry());
    std::vector<uint64> const unit = UnitPrices(bot, entries);
    std::vector<BotTrade::Line> out;
    for (size_t i = 0; i < items.size(); ++i)
    {
        BotTrade::Line l = LineOf(bot, items[i], unit[i]);
        if (mark_upgrades)
        {
            l.upgrade = IsUpgrade(bot, items[i]->GetTemplate());
            l.reagent = IsRecipeReagent(bot, items[i]->GetEntry());
        }
        out.push_back(l);
    }
    if (items_out) *items_out = std::move(items);
    return out;
}

bool HasRoomFor(Player* bot, std::vector<Item*>& items)
{
    if (items.empty()) return true;
    uint32 offending = 0;
    if (bot->CanStoreItems(items.data(), int(items.size()), &offending) == EQUIP_ERR_OK) return true;
    // Bags full: clear what a player would vendor, then ask again.
    MakeBagRoom(bot, uint32(items.size()) + BotTrade::kKeepFreeSlots);
    return bot->CanStoreItems(items.data(), int(items.size()), &offending) == EQUIP_ERR_OK;
}

} // namespace

Result Step(Player* bot, TradeData* mine, TradeData* his, bool gift_ok)
{
    Player* partner = mine->GetTrader();
    if (!partner) return Result::Locked;

    BotTrade::View v;
    v.gift_partner = gift_ok;
    std::vector<Item*> his_items;
    v.his = Lines(bot, his, /*mark_upgrades*/ true, &his_items);
    v.his_money = his->GetMoney();
    std::vector<Item*> my_items;
    v.mine = Lines(bot, mine, false, &my_items);
    v.my_money = mine->GetMoney();
    v.bot_gold = bot->GetMoney();
    v.his_accepted = his->IsAccepted();
    v.my_accepted = mine->IsAccepted();
    v.bot_has_room = true;

    Offer order;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = g_shelves.find(Key(bot, partner));
        if (it != g_shelves.end() && it->second.has_order && GameTime::GetGameTimeMS() - it->second.stamp_ms <= kShelfLifeMs)
        {
            order = it->second.order;
            v.has_order = true;
        }
    }
    if (v.has_order)
    {
        Item* oi = bot->GetItemByGuid(order.item);
        v.order_entry = order.entry;
        v.order_price = order.price;
        v.order_item_present = oi && oi->GetEntry() == order.entry && oi->GetCount() == order.count && IsTradeable(oi);
    }
    // A craft order made for this player that could not go by mail: still in the bags, it is handed over in this window.
    Item* delivery = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = g_deliveries.find(bot->GetGUID().GetCounter());
        if (it != g_deliveries.end())
        {
            if (GameTime::GetGameTimeMS() - it->second.stamp_ms > kShelfLifeMs)
                g_deliveries.erase(it);
            else if (it->second.requester == partner->GetGUID())
            {
                std::vector<Item*> held = bot->GetItemListByEntry(it->second.entry, false);
                if (held.empty())
                    g_deliveries.erase(it);
                else
                {
                    delivery = held.front();
                    v.has_delivery = true;
                    v.delivery_present = true;
                }
            }
        }
    }
    // Only look at room when the bot is about to take something in: it is the costly check.
    if (!v.his.empty() && v.his_accepted && !v.my_accepted)
        v.bot_has_room = HasRoomFor(bot, his_items);

    BotTrade::Action const a = BotTrade::Decide(v);

    // What the bot says about it.
    if (a.say != BotTrade::Say::None)
    {
        std::string text;
        switch (a.say)
        {
            case BotTrade::Say::OfferBuy:    text = "i'll give you " + BotTrade::FormatMoney(a.money) + " for that"; break;
            case BotTrade::Say::PriceIs:     text = order.name + " is " + BotTrade::FormatMoney(order.price); break;
            default:                         text = BotTrade::SayText(a.say); break;
        }
        Say(bot, partner, a.say, a.money ? a.money : order.price, text);
    }

    switch (a.act)
    {
        case BotTrade::Act::Wait:
            return Result::Locked;
        case BotTrade::Act::Cancel:
            bot->TradeCancel(true);
            return Result::ServerRefused;
        case BotTrade::Act::SetMoney:
            mine->UpdateClientStateIndex();
            mine->SetMoney(a.money);
            return Result::Locked;
        case BotTrade::Act::ClearMine:
            mine->UpdateClientStateIndex();
            for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
                mine->SetItem(TradeSlots(i), nullptr);
            return Result::Locked;
        case BotTrade::Act::AddOrdered:
        {
            Item* oi = bot->GetItemByGuid(order.item);
            if (!oi) return Result::Locked;
            for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
                if (!mine->GetItem(TradeSlots(i)))
                {
                    if (oi->IsBindedNotWith(partner)) return Result::ServerRefused;
                    mine->UpdateClientStateIndex();
                    mine->SetItem(TradeSlots(i), oi);
                    break;
                }
            return Result::Locked;
        }
        case BotTrade::Act::AddDelivery:
        {
            if (!delivery) return Result::Locked;
            for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
                if (!mine->GetItem(TradeSlots(i)))
                {
                    if (delivery->IsBindedNotWith(partner)) return Result::ServerRefused;
                    mine->UpdateClientStateIndex();
                    mine->SetItem(TradeSlots(i), delivery);
                    break;
                }
            return Result::Locked;
        }
        case BotTrade::Act::Accept:
        {
            WorldPacket data(CMSG_ACCEPT_TRADE);
            data << uint32(his->GetServerStateIndex());
            WorldPackets::Trade::AcceptTrade accept(std::move(data));
            accept.Read();
            bot->GetSession()->HandleAcceptTradeOpcode(accept);
            if (v.has_order && !v.mine.empty())
            {
                std::lock_guard<std::mutex> lk(g_mtx);
                auto it = g_shelves.find(Key(bot, partner));
                if (it != g_shelves.end()) it->second.has_order = false;
            }
            return Result::Ok;
        }
    }
    return Result::Locked;
}

} // namespace Playerbot::TradeShop
