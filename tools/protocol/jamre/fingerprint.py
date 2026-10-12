"""Position-independent fingerprints of x64 functions, so the same function can be found in two client builds.

Two builds of the same source compile to the same instructions except for RIP-relative displacements, direct call
targets and absolute addresses.  We mask those and recurse into direct callees (to a small depth) so a reader that only
changed in a callee still differs.  Compiler register allocation changes make a function look different; that is the
signal we want, the alignment step treats "different" as "changed or new".
"""
from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass, field

import capstone

from .image import Image

_RIP = re.compile(r"rip\s*([+-])\s*0x[0-9a-f]+")
_HEX = re.compile(r"0x[0-9a-f]+")
MAX_INSNS = 4096


def _md() -> capstone.Cs:
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    md.skipdata = False
    return md


@dataclass
class Func:
    rva: int
    insns: list  # capstone instructions in address order (linear, first-ret bounded)

    @property
    def size(self) -> int:
        return (self.insns[-1].address + self.insns[-1].size - self.insns[0].address) if self.insns else 0


def read_function(img: Image, rva: int, md: capstone.Cs | None = None, stop_on_jmp: bool = False) -> Func | None:
    md = md or _md()
    sec = img.section_at(rva)
    if sec is None or not sec.executable:
        return None
    off = rva - sec.rva
    code = sec.data[off : off + MAX_INSNS * 15]
    insns = []
    far_jump_pending = False
    base = img.base
    for i in md.disasm(code, base + rva):
        insns.append(i)
        if i.mnemonic == "ret":
            break
        if i.mnemonic == "int3":
            insns.pop()
            break
        if stop_on_jmp and i.mnemonic == "jmp":
            break
        if len(insns) >= MAX_INSNS:
            break
    return Func(rva, insns) if insns else None


class Fingerprinter:
    def __init__(self, img: Image, depth: int = 2):
        self.img = img
        self.depth = depth
        self.md = _md()
        self._cache: dict[tuple[int, int, bool], str] = {}

    def _norm(self, i, start_va: int, depth: int, stack: tuple[int, ...]) -> str:
        base = self.img.base
        ops = i.op_str
        if i.mnemonic in ("call", "jmp") or (i.mnemonic.startswith("j") and i.op_str.startswith("0x")):
            m = _HEX.fullmatch(ops.strip())
            if m:
                tgt = int(ops, 16)
                rva = tgt - base
                if i.mnemonic == "call" or not (start_va <= tgt < start_va + 0x10000):
                    # direct call or tail jump outside the function: replace by the callee's fingerprint
                    if depth > 0 and rva not in stack and self.img.is_code(rva):
                        return f"{i.mnemonic} F:{self.fingerprint(rva, depth - 1, stack)}"
                    return f"{i.mnemonic} F"
                return f"{i.mnemonic} +{tgt - start_va}"
        ops = _RIP.sub(r"rip\1X", ops)

        def imm(m):
            v = int(m.group(0), 16)
            return "A" if v >= base else m.group(0)

        ops = _HEX.sub(imm, ops)
        return f"{i.mnemonic} {ops}"

    def tokens(self, rva: int, depth: int | None = None, stack: tuple[int, ...] = (), stop_on_jmp: bool = False) -> list[str]:
        depth = self.depth if depth is None else depth
        fn = read_function(self.img, rva, self.md, stop_on_jmp)
        if fn is None:
            return []
        start = self.img.base + rva
        return [self._norm(i, start, depth, stack + (rva,)) for i in fn.insns]

    def fingerprint(self, rva: int, depth: int | None = None, stack: tuple[int, ...] = (), stop_on_jmp: bool = False) -> str:
        """stop_on_jmp: the code is a switch case body (ends at its jump to the common epilogue), not a whole function."""
        depth = self.depth if depth is None else depth
        key = (rva, depth, stop_on_jmp)
        if key in self._cache:
            return self._cache[key]
        toks = self.tokens(rva, depth, stack, stop_on_jmp)
        h = hashlib.blake2b("\n".join(toks).encode(), digest_size=8).hexdigest() if toks else "none"
        self._cache[key] = h
        return h


@dataclass
class CallSite:
    rva: int              # address of the call
    callee: int           # callee RVA (direct calls only)
    args: dict = field(default_factory=dict)  # arg register -> immediate loaded just before ('ecx','edx','r8d','r9d')


_ARG_REGS = {"ecx": "ecx", "rcx": "ecx", "edx": "edx", "rdx": "edx", "r8d": "r8d", "r8": "r8d", "r9d": "r9d", "r9": "r9d"}


def call_sites(img: Image, rva: int) -> list[CallSite]:
    """Direct calls of a function with the integer constants loaded into the argument registers before each call.

    That is how a Jam reader looks: `mov edx, 6 / call ReadBits`.  Identical primitives share a callee; calibrating them on
    messages whose layout we know (our own writers) labels each callee as u8/u32/bits(n)/guid/...
    """
    fn = read_function(img, rva)
    if fn is None:
        return []
    out: list[CallSite] = []
    regs: dict[str, int] = {}
    base = img.base
    for i in fn.insns:
        if i.mnemonic == "mov" and "," in i.op_str:
            dst, src = (s.strip() for s in i.op_str.split(",", 1))
            if dst in _ARG_REGS and _HEX.fullmatch(src) or (dst in _ARG_REGS and src.isdigit()):
                regs[_ARG_REGS[dst]] = int(src, 0)
            elif dst in _ARG_REGS:
                regs.pop(_ARG_REGS[dst], None)
        elif i.mnemonic == "xor" and "," in i.op_str:
            a, b = (s.strip() for s in i.op_str.split(",", 1))
            if a == b and a in _ARG_REGS:
                regs[_ARG_REGS[a]] = 0
        elif i.mnemonic == "call":
            m = _HEX.fullmatch(i.op_str.strip())
            if m:
                out.append(CallSite(i.address - base, int(i.op_str, 16) - base, dict(regs)))
            regs.clear()
    return out
