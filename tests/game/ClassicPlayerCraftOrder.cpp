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

#include "../../src/modules/PlayerbotV2/Bot/PlayerCraftOrder.h"

#include <vector>

using namespace Playerbot::V2::PlayerCraft;

TEST_CASE("PlayerCraftOrder: parsing a craft order", "[playercraft]")
{
    SECTION("not a craft order")
    {
        CHECK(ParseRequest("follow", 20).type == Kind::None);
        CHECK(ParseRequest("crafty bandage", 20).type == Kind::None);
        CHECK(ParseRequest("craft", 20).type == Kind::None);
        CHECK(ParseRequest("craft   ", 20).type == Kind::None);
    }
    SECTION("list")
    {
        CHECK(ParseRequest("craft list", 20).type == Kind::List);
        CHECK(ParseRequest("  CRAFT   List ", 20).type == Kind::List);
    }
    SECTION("by name, count first, last or absent")
    {
        Request r = ParseRequest("craft Linen Bandage", 20);
        CHECK(r.type == Kind::Craft);
        CHECK(r.name == "linen bandage");
        CHECK(r.count == 1);
        r = ParseRequest("craft linen bandage x5", 20);
        CHECK(r.name == "linen bandage");
        CHECK(r.count == 5);
        r = ParseRequest("craft 5x linen bandage", 20);
        CHECK(r.name == "linen bandage");
        CHECK(r.count == 5);
        r = ParseRequest("craft linen bandage 7", 20);
        CHECK(r.name == "linen bandage");
        CHECK(r.count == 7);
    }
    SECTION("the count is clamped to 1..max")
    {
        CHECK(ParseRequest("craft linen bandage x500", 20).count == 20);
        CHECK(ParseRequest("craft linen bandage x0", 20).count == 1);
    }
    SECTION("a number inside the name is not a count")
    {
        Request const r = ParseRequest("craft elixir of 5 wonders", 20);
        CHECK(r.name == "elixir of 5 wonders");
        CHECK(r.count == 1);
    }
    SECTION("by item link, with and without colour code and count")
    {
        Request r = ParseRequest("craft |cffffffff|Hitem:1251::::::::60:::::|h[Linen Bandage]|h|r x3", 20);
        CHECK(r.type == Kind::Craft);
        CHECK(r.item_entry == 1251);
        CHECK(r.name.empty());
        CHECK(r.count == 3);
        r = ParseRequest("craft |Hitem:2996::::|h[Bolt of Linen Cloth]|h", 20);
        CHECK(r.item_entry == 2996);
        CHECK(r.count == 1);
        r = ParseRequest("craft 4 |Hitem:2996::::|h[Bolt of Linen Cloth]|h", 20);
        CHECK(r.item_entry == 2996);
        CHECK(r.count == 4);
    }
}

TEST_CASE("PlayerCraftOrder: matching a recipe", "[playercraft]")
{
    std::vector<Option> const opts = {
        { 3275, 1251, 1, "linen bandage" },
        { 3276, 1659, 1, "heavy linen bandage" },
        { 2963, 2996, 1, "bolt of linen cloth" },
    };
    CHECK(Match(ParseRequest("craft linen bandage", 20), opts) == 0);       // exact beats substring of "heavy linen bandage"
    CHECK(Match(ParseRequest("craft heavy", 20), opts) == 1);               // prefix
    CHECK(Match(ParseRequest("craft cloth", 20), opts) == 2);               // substring
    CHECK(Match(ParseRequest("craft |Hitem:2996::::|h[x]|h", 20), opts) == 2);  // link by entry
    CHECK(Match(ParseRequest("craft |Hitem:9999::::|h[x]|h", 20), opts) == -1);
    CHECK(Match(ParseRequest("craft sword", 20), opts) == -1);

    std::vector<Option> const ties = { { 20, 1, 1, "bandage b" }, { 10, 2, 1, "bandage a" } };
    CHECK(Match(ParseRequest("craft bandage", 20), ties) == 1);             // same length: lower spell id
}

TEST_CASE("PlayerCraftOrder: a generic name gets the best rank", "[playercraft]")
{
    // "craft water" fitted every Conjure Water rank and the shortest name (rank 1) won: a player asked a level 60 mage and got 151 mana water.
    std::vector<Option> const water = {
        { 5504, 5350, 3, "conjured water", 0 * 1000 + 5 },
        { 10138, 8077, 3, "conjured mineral water", 25 * 1000 + 25 },
        { 10140, 8079, 10, "conjured crystal water", 45 * 1000 + 45 },
        { 5505, 1179, 3, "conjured fresh water", 5 * 1000 + 10 },
    };
    CHECK(Match(ParseRequest("craft water", 20), water) == 2);              // the best rank
    CHECK(Match(ParseRequest("craft conjured water", 20), water) == 0);     // an exact name still means exactly that item
    CHECK(Match(ParseRequest("craft crystal water", 20), water) == 2);
    CHECK(Match(ParseRequest("craft mineral", 20), water) == 1);
}

TEST_CASE("PlayerCraftOrder: planning the casts", "[playercraft]")
{
    Plan p = PlanOrder({ 5, 1, 20, 10 });
    CHECK(p.casts == 5);
    CHECK(p.items == 5);
    CHECK_FALSE(p.trimmed);
    CHECK(p.refusal == nullptr);

    p = PlanOrder({ 5, 2, 20, 10 });           // rounds up: 3 casts make 6
    CHECK(p.casts == 3);
    CHECK(p.items == 6);

    p = PlanOrder({ 10, 1, 20, 4 });           // reagents run out first
    CHECK(p.casts == 4);
    CHECK(p.trimmed);

    p = PlanOrder({ 50, 1, 20, 100 });         // the per order cap
    CHECK(p.items == 20);
    CHECK(p.trimmed);

    p = PlanOrder({ 5, 1, 20, 0 });
    CHECK(std::string(p.refusal) == "no_reagents");
    p = PlanOrder({ 5, 30, 20, 3 });           // one cast already makes more than the cap
    CHECK(std::string(p.refusal) == "too_many");
}

TEST_CASE("PlayerCraftOrder: a unique item the bot already carries is not made again", "[playercraft]")
{
    CHECK(CarryRoom(0, 5) == 0xFFFFFFFFu);     // no limit
    CHECK(CarryRoom(1, 0) == 1);
    CHECK(CarryRoom(1, 1) == 0);               // Mana Jade: one carried
    CHECK(CarryRoom(5, 3) == 2);

    Plan p = PlanOrder({ 1, 1, 20, 1000, CarryRoom(1, 1) });
    CHECK(std::string(p.refusal) == "already_carried");

    p = PlanOrder({ 3, 1, 20, 1000, CarryRoom(1, 0) });      // asked for three, may hold one
    CHECK(p.refusal == nullptr);
    CHECK(p.casts == 1);
    CHECK(p.items == 1);
    CHECK(p.trimmed);

    p = PlanOrder({ 5, 1, 20, 10, CarryRoom(0, 0) });        // no limit changes nothing
    CHECK(p.items == 5);
}

TEST_CASE("PlayerCraftOrder: how a finished order reaches the requester", "[playercraft]")
{
    CHECK(DeliveryFor(false, false) == Delivery::Mail);
    CHECK(DeliveryFor(true, false) == Delivery::Trade);      // conjured: the mail refuses it
    CHECK(DeliveryFor(false, true) == Delivery::Bound);      // soulbound: neither route
    CHECK(DeliveryFor(true, true) == Delivery::Bound);
}
