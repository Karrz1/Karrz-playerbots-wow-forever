# Notice and credits: WoW Forever Bots

"WoW Forever Bots" by Karrz is the name of this bot system. Every upstream credit and license below stays in force.

This module is part of a TrinityCore-based tree and is distributed under the GNU General
Public License version 2 (`COPYING` at the repository root), the same terms as TrinityCore.

- **PlayerbotV2** was written by agatho (https://github.com/agatho/TrinityCore, branch
  `playerbot-v2`), whose README states "GPL-2.0, same as TrinityCore". The core API, the
  module layout and most of the bot brain, travel and fleet code come from there.
- **Classic (1.12) adaptation, raid, battleground, world-population and auction house work**
  was added for WoW Forever by Karrz (Karrz1).
- **ike3 / cmangos playerbots** (ike3's mangosbot, https://github.com/ike3/mangosbot-bots, for cMaNGOS; GPL-2.0): several behaviours
  (raid brain, speech lines, rpg, battleground paths and objectives, travel) were ported from a
  VMaNGOS-based bot tree that derives from ike3 bots, VMaNGOS and cMaNGOS (all GPL-2.0).
  Source comments name the routines they follow.
- **mod-playerbots** (AzerothCore, AGPL-3.0) was read as a reference for behaviour ideas
  (for example dungeon dispersal and fleeing rules). Its code was not copied; the logic was
  re-written here. Comments mark where an idea came from.
- **Vanilla encounter data and scripts** follow VMaNGOS (GPL-2.0), as do the Classic ports
  already in `src/server/scripts/Custom/Classic`.
- **SimulationCraft (simc)** (GPL-3.0) action lists and talent loadouts were used as
  reference for the retail-era rotations and `sql/playerbot_v2/0004_simc_seed.sql`.

## Licence of each part

**Our own work in the bot module (by Karrz / Karrz1) is licensed GPL-2.0-or-later.** Code that came
from others keeps its original licence, as listed here:

- **From agatho's PlayerbotV2 (GPL-2.0, "same as TrinityCore"):** in `src/modules/PlayerbotV2/`,
  the bot API files `Api/PlayerbotAPI`, `Api/PlayerbotMovement` and `Api/BotItemScorer` (moved here
  from agatho's `src/server/game/Playerbot/`), most of `Threading/`,
  `Persistence/`, `World/`, `Group/`, `Session/`, the module's own `sql/`, and the original files of
  `Bot/`, `Combat/`, `Fleet/`, `Travel/` and `Diagnostics/`, plus the first 18 files of
  `sql/playerbot_v2/`. A file that existed in agatho's `playerbot-v2` branch stays under agatho's
  terms, even where we have changed it since.
- **Ported from ike3 / cMaNGOS playerbots, by way of the VMaNGOS-based bot tree (GPL-2.0):** the
  Classic raid brain in `Bot/Raid/`, battleground paths and objectives in `Bot/Battleground/`,
  rpg and idle states in `Bot/States/`, the Classic class rotations in `Combat/Apl/Apl_Classic_*`,
  bot speech lines (`Bot/BotTextMgr`), and parts of travel (`Travel/PortalIndex`,
  `Travel/TownNpcIndex`, `Travel/UnifiedTravelGraph`). Source comments name the routine each one
  follows.
- **By SIN (GPL-2.0, `COPYING` in its folder):** the add-on `Addon/ForeverCombatNameplates`.
- **Ours from the start:** the add-ons `Addon/ForeverBots` and `Addon/ForeverDots`, `Commands/`,
  the rest of `Api/`, and every file not named above that was added for WoW Forever.

Keep these notices, and the copyright and licence headers that individual files carry, when
redistributing. Source files of the module do not carry per-file headers; this notice and
`COPYING` apply to all of them.
