"""Find compiler-generated switch jump tables (MSVC x64 style) and the dispatch instruction that uses them.

MSVC x64 emits   mov eax, [base + idx*4 + TABLE_RVA]  /  add rax, base  /  jmp rax   with 32-bit RVAs in the table.
Sparse switches add a byte index table first:   movzx eax, byte [base + idx + BYTETABLE_RVA]  then the dword load.
We locate the dword load by byte pattern (no full disassembly of a 60 MB image), validate that the table holds RVAs into
code, and read the case bound from a `cmp reg, imm` just before it.
"""
from __future__ import annotations

import re
import struct
from dataclasses import dataclass

from .image import Image

# optional REX (0x40-0x4F), 8B /r with mod=10 rm=100 (SIB), SIB scale=4 (bits 10), disp32
_DWORD_LOAD = re.compile(rb"[\x40-\x4f]?\x8b[\x84\x8c\x94\x9c\xa4\xac\xb4\xbc][\x80-\xbf]....", re.S)
# movzx r32, byte [base + idx + disp32]  (0F B6 /r, mod=10 rm=100, SIB scale=1)
_BYTE_LOAD = re.compile(rb"[\x40-\x4f]?\x0f\xb6[\x84\x8c\x94\x9c\xa4\xac\xb4\xbc][\x00-\x3f]....", re.S)
# cmp r/m32, imm8 (83 /7 ib), cmp r/m32, imm32 (81 /7 id), cmp eax, imm32 (3D id)
_CMP = re.compile(rb"\x83[\xf8-\xff](.)|\x81[\xf8-\xff](....)|\x3d(....)", re.S)


@dataclass
class SwitchTable:
    load_rva: int          # address of the dword load instruction
    table_rva: int         # RVA of the dword table
    byte_table_rva: int | None  # RVA of the byte index table, if any
    cases: int             # number of case slots (bound from cmp, else run length)
    bound_from_cmp: bool
    targets: list[int]     # RVA per case slot (after the byte index table, if any)

    def distinct_targets(self) -> int:
        return len(set(self.targets))


def _run_length(img: Image, table_rva: int, limit: int) -> int:
    n = 0
    while n < limit:
        v = img.u32(table_rva + 4 * n)
        if v is None or not img.is_code(v):
            break
        n += 1
    return n


def _find_bound(code: bytes, load_off: int) -> int | None:
    """Largest-index bound from the closest `cmp reg, imm` in the 96 bytes before the load (index <= imm)."""
    lo = max(0, load_off - 96)
    window = code[lo:load_off]
    best = None
    for m in _CMP.finditer(window):
        if m.group(1) is not None:
            v = m.group(1)[0]
            v = v - 256 if v > 127 else v
        elif m.group(2) is not None:
            v = struct.unpack("<i", m.group(2))[0]
        else:
            v = struct.unpack("<i", m.group(3))[0]
        if 0 <= v < 100000:
            best = v  # keep the last (closest) one
    return best


def _find_byte_table(code: bytes, load_off: int, img: Image, rva_of) -> int | None:
    lo = max(0, load_off - 48)
    window = code[lo:load_off]
    last = None
    for m in _BYTE_LOAD.finditer(window):
        disp = struct.unpack("<I", m.group()[-4:])[0]
        if img.section_at(disp) is not None and not img.is_code(disp):
            last = disp
    return last


def find_switch_tables(img: Image, min_cases: int = 4) -> list[SwitchTable]:
    out: list[SwitchTable] = []
    seen: set[tuple[int, int]] = set()
    for sec in img.code_sections:
        code = sec.data
        for m in _DWORD_LOAD.finditer(code):
            disp = struct.unpack("<I", m.group()[-4:])[0]
            if img.section_at(disp) is None or img.is_code(disp):
                continue
            first = img.u32(disp)
            if first is None or not img.is_code(first):
                continue
            load_off = m.start()
            bound = _find_bound(code, load_off)
            byte_tbl = _find_byte_table(code, load_off, img, lambda o: sec.rva + o)
            if byte_tbl is not None:
                ncases = (bound + 1) if bound is not None else None
                if ncases is None:
                    continue
                idx = [img.u8(byte_tbl + i) for i in range(ncases)]
                if any(v is None for v in idx):
                    continue
                run = _run_length(img, disp, max(idx) + 1)
                if run <= max(idx):
                    continue
                targets = [img.u32(disp + 4 * v) for v in idx]
                cases, from_cmp = ncases, True
            else:
                run = _run_length(img, disp, 100000)
                if bound is not None and bound + 1 <= run:
                    cases, from_cmp = bound + 1, True
                else:
                    cases, from_cmp = run, False
                targets = [img.u32(disp + 4 * i) for i in range(cases)]
            if cases < min_cases:
                continue
            key = (sec.rva + load_off, disp)
            if key in seen:
                continue
            seen.add(key)
            out.append(SwitchTable(sec.rva + load_off, disp, byte_tbl, cases, from_cmp, targets))
    out.sort(key=lambda t: -t.cases)
    return out
