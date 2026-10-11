/*
 * Seller characters for the AuctionHouseBot (Forever / classic data).
 * Own file so it merges cleanly; AuctionBotConfig::Initialize calls EnsureCharacters once.
 */

#ifndef AUCTION_HOUSE_BOT_SELLERS_H
#define AUCTION_HOUSE_BOT_SELLERS_H

#include "Define.h"

namespace AuctionBotSellers
{
    // AuctionHouseBot.Sellers.Count > 0 and an account id set: makes sure that account owns that many
    // named level-60 characters (bare rows in the characters table; nobody ever logs them in), so listings
    // show a real seller name. Returns how many characters it created.
    uint32 EnsureCharacters(uint32 accountId);
}

#endif
