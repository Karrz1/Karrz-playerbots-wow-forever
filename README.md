# WoW Forever Bots

**Join our Discord:** https://discord.gg/Jk596N3xun (help, news, bug reports)

**WoW Forever Bots by Karrz**: bot companions, raid and battleground bots, world bots and an auction house bot for the
WoW Forever (Classic 1.60 beta) TrinityCore server by advocaite.

## Download the repack (Windows)

The ready-made package is on the Releases page: **https://github.com/Karrz1/Karrz-playerbots-wow-forever/releases/latest**

> **Important: unzip into a folder whose path has NO SPACES, for example `C:\WoWForever`.** Not "Downloads", not "Program Files", not a folder with a space in its name. The installer stops if the path has a space.

1. Download **one file**: `WoW-Forever-Bots-<version>.zip` and unzip it into that folder (for example `C:\WoWForever`).
2. Double-click **Install.cmd**. Windows asks once for permission. It looks for your WoW Forever game folder (build 70450, the folder that contains `.build.info`) and then lets you pick it in a window; it sets up the database, extracts the maps from your own client (the slow part: 30 minutes to a few hours), installs the add-on and a local certificate, creates the shortcuts, starts the server and offers to create your account (name, password, optionally GM).
3. From then on use the **WoW Forever Bots** menu (Start Menu or Desktop, or `WoW-Forever-Bots.cmd`): **1** Start server, **2** Shut down server, **3** Auto-restart (restarts it if it crashes), **4** Update (installs the newest release, keeps your characters), **5** Create account, **6** Join our Discord. Start the game from advocaite's ForeverLauncher, log in with your account, realm Trinity.

The package contains no Blizzard files; the maps are made from your own client. `SHA256SUMS.txt` on the release page lets you check the download. Full text: `README.md` inside the zip.

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

## What you need, and where to get it

- **The server code**: this repository holds our changes on top of advocaite's `forever` branch (https://github.com/advocaite/TrinityCore/tree/forever, commit `440d365e`, based on TrinityCore). Follow "Server owners" above.
- **The game client**: the WoW Forever (Classic beta) client, build **70450**, and advocaite's launcher that applies the server key: https://github.com/advocaite/foreverlauncher. Both come from advocaite's Forever project; we do not distribute client files.
- **Vanilla (1.12) data**: the VMaNGOS project (https://github.com/vmangos/core) and its public database dump `world_full_14_june_2021` (https://github.com/brotalnia/database). You download it yourself; `tools/classic_spell_values/build_vanilla_data.py` turns it into the SQL files the server uses. Nothing from the dump is in this repository.
- **Maps**: extracted from your own client with the tools in this repository (`tools/extract_client_data.sh` or `.ps1`).
- **Ready-made Windows package (repack)**: https://github.com/Karrz1/Karrz-playerbots-wow-forever/releases - the same server we run, with an **Update** button (`Update-WoWForever.cmd`) that installs each new release and keeps your characters. The repack also contains the vanilla data already built from the VMaNGOS database (credit: the VMaNGOS project, GPL-2.0; see [`NOTICE.md`](NOTICE.md)).
- **The add-on**: `ForeverBots-addon.zip` in this repository, also inside the repack.

## Run it like Karrz's realm

Anyone can build the server Karrz plays on. These are the steps in order; the realm's own PC scripts, logins and certificates are not part of this repository.

1. **Build.** `cmake -S . -B build -DSERVERS=1 -DTOOLS=1 -DSCRIPTS=static -DBUILD_PLAYERBOT_V2=1`, then build `worldserver`, `bnetserver` and the tools. Details: [`INSTALL-BOTS.md`](INSTALL-BOTS.md).
2. **Vanilla data.** Download the VMaNGOS world dump (`world_full_14_june_2021`) yourself and run `python3 tools/classic_spell_values/build_vanilla_data.py --dump <file>`. It writes the vanilla (1.12) spell, creature and boss numbers as SQL files that the server applies at start. See [`tools/classic_spell_values/README.md`](tools/classic_spell_values/README.md).
3. **Maps.** Extract the maps, vmaps and mmaps from your own client with the tools built in step 1: `tools/extract_client_data.sh` (Linux) or `tools/extract_client_data.ps1` (Windows) runs `mapextractor`, `vmap4extractor`, `vmap4assembler` and `mmaps_generator` in order. Put the result in `DataDir`. No map files are included, and the map format is the stock TrinityCore one. The mmaps (pathfinding data) are REQUIRED for the bots: without them they cannot walk, and the server logs "Could not load MMAP ... result: 2". The full run takes a few hours and about 45 GB of free disk while it works (about 2 GB are left); the Windows script can be started again after an interruption with `-Resume`. Never copy someone else's mmaps: they are made from your own client.
4. **Config.** Create the bot database (`INSTALL-BOTS.md`, step 3), rename `playerbot.conf.dist` to `playerbot.conf` in `worldserver.conf.d`, and copy [`examples/karrz-realm.conf`](examples/karrz-realm.conf) into the same folder. It holds the realm's world bots and 50/50 faction balance, the battleground rules, the raid switches (solo raid entry, saved on entry, trash stays dead, no Molten Core runes, no attunements, open Ahn'Qiraj gates), the auction house bot and the logging. Lower the bot numbers in it on a smaller machine. The first start creates a few thousand roster characters, which takes about two hours.
5. **Add-on.** Copy `src/modules/PlayerbotV2/Addon/ForeverBots` into `<WoW folder>/_classic_beta_/Interface/AddOns/` (the public repository also has it ready as `ForeverBots-addon.zip`). Type `/fb` in game.

## Credits and licenses

Karrz (Karrz1): WoW Forever Bots. agatho: PlayerbotV2, the base of the bot module. advocaite: the Forever
TrinityCore core. TrinityCore: the server. Also credited in [`NOTICE.md`](NOTICE.md): ike3 / cmangos playerbots, VMaNGOS, cMaNGOS,
mod-playerbots (AzerothCore, reference only), SimulationCraft.
License: GPL-2.0 ([`LICENSE`](LICENSE)), like TrinityCore. Details in [`NOTICE.md`](NOTICE.md) and [`BOTS.md`](BOTS.md).
