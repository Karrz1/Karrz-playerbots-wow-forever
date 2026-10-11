# Karrz's Player Bots

**Karrz's Player Bots by Karrz**, for WoW Forever. Built on agatho's PlayerbotV2. If you reuse these bots, keep this name, the credits below and the license notices.

This is the bot system for advocaite's TrinityCore `forever` tree (this repository holds the bot files and the files of that tree they change; see the README for how to put them together): party companions you add with
one click, raid / dungeon / battleground bots, an optional ambient population of bots around
real players, and a vanilla-style auction house bot. It is shared as it stands, so you can read
the code and the settings, try it, and take what is useful. Nothing here is a finished release.

Base: `advocaite/TrinityCore` branch `forever` at `fb04ef10`. Copy this repository over a checkout of that commit.

## What is in it

| Part | Where |
|---|---|
| Bot module (AI, raid brain, BG scripts, world bots, fleet, travel) | `src/modules/PlayerbotV2/` |
| Core side: a small API plus hooks | `src/server/game/Playerbot/`, `src/server/game/Modules/`, `src/server/scripts/Commands/cs_playerbot_v2.cpp`, hooks in login, group, chat, spell, threat, BG/LFG queue, map, session |
| Bot database migrations and seed data | `sql/playerbot_v2/`, `sql/playerbot_shared/` |
| Settings | `src/modules/PlayerbotV2/conf/playerbot.conf.dist` (every key documented), `contrib/forever-bots/zz-forever-bots.conf` (recommended values) |
| In-game window (addon) | `contrib/forever-bots/addon/ForeverBots/` |
| Everything players can type or click | `src/modules/PlayerbotV2/BOT_COMMANDS.md` |

Besides the module, this tree carries Classic changes the bots depend on or were tested with:
vanilla auction house seller (`src/server/game/AuctionHouseBot/`), road-aware pathing
(`HandcraftedRoad*`, `dtQueryFilterTC`, `RoadOverrides`), Classic battleground scripts
(`src/server/scripts/Custom/Classic/battlegrounds/`), vanilla spell values and some Classic
data fixes (`sql/custom/world/2026_10_*`), and several smaller core fixes. Every file here that also exists in the `forever` tree carries the change. Behaviour changes that are a matter of taste are behind switches that default to off:

- `Classic.ArmorFormula = 0` (worldserver.conf): `1` uses the vanilla armor formula
  `armor / (armor + 400 + 85 * attacker level)`, capped at 75%.
- `Classic.FiveSecondRule = 0`: `1` uses the vanilla five second rule for mana regeneration.

## Build

The module is only built with `-DBUILD_PLAYERBOT_V2=1` (default `0`). Without it the hooks
compile to empty inlines and `WorldSession::IsBot()` is a constant `false`.

```
cmake -S . -B build -DSERVERS=1 -DSCRIPTS=static -DBUILD_PLAYERBOT_V2=1
cmake --build build --config RelWithDebInfo --target worldserver
```

Status: this snapshot was assembled by applying our changes onto your current `forever` head
and has not been compiled in that exact combination. Our own tree builds and passes its tests
on Windows (Visual Studio 2022, MySQL 8.4). If it does not build for you, the error
list is the most useful feedback we can get.

## Enable

1. Configure with `-DBUILD_PLAYERBOT_V2=1` as above.
2. Create the shared database and seed it (once):
   ```
   mysql -u<user> -p < sql/playerbot_shared/playerbot_shared_schema.sql
   mysql -u<user> -p playerbot < sql/playerbot_shared/playerbot_shared_data.sql
   ```
3. Rename the module config the build copies into `worldserver.conf.d` from
   `playerbot.conf.dist` to `playerbot.conf`. Optionally copy `contrib/forever-bots/zz-forever-bots.conf`
   there too (recommended values; it is read after `playerbot.conf`).
4. Start worldserver. `sql/playerbot_v2/*.sql` migrations are applied on boot to the characters
   and `playerbot` databases (back up `characters` first). If the binary runs on a machine
   without the source tree, set `SourceDirectory` in `worldserver.conf`.
5. Add `Logger.playerbot.v2=3` to see the module's log lines (written to `Playerbot.log`).
6. Install the addon: copy `contrib/forever-bots/addon/ForeverBots` into `Interface/AddOns/`, and
   make your account a GM account for the `.bot` commands.

The server prints "Karrz's Player Bots ..." at start-up, `.bot about` shows the same line, and the in-game addon (folder `ForeverBots`, slash command `/fb`) is titled "Karrz's Player Bots".

Step by step: `INSTALL-BOTS.md`. More: `src/modules/PlayerbotV2/INSTALL.md`, `WORLD_BOTS.md`.

## Main config keys (all in `playerbot.conf`)

| Key | Default | What it does |
|---|---|---|
| `Playerbot.SharedDatabase` | `playerbot` | name of the shared database |
| `Playerbot.AiWorkerThreads` | `0` | AI worker threads (0 = automatic) |
| `Playerbot.World.Enable` | `0` | ambient bots around real players |
| `Playerbot.World.MaxBots` / `BotsPerPlayer` / `ActivityRadius` | `100` / `25` / `300` | size and reach of that population |
| `Playerbot.World.RssBudgetMb` | `0` | memory cap for the spawner (0 = off) |
| `Playerbot.Bg.AutoJoin` / `Bg.AutoSeed.*` | `0` | bots queue for and fill battlegrounds |
| `Playerbot.Raid.Enable` | `1` | raid brain; only acts for bot groups |
| `Playerbot.Cast.Enable` | `0` | human-like cast speed per context |
| `Playerbot.Behavior.*` | see file | individual behaviours (formation, pull back, spreading, ...) |

Without a player summoning bots nothing spawns, and with `World.Enable = 0` no ambient bots exist.

## Credits

- **Karrz (Karrz1)**, with Claude Code: Karrz's Player Bots, i.e. the Classic adaptation, raid, battleground, world
  population and auction house work, and the Classic data and core fixes in this tree.
- **agatho**: PlayerbotV2, the base of the module (https://github.com/agatho/TrinityCore, branch `playerbot-v2`).
- **advocaite**: the Forever TrinityCore core this is built on.
- **ike3 / cmangos playerbots**, **VMaNGOS** and **cMaNGOS**: behaviour and data that were ported (see `NOTICE.md`).
- **mod-playerbots (AzerothCore)**: read as a reference for behaviour ideas only; no code copied.
- **SimulationCraft**: reference for rotation and talent data.

## Licenses

- This tree, the bot module and the core changes: **GPL-2.0** (`COPYING`), like TrinityCore.
- agatho's PlayerbotV2: GPL-2.0. ike3 / cmangos playerbots, VMaNGOS, cMaNGOS: GPL-2.0.
- mod-playerbots (AzerothCore): AGPL-3.0. Reference only; none of its code is in this tree.
- SimulationCraft: GPL-3.0. Data and action lists used as reference (`sql/playerbot_v2/0004_simc_seed.sql`).
- Third-party libraries under `dep/` keep their own licenses.
- World of Warcraft data, names and client files belong to Blizzard Entertainment and are not
  included.

See `src/modules/PlayerbotV2/NOTICE.md` for details.
