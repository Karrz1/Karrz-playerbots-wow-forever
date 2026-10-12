// BotTradeShop - the live side of bot trading: what a bot has for sale, what things are worth, and one step of the trade window.
// The decisions are in BotTrade.h (pure, tested); this reads and changes the Player, the TradeData and the auction house.
// World thread only.

#pragma once

#include "Define.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

class Item;
class Player;
class TradeData;
struct ItemTemplate;

namespace Playerbot
{
enum class Result : uint8;
}

namespace Playerbot::TradeShop {

struct Offer
{
    uint32 entry = 0;
    uint32 count = 0;
    uint64 price = 0;        // for the whole stack, copper
    ObjectGuid item;
    std::string name;
};

// What one stack is worth on the market, for the bot's faction. `entries` are looked up in the live auction house together.
// Items the auction house does not list are priced from their vendor value (BotTrade::FairUnitPrice).
std::vector<uint64> UnitPrices(Player const* viewer, std::vector<uint32> const& entries);

// Gear the bot would wear in place of what it has there (an empty slot counts).
bool IsUpgrade(Player* bot, ItemTemplate const* tmpl);

// The stacks the bot is willing to sell, most valuable first, at most `max` of them: tradeable, no quest items, nothing the bot
// itself lives on (consumables, reagents, ammo) and no gear that would be an upgrade for it.
std::vector<Offer> BuildOffers(Player* bot, uint32 max = 10);

// What the bot last listed to `buyer`, and the order he placed from it (both forgotten after ten minutes).
void StoreList(Player const* bot, Player const* buyer, std::vector<Offer> list);
bool GetList(Player const* bot, Player const* buyer, std::vector<Offer>& out);
bool PlaceOrder(Player const* bot, Player const* buyer, size_t index, Offer& out);

// "1) Linen Cloth x20 - 40s" lines packed into whisper-sized chunks.
std::vector<std::string> Describe(std::vector<Offer> const& list);

// One step of an open trade window; see API::trade_with.
Result Step(Player* bot, TradeData* mine, TradeData* his, bool gift_ok);

} // namespace Playerbot::TradeShop

namespace Playerbot {
// Clears bag rot until `min_free` slots are free (defined with API::make_room).
bool MakeBagRoom(Player* bot, uint32 min_free);
}
