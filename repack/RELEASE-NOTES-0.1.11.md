WoW Forever Bots 0.1.11 (new launcher, new add-on window, livelier world bots)

Matches the main realm build 132h. Needs the WoW Forever client build 70450, same as 0.1.10.

**New launcher**
- WoWForeverLauncher.exe: a window with news and patch notes from this page, realm status, a Browse button for your game folder and a Settings tab. It is inside the full zip and also a separate download below, for players who already have the game client and no repack.
- Join a friend's server: the Add server window and a how-to (JOIN-A-FRIEND.md) explain the host steps.

**New add-on window (add-on 0.5.12)**
- A client-style window with side tabs: Orders, Formation, Bots and Assign. Class and role icons, a minimap button, key bindings kept, and the stance is sent again after a zone change.

**World bots**
- Bots grind levels instead of questing by default; each bot walks to its own spot beside a quest giver instead of piling up on one; starter bots only go to hubs with a quest they can take.
- Pairs of bots duel outside the capital gates, and bots duel players.
- Bots of level 40 and up ride on long walks (75 percent speed from 40, 150 from 60) and learn their race's mount when they level past 40 or 60.
- A fuller starter outfit, balanced bot pool at every level, and bots in a group with you follow your mount and dismount with you.

**Bots and groups**
- Group Finder: bots fill a posted group's missing roles after a wait (bots come at once when nobody is available), up to the dungeon's size, as a raid when it is bigger than 5.
- Companions obey the group leader, follow at the right distance, are let go cleanly when removed from a group, and do not loot while you are there to loot.
- Passive and Aggressive also apply in dungeon trash; Defensive now means Passive.
- Craft orders cast the exact recipe rank, conjured and unique items are handed over by trade, and trades of recipe reagents count as gifts.
- Rotations: Heroic Strike, Sunder Armor and mage nukes wait until the target is in range; Hunters respect the minimum range; Feint is cast on the mob.

**Since the last test build**
- Bots found guilds and recruit members, and the guild tabs show up right.
- Group Finder: searching no longer crashes the game when bots have posted groups.
- Bots walk smoother routes around buildings and stand, sit and camp less often while on the move.
- Shared quest drops reach every group member who needs them, low-level hunters keep shooting, and warlock imps are summoned correctly on login.
- Starting zones: 50 bots with no day rhythm, a camp of 10 and residents placed around you, so a new character does not land in an empty zone.
- Hunters: an Auto Shot that is not ready yet is no longer counted as a refusal.
- Healers no longer overlap on the same target, melee bots wait for the tank before chasing an idle mob in an instance, and the altar's Stay ends when the fight starts.
- Support tickets (bug, complaint, suggestion) no longer crash the server.
- The add-on sends nothing from timers or events through chat.
- Creature armor and dungeon bot fixes from the main realm.

**Fixes**
- Invites by name and from the portrait menu work again on client 70450.
- Pathing: bots at the end of the list no longer stall, and bots that get stuck in the world report why.
- Community fixes merged: mining veins looted several times, starter gathering loot, gossip text conditions.
- Option to give every account the secured-account backpack slots.

**Install and update**
- The first install (and the first start after an update, if needed) builds the bot pathing automatically for every map from your own game files; it takes about 10 to 20 minutes on a modern PC, and the server starts when it is done. If it is interrupted, start again and it continues.
- Before you update from 0.1.10 or older, copy your backups folder somewhere safe (or delete the old pre-0.1.5 folders in it). The old updater deleted the backup it had just made; this release fixes that, but the old updater is the one that runs this time.
- Update from an earlier release with the Update button (menu U). To install: download WoW-Forever-Bots-0.1.11.zip, unzip it into a folder without spaces in its path (for example C:\WoWForever) and double-click Install.cmd. wow-forever-update-0.1.11.zip is what the Update button downloads. wow-forever-source-0.1.11.zip is the complete source (GPL v2). WoWForeverLauncher.exe is the launcher alone. Checksums: SHA256SUMS.txt.
