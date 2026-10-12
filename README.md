# WoW Forever Bots

**WoW Forever Bots by Karrz**: bot companions, raid and battleground bots, world bots and an auction house bot for the
WoW Forever (Classic 1.60 beta) TrinityCore server by advocaite.

## Players: get the add-on

The add-on adds a window with one button per class and role, so you can add a bot to your group, give orders and pull.
It only works on a server that runs these bots (ask the server owner).

1. Download [`addon/ForeverBots-addon.zip`](addon/ForeverBots-addon.zip) (use the Download button on that page) and unzip it,
   or copy the folder [`addon/ForeverBots`](addon/ForeverBots).
2. Put the folder `ForeverBots` into `<your WoW folder>\_classic_beta_\Interface\AddOns\`
   (so that `...\AddOns\ForeverBots\ForeverBots.toc` exists). Do not rename the folder.
3. Start the game. If the AddOns list marks it out of date, tick "Load out of date AddOns". In game type `/reload`, then `/fb`.
4. The `.bot` commands need a GM account on the server, so ask the owner to set that up for you.

This is the only add-on you need: **ForeverBots** is the bot control window and it also handles whispering to your bots (right-click menu entries
Whisper and Invite, and a whisper box that works inside battlegrounds). Nothing else from this repository goes into the game folder.

What the buttons and chat orders do: [`src/modules/PlayerbotV2/BOT_COMMANDS.md`](src/modules/PlayerbotV2/BOT_COMMANDS.md).

## Server owners

This repository contains the bot module, the SQL, the settings, and the files of advocaite's TrinityCore `forever` branch that the
bots change. To use it:

1. Check out advocaite's `forever` at commit `440d365e` (https://github.com/advocaite/TrinityCore).
2. Copy the contents of this repository over that checkout (same folder layout, overwrite existing files).
3. Build with `-DBUILD_PLAYERBOT_V2=1` (off by default; without it the server is unchanged), create the shared database from
   `sql/playerbot_shared/`, rename `playerbot.conf.dist` to `playerbot.conf`, start worldserver (it applies `sql/playerbot_v2`).

Short guide: [`BOTS.md`](BOTS.md). Step by step: [`INSTALL-BOTS.md`](INSTALL-BOTS.md).

Not Blizzlike extras (solo raid entry, no Molten Core runes, no attunements, open Ahn'Qiraj gates, killed raid trash stays dead) are
settings that are **off** in the stock `worldserver.conf.dist`; [`examples/zz-forever-bots.conf`](examples/zz-forever-bots.conf) turns them on if you copy it
into `worldserver.conf.d`, and [`doc/OptionalServerOptions.md`](doc/OptionalServerOptions.md) describes each one.

The original (1.12) spell, creature and mana numbers are **not** in this repository. [`tools/classic_spell_values/`](tools/classic_spell_values/README.md) builds them
from the VMaNGOS database dump that you download yourself; without them the server uses the client's own values.

## Credits and licenses

Karrz (Karrz1), with Claude Code: WoW Forever Bots. agatho: PlayerbotV2, the base of the bot module. advocaite: the Forever
TrinityCore core. TrinityCore: the server. Also credited in [`NOTICE.md`](NOTICE.md): ike3 / cmangos playerbots, VMaNGOS, cMaNGOS,
mod-playerbots (AzerothCore, reference only), SimulationCraft.
License: GPL-2.0 ([`LICENSE`](LICENSE)), like TrinityCore. Details in [`NOTICE.md`](NOTICE.md) and [`BOTS.md`](BOTS.md).
