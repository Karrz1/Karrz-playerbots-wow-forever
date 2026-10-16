WoW Forever Bots 0.1.8 (Lucifron and Onyxia fight plans, raid pull wait, walking fixes)

Matches the main realm build. Adds a fight plan for Lucifron, a more careful Onyxia plan, a longer raid pull wait and more walking fixes for the bots.

**Raids**
- Lucifron: one plan for the whole fight. A main tank holds Lucifron, two more tanks take his Protectors, the damage dealers wait a few seconds at the start and then kill the Protectors first and Lucifron after, and your marks (skull, cross) always win over that order. In our test group the fight was won with nobody dying.
- Onyxia: the raid keeps out of her Flame Breath cone and her tail arc (the game's real 120 degree cones), dodges the Deep Breath corridor, stays off the burning floor, and warrior tanks use Berserker Rage when she fears.
- Raid pull wait is now 8 seconds. After your Pull the damage dealers wait for the tank to reach the monster (or for your Attack); in dungeons the wait stays shorter.

**World and walking**
- Bots use checked walking links between parts of the map that were cut off from each other, and give up quest goals they cannot reach instead of standing still.
- Town life: bots rest in inns, visit mailboxes and no longer pile up in the same spot.
- Includes the latest data from the upstream Forever branch. The realm stays on game client build 70374, so your client does not need to change.

**Install and update**
- Update from an earlier release with the Update button (menu U). To install: download WoW-Forever-Bots-0.1.8.zip, unzip it into a folder without spaces in its path (for example C:\WoWForever) and double-click Install.cmd. wow-forever-update-0.1.8.zip is what the Update button downloads. wow-forever-source-0.1.8.zip is the complete source (GPL v2). Checksums: SHA256SUMS.txt.
