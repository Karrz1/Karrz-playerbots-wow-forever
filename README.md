# WoW Forever Bots

**Join our Discord:** https://discord.gg/eJt62dDh54 (help, news, bug reports)

**WoW Forever Bots by Karrz**: bot companions, raid and battleground bots, world bots and an auction house bot for the
WoW Forever (Classic 1.60 beta) TrinityCore server by advocaite.

## Download the repack (Windows)

The ready-made package is on the Releases page: **https://github.com/Karrz1/Karrz-playerbots-wow-forever/releases/latest**

> **Important: unzip into a folder whose path has NO SPACES, for example `C:\WoWForever`.** Not "Downloads", not "Program Files", not a folder with a space in its name. The installer stops if the path has a space.

1. Download **one file**: `WoW-Forever-Bots-<version>.zip` and unzip it into that folder (for example `C:\WoWForever`).
2. Double-click **Install.cmd**. Windows asks once for permission. It looks for your WoW Forever game folder (build 70888, or another build listed in `client-builds.txt`: the folder that contains `.build.info`) and then lets you pick it in a window; it sets up the database, extracts the maps from your own client (the slow part: about 10 to 20 minutes on a modern PC), installs the add-on and a local certificate, creates the shortcuts, starts the server and offers to create your account (name, password, optionally GM).
3. From then on use the **WoW Forever Bots** menu (Start Menu or Desktop, or `WoW-Forever-Bots.cmd`): **1** Start server, **2** Shut down server, **3** Auto-restart (restarts it if it crashes), **4** Update (installs the newest release, keeps your characters), **5** Create account, **6** Join our Discord. Start the game with **WoWForeverLauncher** (the PLAY button), log in with your account, realm Trinity. Always start the game from the launcher: it puts the server key into the game every time.

The package contains no Blizzard files; the maps are made from your own client. `SHA256SUMS.txt` on the release page lets you check the download. Full text: `README.md` inside the zip.

## Launcher

The full zip already contains the launcher (`WoWForeverLauncher.exe`, also started by the Start Menu shortcut "WoW Forever Bots (New)" and `Play-WoWForever.cmd`).
If you already have the game client and just want to play on a server that runs these bots, download the launcher alone from the Releases page:
**https://github.com/Karrz1/Karrz-playerbots-wow-forever/releases/latest/download/WoWForeverLauncher.exe**

