WoW Forever Bots 0.1.10 (Upper Blackrock Spire, summon into instances, ready checks)

Matches the main realm build. Needs the WoW Forever client build 70450, same as 0.1.9.

**Dungeons and raids**
- Upper Blackrock Spire: the door opens when you come close carrying the Seal of Ascension, the rune-room event starts as you walk in so its door opens, the altar needs three players to channel (with the channel bar and beam), and the Rookery eggs hatch as in vanilla.
- Bots answer a ready check before anything else, dead ones too. Raid ready uses your current group and never replaces it.
- Come, summon, summon <name> and summon all bring a bot to you inside an instance too: a bot counts as arrived only once it stands beside you, and bots fighting elsewhere leave the fight. A bot enters the instance its group's human stands in, and bots may enter a raid while a boss fight is going on.
- Majordomo: tanks pick up the adds, mages sheep the targets you mark (and renew a sheep before it runs out), damage goes only on skull, and the bots stay clear of the hall's lava and Magic Reflection.

**Bots**
- Companions follow you off a ledge: they walk to the nearest edge and drop or leap down to your floor when the way around is long or missing.
- Your saved companions are kept per character.
- Lazy lock: `.playerbot lazy` takes no argument, and a refused spell no longer stalls the rotation.

**World**
- The Elwynn road now continues to Sentinel Hill, and bots use three more checked walking links.
- Seasonal world events: spawns without an equipment set no longer cause errors.

**Install and update**
- The installer and the menu start their own database by its port and wait for it, so they work on a PC that already runs another MySQL.
- Update from an earlier release with the Update button (menu U). To install: download WoW-Forever-Bots-0.1.10.zip, unzip it into a folder without spaces in its path (for example C:\WoWForever) and double-click Install.cmd. wow-forever-update-0.1.10.zip is what the Update button downloads. wow-forever-source-0.1.10.zip is the complete source (GPL v2). Checksums: SHA256SUMS.txt.
