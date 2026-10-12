"""Align the switch cases of two builds by reader fingerprint: finds inserted, removed and changed messages."""
from __future__ import annotations

import difflib
from dataclasses import dataclass

from .fingerprint import Fingerprinter
from .jumptables import SwitchTable


@dataclass
class Block:
    kind: str            # equal | insert | delete | replace
    old: tuple[int, int]  # [start, end) case indices in the old table
    new: tuple[int, int]


def case_fingerprints(fp: Fingerprinter, table: SwitchTable) -> list[str]:
    return [fp.fingerprint(t, stop_on_jmp=True) for t in table.targets]


def align(old: list[str], new: list[str]) -> list[Block]:
    sm = difflib.SequenceMatcher(None, old, new, autojunk=False)
    return [Block(tag, (i1, i2), (j1, j2)) for tag, i1, i2, j1, j2 in sm.get_opcodes()]


def index_map(blocks: list[Block]) -> dict[int, int]:
    """old case index -> new case index for every case inside an equal block."""
    m: dict[int, int] = {}
    for b in blocks:
        if b.kind == "equal":
            for k in range(b.old[1] - b.old[0]):
                m[b.old[0] + k] = b.new[0] + k
    return m