It shows the news and patch notes of the latest release, the realm status and a Servers tab (add a friend's server, see `repack/JOIN-A-FRIEND.md` in the zip), and remembers your game folder (the folder that contains `Wow.exe` and `_forever_`). It changes nothing outside that folder; a server that is not on your own PC needs one hosts-file line, which it asks Windows permission for.

## Players: get the add-on

The add-on adds a window with one button per class and role, so you can add a bot to your group, give orders and pull.
It only works on a server that runs these bots (ask the server owner).

1. Download [`addon/ForeverBots-addon.zip`](addon/ForeverBots-addon.zip) (use the Download button on that page) and unzip it,
   or copy the folder [`addon/ForeverBots`](addon/ForeverBots).
2. Put the folder `ForeverBots` into `<your WoW folder>\_forever_\Interface\AddOns\`
   (so that `...\AddOns\ForeverBots\ForeverBots.toc` exists). Do not rename the folder.
3. Start the game. If the AddOns list marks it out of date, tick "Load out of date AddOns". In game type `/reload`, then `/fb`.
4. The `.bot` commands need a GM account on the server, so ask the owner to set that up for you.

This is the only add-on you need: **ForeverBots** is the bot control window and it also handles whispering to your bots (right-click menu entries
Whisper and Invite, and a whisper box that works inside battlegrounds). Nothing else from this repository goes into the game folder.

What the buttons and chat orders do: [`src/modules/PlayerbotV2/BOT_COMMANDS.md`](src/modules/PlayerbotV2/BOT_COMMANDS.md).

## Server owners

Build from the `forever-bots` branch of https://github.com/Karrz1/TrinityCore. It is the full server source with the bots in it,
and it is what the repack is built from. The files in this repository are an older, partial copy: copying them over
advocaite's `forever` does **not** give a working bot build (the module loader and core wiring are missing), so do not use that way.

1. `git clone -b forever-bots https://github.com/Karrz1/TrinityCore.git ForeverCore-bots`
2. Build with `-DBUILD_PLAYERBOT_V2=1` (off by default; without it the server is unchanged), create the shared database from
   `sql/playerbot_shared/`, rename `playerbot.conf.dist` to `playerbot.conf`, start worldserver (it applies `sql/playerbot_v2`).

3. In `worldserver.conf` add `Network.EnterEncryptedModeRegionGroup = 8` (it is not in `worldserver.conf.dist`; without it the game
   says "server identity check failed" or WOW51900319). The client builds the server accepts (70888, 70862 and older) come from
   `sql/custom/auth`, which the server applies at start (`Updates.EnableDatabases = 15`).
4. The game only talks to a server whose certificate it trusts: bnetserver needs a certificate for your portal name that Windows
   (or Wine) trusts, and the portal name must point at the server (hosts line). The repack's Install.cmd does both; on your own build
   you do them yourself. Players start the game with WoWForeverLauncher, which writes the server key and can add the hosts line.
Short guide: [`BOTS.md`](BOTS.md). Step by step: [`INSTALL-BOTS.md`](INSTALL-BOTS.md).

Not Blizzlike extras (solo raid entry, no Molten Core runes, no attunements, open Ahn'Qiraj gates, killed raid trash stays dead) are
settings that are **off** in the stock `worldserver.conf.dist`; [`examples/zz-forever-bots.conf`](examples/zz-forever-bots.conf) turns them on if you copy it
into `worldserver.conf.d`, and [`doc/OptionalServerOptions.md`](doc/OptionalServerOptions.md) describes each one.

The original (1.12) spell, creature and mana numbers are **not** in this repository. [`tools/classic_spell_values/`](tools/classic_spell_values/README.md) builds them
from the VMaNGOS database dump that you download yourself; without them the server uses the client's own values.

## What you need, and where to get it

- **The server code**: the `forever-bots` branch of https://github.com/Karrz1/TrinityCore (advocaite's Forever core, based on TrinityCore, plus the bots). Follow "Server owners" above.
- **The game client**: the WoW Forever client, build **70888** (10 November). 70862 and the other builds in `client-builds.txt` work too. We do not distribute client files.
- **The launcher**: `WoWForeverLauncher.exe` from the Releases page. It writes the server key into the game every time it starts it, so you need it for every server, also one you build yourself. It can also add the hosts line for a server on another PC.
- **Vanilla (1.12) data**: the VMaNGOS project (https://github.com/vmangos/core) and its public database dump `world_full_14_june_2021` (https://github.com/brotalnia/database). You download it yourself; `tools/classic_spell_values/build_vanilla_data.py` turns it into the SQL files the server uses. Nothing from the dump is in this repository.
- **Maps**: extracted from your own client with the tools in this repository (`tools/extract_client_data.sh` or `.ps1`).
- **Ready-made Windows package (repack)**: https://github.com/Karrz1/Karrz-playerbots-wow-forever/releases - the same server we run, with an **Update** button (`Update-WoWForever.cmd`) that installs each new release and keeps your characters. The repack also contains the vanilla data already built from the VMaNGOS database (credit: the VMaNGOS project, GPL-2.0; see [`NOTICE.md`](NOTICE.md)).
- **The add-on**: `ForeverBots-addon.zip` in this repository, also inside the repack.

## Run it like Karrz's realm

Anyone can build the server Karrz plays on. These are the steps in order; the realm's own PC scripts, logins and certificates are not part of this repository.

1. **Build.** Clone `forever-bots` (see "Server owners"), then in that folder `cmake -S . -B build -DSERVERS=1 -DTOOLS=1 -DSCRIPTS=static -DBUILD_PLAYERBOT_V2=1`, then build `worldserver`, `bnetserver` and the tools. Details: [`INSTALL-BOTS.md`](INSTALL-BOTS.md).
2. **Vanilla data.** Download the VMaNGOS world dump (`world_full_14_june_2021`) yourself and run `python3 tools/classic_spell_values/build_vanilla_data.py --dump <file>` (that tool is in this repository). It writes the vanilla (1.12) spell, creature and boss numbers as SQL files that the server applies at start. See [`tools/classic_spell_values/README.md`](tools/classic_spell_values/README.md).
3. **Maps.** Extract the maps, vmaps and mmaps from your own client with the tools built in step 1: `tools/extract_client_data.sh` (Linux) or `tools/extract_client_data.ps1` (Windows) runs `mapextractor`, `vmap4extractor`, `vmap4assembler` and `mmaps_generator` in order. Put the result in `DataDir`. No map files are included, and the map format is the stock TrinityCore one. The mmaps (pathfinding data) are REQUIRED for the bots: without them they cannot walk, and the server logs "Could not load MMAP ... result: 2". The full run takes about 10 to 20 minutes on a modern PC and about 45 GB of free disk while it works (about 2 GB are left); the Windows script can be started again after an interruption with `-Resume`. Never copy someone else's mmaps: they are made from your own client.
4. **Config.** Create the bot database (`INSTALL-BOTS.md`, step 3), rename `playerbot.conf.dist` to `playerbot.conf` in `worldserver.conf.d`, and copy [`examples/karrz-realm.conf`](examples/karrz-realm.conf) from this repository into the same folder. It holds the realm's world bots and 50/50 faction balance, the battleground rules, the raid switches (solo raid entry, saved on entry, trash stays dead, no Molten Core runes, no attunements, open Ahn'Qiraj gates), the auction house bot and the logging. Lower the bot numbers in it on a smaller machine. The first start creates a few thousand roster characters, which takes about two hours.
5. **Add-on.** Copy `src/modules/PlayerbotV2/Addon/ForeverBots` into `<WoW folder>/_forever_/Interface/AddOns/` (the public repository also has it ready as `ForeverBots-addon.zip`). Type `/fb` in game.

## Credits and licenses

Karrz (Karrz1): WoW Forever Bots. agatho: PlayerbotV2, the base of the bot module. advocaite: the Forever
TrinityCore core. TrinityCore: the server. Also credited in [`NOTICE.md`](NOTICE.md): ike3 / cmangos playerbots, VMaNGOS, cMaNGOS,
mod-playerbots (AzerothCore, reference only), SimulationCraft.
License: GPL-2.0 ([`LICENSE`](LICENSE)), like TrinityCore. Details in [`NOTICE.md`](NOTICE.md) and [`BOTS.md`](BOTS.md).
