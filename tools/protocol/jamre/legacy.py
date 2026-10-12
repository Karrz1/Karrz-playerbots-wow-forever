"""Our current (70245) core opcode -> wire translation, mirrored from ClassicOpcodes.cpp so tables can be generated and compared.

Parses the override map in ClassicOpcodes.cpp; the three per-group index rules are copied from TranslateServerOpcode70245 and
are checked against the C++ by the Catch2 test (tests/game/ClassicOpcodeTable.cpp) so the two cannot drift silently.
"""
from __future__ import annotations

import re

FIRST_GROUP, LAST_GROUP = 0x45, 0x6A
CHAT_GROUP, GUILD_GROUP = 0x4A, 0x51


def load_core(opcodes_h: str) -> dict[str, int]:
    core = {}
    for line in open(opcodes_h, encoding="latin-1"):
        m = re.match(r"\s*(SMSG_\w+)\s*=\s*0x([0-9A-Fa-f]+)", line)
        if m:
            core[m.group(1)] = int(m.group(2), 16)
    return core


def load_overrides(classic_opcodes_cpp: str) -> dict[str, int]:
    src = open(classic_opcodes_cpp, encoding="latin-1").read()
    blk = src.split("ServerOpcodes =")[1].split("};")[0]
    return {m.group(1): int(m.group(2), 16) for m in re.finditer(r"\{\s*(SMSG_\w+),\s*0x([0-9A-Fa-f]+)\s*\}", blk)}


def legacy_70245(name: str, core: int, overrides: dict[str, int]) -> int:
    if name in overrides:
        return overrides[name]
    g, i = core >> 16, core & 0xFFFF
    if FIRST_GROUP <= g <= LAST_GROUP:
        if g == FIRST_GROUP and 0x17E <= i <= 0x1C0:
            i -= 1
        if g == CHAT_GROUP and i >= 4:
            i += 2
        if g == GUILD_GROUP and i >= 0x1C:
            i -= 1
        return ((g + 1) << 16) | i
    return core
