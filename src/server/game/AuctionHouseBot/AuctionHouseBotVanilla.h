/*
 * Vanilla-era item filter for the AuctionHouseBot seller (Forever / classic data).
 * Own file so it merges cleanly; the seller only calls AuctionBotVanilla::IsAllowed.
 */

#ifndef AUCTION_HOUSE_BOT_VANILLA_H
#define AUCTION_HOUSE_BOT_VANILLA_H

struct ItemTemplate;

namespace AuctionBotVanilla
{
    // Reads AuctionHouseBot.Vanilla.* from worldserver.conf (call from seller Initialize).
    void LoadConfig();

    // True when the filter is switched on (AuctionHouseBot.Vanilla.Only = 1).
    bool Enabled();

    // False for anything that is not stock level-60 vanilla content.
    bool IsAllowed(ItemTemplate const* proto);
}

#endif
