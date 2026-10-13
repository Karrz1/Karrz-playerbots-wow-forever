# Server Options for Server Owners


*These options are for people who run their own server. Players on a normal server don't need this.*

By default the server plays the **Blizzlike** way. A few options let you make raids easier or more relaxed on your own server. Each one is **0 (off)** by default. Set it to **1** to turn it on.

### How to change an option
1. Stop the server.
2. Open **worldserver.conf** in a text editor.
3. Find the option below, change the number, and save.
4. Start the server again. Options only change after a restart.

### The options

- **Instance.MoltenCoreSkipRunes**
Off: you douse the seven runes in Molten Core, then Majordomo Executus appears, and his dialogue summons Ragnaros.  
On: no dousing needed. Majordomo and Ragnaros are in place from the start and you can fight them straight away.

- **Instance.PersistRaidTrash**
Off: trash respawns after you leave and come back.  
On: monsters you killed stay dead after you leave, until the raid resets.

- **Instance.IgnoreAttunement**
Off: you need the usual attunement or key quest to enter Molten Core, Blackwing Lair and Onyxia's Lair.  
On: nobody needs an attunement to walk in.

- **Instance.AhnQirajGatesOpen**
Off: the gates to Ahn'Qiraj start closed.  
On: the gates start open.

- **Instance.MoltenCoreHoundsLinked**
Off (default): Core Hounds die one by one.  
On: the hound pack is linked, so the hounds feign death and rise again together.

### Good to know
- All options live in worldserver.conf and need a server restart.
- With every option at 0 your server matches the original game as closely as it can.
- If you leave the rune option off, the rune dousing is done by you. Bots can't douse runes yet.
