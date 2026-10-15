/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tc_catch2.h"

#include "../../src/modules/PlayerbotV2/Api/BotTrade.h"
#include "../../src/modules/PlayerbotV2/Bot/TradeTalk.h"

using namespace Playerbot::BotTrade;
namespace TT = Playerbot::V2::TradeTalk;

namespace {
Line L(std::uint32_t entry, std::uint32_t count, std::uint64_t value, bool upgrade = false) { return { entry, count, value, upgrade }; }
}

TEST_CASE("BotTrade: the auction house price wins when the item is listed", "[bottrade]")
{
    PriceInputs in;
    in.ah_unit = 500; in.vendor_sell = 20; in.vendor_buy = 80;
    CHECK(FairUnitPrice(in) == 500);
}

TEST_CASE("BotTrade: with no listing the price comes from the vendor value", "[bottrade]")
{
    PriceInputs sold;                       // a vendor sells it
    sold.vendor_sell = 20; sold.vendor_buy = 80;
    CHECK(FairUnitPrice(sold) == 80);

    PriceInputs drop;                       // a vendor does not sell it: the vanilla 4:1 ratio
    drop.vendor_sell = 25;
    CHECK(FairUnitPrice(drop) == 100);

    PriceInputs grey;                       // greys are only ever worth the vendor's payout
    grey.vendor_sell = 7; grey.vendor_buy = 28; grey.poor = true;
    CHECK(FairUnitPrice(grey) == 7);

    CHECK(FairUnitPrice({}) == 0);
}

TEST_CASE("BotTrade: the market price is the median, so one absurd listing does not set it", "[bottrade]")
{
    CHECK(MedianPrice({}) == 0);
    CHECK(MedianPrice({ 90, 100, 5000000 }) == 100);
    CHECK(MedianPrice({ 10, 20 }) == 15);
}

TEST_CASE("BotTrade: money is spoken in gold, silver and copper", "[bottrade]")
{
    CHECK(FormatMoney(0) == "0c");
    CHECK(FormatMoney(5) == "5c");
    CHECK(FormatMoney(12005) == "1g 20s 5c");
    CHECK(FormatMoney(30000) == "3g");
    CHECK(FormatMoney(4000) == "40s");
}

TEST_CASE("BotTrade: a group mate's gear upgrade is a gift, accepted once he has accepted", "[bottrade]")
{
    View v;
    v.gift_partner = true;
    v.his = { L(1, 1, 5000, true) };
    CHECK(Decide(v).act == Act::Wait);                 // he is still choosing
    v.his_accepted = true;
    CHECK(Decide(v).act == Act::Accept);
    v.my_accepted = true;
    CHECK(Decide(v).act == Act::Wait);                 // already accepted, waiting for the swap
}

TEST_CASE("BotTrade: a gift the bot has no room for is cancelled with a reason", "[bottrade]")
{
    View v;
    v.gift_partner = true;
    v.his = { L(1, 1, 5000, true) };
    v.his_accepted = true;
    v.bot_has_room = false;
    Action const a = Decide(v);
    CHECK(a.act == Act::Cancel);
    CHECK(a.say == Say::NoRoom);
}

TEST_CASE("BotTrade: money alone is a gift from the owner and nothing from a stranger", "[bottrade]")
{
    View v;
    v.his_money = 100;
    v.his_accepted = true;
    CHECK(Decide(v).act == Act::Wait);
    v.gift_partner = true;
    CHECK(Decide(v).act == Act::Accept);
}

TEST_CASE("BotTrade: an item that is no upgrade is bought at its fair price", "[bottrade]")
{
    View v;
    v.gift_partner = true;
    v.bot_gold = 100000;
    v.his = { L(2, 5, 4000), L(3, 1, 1000) };
    Action a = Decide(v);
    CHECK(a.act == Act::SetMoney);
    CHECK(a.money == 5000);
    CHECK(a.say == Say::OfferBuy);

    v.my_money = 5000;                                  // the offer is in the window
    CHECK(Decide(v).act == Act::Wait);                  // he has not accepted
    v.his_accepted = true;
    CHECK(Decide(v).act == Act::Accept);
}

TEST_CASE("BotTrade: when he changes the items the offer follows", "[bottrade]")
{
    View v;
    v.bot_gold = 100000;
    v.my_money = 5000;
    v.his = { L(2, 5, 4000), L(3, 1, 1000), L(4, 1, 300) };
    v.his_accepted = true;
    Action const a = Decide(v);
    CHECK(a.act == Act::SetMoney);
    CHECK(a.money == 5300);
}

TEST_CASE("BotTrade: a bot that cannot pay says so and closes the window", "[bottrade]")
{
    View v;
    v.bot_gold = 100;
    v.his = { L(2, 1, 5000) };
    Action const a = Decide(v);
    CHECK(a.act == Act::Cancel);
    CHECK(a.say == Say::CantAfford);
}

TEST_CASE("BotTrade: worthless items are refused, not bought for nothing", "[bottrade]")
{
    View v;
    v.bot_gold = 100;
    v.his = { L(2, 1, 0) };
    Action const a = Decide(v);
    CHECK(a.act == Act::Cancel);
    CHECK(a.say == Say::WorthNothing);
}

