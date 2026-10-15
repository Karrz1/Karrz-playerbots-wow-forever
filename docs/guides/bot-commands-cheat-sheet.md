# Bot Commands Cheat Sheet

## Part 1 of 3: How to give orders

Only the **owner** of a bot can order it. Orders are plain words, not case-sensitive.

### Three ways to order
- **Party or raid chat**: type the word, e.g. `follow`. Every bot of yours in the group acts.
- **Whisper one bot**: `/w BotName follow`. Only that bot acts and it whispers back to confirm. Right-click the bot and choose **Whisper** works too.
- **Buttons**: **/fb** sends the same words for you.

### Move and stance (party/raid chat)
`follow` · `stay` · `hold` (stay but fight what attacks) · `resume` (act on their own again) · `come` (bot teleports to you) · `flee` (run back to you) · `passive` (stay close, defend you, do as told) · `defensive` · `aggressive` (may attack on their own) · `disengage` (stop attacking)

### Fighting (party/raid chat)
`attack` (everyone attacks your target at once) · `assist` (same as attack) · `pull` (tank pulls, damage dealers wait for your attack or join after 6 s) · `pull back` (same, tank brings the monster to you) · `fall back` · `back` · `rally here` (set rally point)

### After a wipe
`release` (dead bots release and run to their bodies)

### Questions the bots answer
`who` · `help` · `stats` · `gear` (see worn gear) · `summon` (bring the bot to you)

---

## Part 2 of 3: Marks, formations and more

### Marks and tanks (party/raid chat or whisper)
`assign <name> <icon> [tank|cc|kill]`: give a bot a raid icon. `assign clear` removes it. `assign list` shows who has what.  
`tank <name> <icon>` · `clear tanks` · `main tank <name>` or `set main tank <name>` (no name = your target; raids only)  
Icons: star, circle, diamond, triangle, moon, square, cross, skull.

### Formation and spacing
`formation circle|arrow|line|none` · `spacing tight|close|spread` · `distance <1-20>` (how many yards bots follow you)

### Quest and loot words
`quests` · `accept` · `accept all` · `turn in` · `share` · `drop quest <name>` · `loot` (need before greed) · `loot all` (bot rolls for itself and takes everything, resets when you relog)

### Vendor words
`sell grey` (sell junk to the vendor you've selected or one nearby) · `repair`  
If the vendor is far away, the bot walks over and you say it again.

### Whisper only (to one bot)
`goto`, `mount`, `dismount`, `sit`, `stand`, `hearth`, `invite`, `leave`, `reset`, `focus <name>`, `interrupt`, `cc`, `aoe on|off`, `rc` (ready check)  
Many have an `_all` version, such as `mount_all`.

---

## Part 3 of 3: Bot management and tips

### Managing your bots
These come from the **/fb** window or by typing:  
`/fb add <class> [tank|healer|dps]` · `/fb call <name|all>` · `/fb save all` · `/fb unsave all` · `/fb dismiss <name|all>` · `/fb delete <name>` · `/fb list` · `/fb regear all`

Window buttons: **Call saved**, **Save group**, **Dismiss all**, **Unsave all**, **Delete target** (asks first), **List**, **Regear all**.

### Handy /fb shortcuts
`/fb fallback` · `/fb pullback` · `/fb rally` · `/fb formation circle` · `/fb spacing tight` · `/fb distance 8` · `/fb maintank` · `/fb bot on|off` (small panel next to a targeted group member)

### Address a group of bots
Start a whisper or `;` party line with a prefix: `tank:`, `healer:`, `dps:`, `all:`, a class such as `mage:`, or a bot's name.  
Example: `;mage: attack`

### Key bindings
The add-on adds keys you can set under **Esc > Options > Key Bindings > AddOns > WoW Forever Bots**. They include **Attack (mouseover)**, which orders an attack on the monster under your mouse. Bots also react to the Attack and On-my-way pings.

### Tips
- If a bot ignores you, check you are its owner and that it is in your group.
- Bots answer your orders with a short whisper. The addon hides these from chat and shows them on the status line.
- `help` in party chat gives a short reminder of all this.
- Not every word works in party chat. Some only work as a whisper.
