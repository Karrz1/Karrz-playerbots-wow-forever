/*
 * Blizzlike buying rules for the AuctionHouseBot buyer (Forever / classic data).
 * Own file so it merges cleanly; the buyer only asks it three questions and reports purchases.
 */

#ifndef AUCTION_HOUSE_BOT_BUYER_POLICY_H
#define AUCTION_HOUSE_BOT_BUYER_POLICY_H

#include "Define.h"
#include <string>

struct AuctionPosting;

namespace AuctionBotBuyerPolicy
{
    void LoadConfig();

    // Buyer.Commodities: stackable listings (herbs, ore, cloth...) can be bought whole.
    bool CommoditiesEnabled();

    // False while the listing is still "too fresh" (a stable per-listing delay between
    // Buyer.MinDelay and Buyer.MaxDelay seconds) or an hourly/daily limit is spent.
    bool Eligible(AuctionPosting const* auction);

    // Buyer.FairPricing: roll by fair value instead of the stock falloff (see RollFair).
    bool FairPricingEnabled();

    // Fair-price roll for a buyout or bid at unitPrice: certain up to the fair price, then falling
    // linearly to Buyer.FairMinChance percent at the price cap, never above the cap.
    // qualityChance is the stock Buyer.ChanceMultiplier.<quality> value (100 = unchanged).
    bool RollFair(AuctionPosting const* auction, uint64 unitPrice, uint32 qualityChance);

    // Highest price per unit (or per item for single items) the bot pays; 0 = no cap.
    uint64 MaxUnitPrice(AuctionPosting const* auction);

    // Total price of buying the whole listing at the given unit/bid price.
    uint64 TotalPrice(AuctionPosting const* auction, uint64 unitPrice);

    // Logs "[ahbot] buy ..." and updates the hourly/daily counters.
    void RecordBuy(AuctionPosting const* auction, uint64 totalPrice, char const* house);

    // "bought X items for Y copper today" for .ahbot stats
    std::string Describe();
}

#endif