TEST_CASE("BotTrade: a full bag stops the purchase", "[bottrade]")
{
    View v;
    v.bot_gold = 100000;
    v.his = { L(2, 1, 500) };
    v.my_money = 500;
    v.his_accepted = true;
    v.bot_has_room = false;
    CHECK(Decide(v).act == Act::Cancel);
}

TEST_CASE("BotTrade: items and gold from him at once is not a deal the bot can price", "[bottrade]")
{
    View v;
    v.bot_gold = 100000;
    v.his = { L(2, 1, 500) };
    v.his_money = 50;
    Action const a = Decide(v);
    CHECK(a.act == Act::Wait);
    CHECK(a.say == Say::OneWay);

    View mixed;                                         // an upgrade next to goods
    mixed.gift_partner = true;
    mixed.bot_gold = 100000;
    mixed.his = { L(1, 1, 500, true), L(2, 1, 500) };
    CHECK(Decide(mixed).say == Say::OneWay);
}

TEST_CASE("BotTrade: an order is filled when he puts in the price", "[bottrade]")
{
    View v;
    v.has_order = true;
    v.order_entry = 77;
    v.order_price = 2000;
    v.order_item_present = true;
    CHECK(Decide(v).act == Act::Wait);                  // nothing in the window yet

    v.his_money = 1500;                                 // too little
    Action a = Decide(v);
    CHECK(a.act == Act::Wait);
    CHECK(a.say == Say::PriceIs);

    v.his_money = 2000;
    CHECK(Decide(v).act == Act::AddOrdered);

    v.mine = { L(77, 1, 2000) };                        // the item is in the bot's side
    CHECK(Decide(v).act == Act::Wait);                  // he has not accepted
    v.his_accepted = true;
    CHECK(Decide(v).act == Act::Accept);
}

TEST_CASE("BotTrade: if he lowers the gold the item is taken back", "[bottrade]")
{
    View v;
    v.has_order = true;
    v.order_price = 2000;
    v.order_item_present = true;
    v.mine = { L(77, 1, 2000) };
    v.his_money = 1000;
    CHECK(Decide(v).act == Act::ClearMine);
}

TEST_CASE("BotTrade: an ordered item that has gone is reported", "[bottrade]")
{
    View v;
    v.has_order = true;
    v.order_price = 2000;
    v.order_item_present = false;
    v.his_money = 2000;
    Action const a = Decide(v);
    CHECK(a.act == Act::Cancel);
    CHECK(a.say == Say::OrderGone);
}

TEST_CASE("BotTrade: a bot keeps a few bag slots free", "[bottrade]")
{
    CHECK(SlotsToClear(0) == kKeepFreeSlots);
    CHECK(SlotsToClear(kKeepFreeSlots - 1) == 1);
    CHECK(SlotsToClear(kKeepFreeSlots) == 0);
    CHECK(SlotsToClear(30) == 0);
    CHECK(SlotsToClear(2, 6) == 4);
}

TEST_CASE("TradeTalk: the ways to ask what a bot sells", "[bottrade]")
{
    CHECK(TT::IsInventoryQuestion("what do you have"));
    CHECK(TT::IsInventoryQuestion("hey what are you selling"));
    CHECK(TT::IsInventoryQuestion("anything for sale"));
    CHECK(TT::IsInventoryQuestion("trade list"));
    CHECK_FALSE(TT::IsInventoryQuestion("how are you"));
    CHECK_FALSE(TT::IsInventoryQuestion("where do you have your hearthstone"));
}

TEST_CASE("TradeTalk: an order by number or by name", "[bottrade]")
{
    TT::Order o = TT::ParseOrder("buy 2");
    CHECK(o.is_order);
    CHECK(o.buy_verb);
    CHECK(o.index == 2);

    o = TT::ParseOrder("ok i ll take the 3");
    CHECK(o.is_order);
    CHECK_FALSE(o.buy_verb);
    CHECK(o.index == 3);

    o = TT::ParseOrder("buy the linen cloth");
    CHECK(o.is_order);
    CHECK(o.name == "linen cloth");

    CHECK_FALSE(TT::ParseOrder("how are you").is_order);
    CHECK_FALSE(TT::ParseOrder("buy").is_order);
}

TEST_CASE("TradeTalk: an order is matched against the list", "[bottrade]")
{
    std::vector<TT::ListedItem> list{ { "heavy linen bandage" }, { "linen cloth" }, { "mageweave cloth" } };
    CHECK(TT::ResolveOrder(TT::ParseOrder("buy 3"), list) == 2);
    CHECK(TT::ResolveOrder(TT::ParseOrder("buy 9"), list) == -1);
    CHECK(TT::ResolveOrder(TT::ParseOrder("buy linen cloth"), list) == 1);
    CHECK(TT::ResolveOrder(TT::ParseOrder("buy mageweave"), list) == 2);
    CHECK(TT::ResolveOrder(TT::ParseOrder("buy a sword"), list) == -1);
}
