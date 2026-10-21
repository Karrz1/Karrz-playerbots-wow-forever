WoW Forever Bots 0.1.13 (one-click setup from the launcher, healer and battleground fixes)

Matches the main realm build 132m. Needs the WoW Forever client build 70450, same as 0.1.12.

**One-click setup**
- Run WoWForeverLauncher.exe and it sets everything up in its own window: one permission prompt, the installer's progress shown in the window, then it asks for your account name and password. Your login is the account name plus @local (for example yourname@local), realm Trinity. The first account you make is always a game master.
- If the add-on ClassicForever_Bots (made for another server) is in your client, the setup tells you to untick it; it prints a harmless error here. It is not touched.

**Bots**
- Healers count percent mana costs, drop to a lower rank they can pay for, and use Holy Shock only within its range.
- Battlegrounds: bots pick up buffs by walking to them, shamans retry weapon imbues and fall back to a lower rank, and PvP reactions are a little quicker.
- Ghost runs are no longer refused for a corpse in deep water or magma, and bots no longer pick a walk goal inside magma or slime.
- A bot never keeps a spell of another class, bots in a starter zone get the right gear and appearance, and the Group Finder fill knows every dungeon and raid.
- Standing at an altar or object ends when the group's fight starts.
- The auction house bot writes one summary line every ten minutes.

**Install and update**
- Update from an earlier release with the Update button (menu U) or the launcher. To install: download WoW-Forever-Bots-0.1.13.zip, unzip it into a folder without spaces in its path (for example C:\WoWForever) and run WoWForeverLauncher.exe, or double-click Install.cmd. wow-forever-update-0.1.13.zip is what the Update button downloads. wow-forever-source-0.1.13.zip is the complete source (GPL v2). WoWForeverLauncher.exe is the launcher alone. Checksums: SHA256SUMS.txt.
- The first install builds the bot pathing from your own game files; it takes about 10 to 20 minutes on a modern PC.
