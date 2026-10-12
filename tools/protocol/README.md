# tools/protocol: client message dispatch extraction (jamre)

Purpose: get the Classic client's real server-message numbering and reader layouts from the client binary instead of
guessing them, so `ClassicServerOpcodes70291.inc` (and later the layout specs) are generated and verified.
Plan and evidence: `audit/70291-protocol-plan.md` in the project files.

Read-only: the tool parses `WowB.exe` from disk. It does not run, patch or attach to the client.

## Setup
```
python3 -m venv venv && venv/bin/pip install pefile capstone keystone-engine pytest
cd tools/protocol && ../../venv/bin/python -m pytest -q jamre     # synthetic-image unit tests (keystone-assembled x64)
```

## Steps against the real binaries
1. `python -m jamre.cli tables WowB-70291.exe` lists switch jump tables (cases, distinct targets, byte-index table or not).
   The server-message dispatch is one table per message group; the main group is the largest. Expect group case counts
   like our `GetOpcodeArrayIndex` list in `Opcodes.h` (main 903 in the core table; Classic differs).
2. Same for `WowB-70205.exe`. Pair the tables of both builds by case count and fingerprint similarity.
3. `python -m jamre.cli align OLD NEW OLD_TABLE_RVA NEW_TABLE_RVA --map main.json` prints inserted, removed and changed
   case blocks and writes the old-index to new-index map (equal blocks only).
4. `python -m jamre.cli case WowB-70291.exe TABLE_RVA INDEX` shows the direct calls of a case body with the integer
   arguments loaded before each call (`mov edx, 6 / call ReadBits`): the reader layout, once primitives are calibrated on
   messages whose 70205 layout we know.
5. `python -m jamre.gentable Opcodes.h ClassicOpcodes.cpp maps.json ClassicServerOpcodes70291.inc` writes the table and
   lists every opcode it could not resolve (inserted/changed blocks, groups not aligned) on stderr.

Assumptions to check on the first real run (MSVC x64 builds): jump tables hold 32-bit RVAs loaded with
`mov eax, [base+idx*4+TABLE]`; sparse switches add a byte index table; case bodies end with a `jmp` to the epilogue.
If a table is missing or has the wrong count, use `disasm` on the dispatching function and adjust `jumptables.py`.

## Verification in the server repo
`tests/game/ClassicOpcodeTable.cpp` (Catch2): sniff anchors, 70245 numbering unchanged, no two sent opcodes on one wire number.

## Without sending the executable (PowerShell, no install)
`Export-JamTables.ps1` (PowerShell 5.1, C# 5 via Add-Type) parses the exe read-only and writes the same small
`jamre-export-v1` JSON (`.json.gz`, a few MB): the switch tables and the code at their case targets. `jamre.cli align` and
`tables` accept these files in place of the exes. Run it for the 70291 and the 70205 client and put the two files in the
project files. The C# is the same detection logic as `jumptables.py` (byte patterns, no disassembler); it has not been
compiled yet, a compile error from Add-Type is the first thing to fix.

## The executable is encrypted on disk: use the running client

`WowB.exe` (70205 and 70291 alike) has every `.text` block at entropy 8.0 and no jump tables, call patterns or `int3`
padding in the file; only `.rdata`, `.data` and `.pdata` are plain. The client decrypts its code while it loads, so the
disassembly-based commands (`tables`, `disasm`, `case`, `align`, `export`) need the loaded image:

1. Start the client, log in (best: have been in a party and a battleground so the readers have run).
2. `.\Dump-WowBMemory.ps1 -Out "$env:TEMP\wowb-70291-live.mem.gz" -Build 70291 -Seconds 900` (read-only, `PROCESS_VM_READ`).
   The client decrypts code pages on demand, the first time they run, and a decrypted page stays readable, so the script
   keeps polling the unreadable pages for `-Seconds` and merges them: play normally meanwhile (group invite, BG, inspect,
   whisper, auction house) and everything that ran is captured. `Image.is_present(rva)` tells never-decrypted from zero.
3. Pass that file wherever an exe path is accepted (`python -m jamre.cli tables wowb-70291-live.mem.gz`).

The dump holds Blizzard code: never commit or share it.
