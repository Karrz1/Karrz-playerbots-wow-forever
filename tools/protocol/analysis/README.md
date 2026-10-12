# Analysis scripts used on the 70291 memory dump

Run with the tool's venv (`pip install pefile capstone`) against a `Dump-WowBMemory.ps1` dump (never against the on-disk exe, its code is
encrypted). Order used to produce `doc/classic-70291-protocol.md`:

1. `cases.py DUMP out.json`  : the dispatcher's jump table (RVA 0x8f2308, 911 entries) -> case body RVA per message index.
2. `cases2.py DUMP out.json cases.json` : per case the constructor, the second call (handler invoke) and the destructor.
3. `seq.py DUMP cases.json 0 38e` : the field sequence each message constructor reads (u8..u64, GUID), sub-readers expanded.
4. `events.py DUMP cases.json 90 af` : strings (Lua event names) referenced by the registered handler callbacks, to name a case.
5. `layout.py DUMP lo hi` / `d.py DUMP rva [lo hi]` / `prim.py DUMP rva..` : annotated reads / plain disassembly / primitive readers.

The addresses (dispatcher 0x8e5a90, primitive readers 0x473a000 u8, 0x473a0c0 u16, 0x473a120/0x473a180 u32, 0x473a300 u64, GUID 0x47b4330)
are specific to build 70291; for another build find the dispatcher via a crash stack (`doc/classic-70291-protocol.md`).

## Golden byte tests and the writer scan

- `gen_specs.py MAIN_GROUP_CASES.txt OUT.inc` writes `tests/game/data/Classic70291MainGroupSpecs.inc` (field layout per main-group case, `?` = unresolved
  field, `!` = reader page not decrypted). `tests/game/ClassicWireSpec.cpp` (`[ClassicWireSpec]`) writes our packets and requires the client's
  reader to consume every byte. `ctest -C RelWithDebInfo -L ClassicWireSpec` (the tag is the ctest label; `-R` matches test names, not tags); the hidden `[.ClassicWireGaps]` test fails on purpose and lists the opcodes
  still to decode.
- `writer_vs_reader.py MAIN_GROUP_CASES.txt` is a compiler-free finder: it reads the plain `_worldPacket << x;` writers from the sources and compares
  their flat layout with the client specs (77 simple writers: all equal after the SMSG_SEASON_INFO fix). It also confirms the arithmetic
  opcode translation of ClassicOpcodes.cpp for those 77 messages.
