"""Build tiny synthetic x64 images with keystone, shaped like MSVC switch dispatch, for the unit tests."""
from __future__ import annotations

import struct

import keystone

from jamre.image import Image, Section

BASE = 0x140000000
KS = keystone.Ks(keystone.KS_ARCH_X86, keystone.KS_MODE_64)


def asm(text: str, rva: int) -> bytes:
    enc, _ = KS.asm(text, BASE + rva)
    return bytes(enc)


def switch_image(case_asm: list[str], text_rva=0x1000, rdata_rva=0x3000, shift=0, sparse: list[int] | None = None) -> tuple[Image, int]:
    """One dispatch function at text_rva+shift and one case body per entry of case_asm (each a code string).

    sparse: if given, a byte index table maps case index -> body index (MSVC sparse switch).
    Returns (image, rva of the dword table).
    """
    ncases = len(sparse) if sparse is not None else len(case_asm)
    bodies = bytearray()
    body_rvas = []
    cur = text_rva + 0x100 + shift
    for text in case_asm:
        code = asm(text, cur)
        body_rvas.append(cur)
        bodies += code + b"\xcc" * ((-len(code)) % 16)
        cur += len(code) + ((-len(code)) % 16)
    byte_rva = rdata_rva + 0x400
    dword_rva = rdata_rva
    byte_part = ""
    if sparse is not None:
        byte_part = f"movzx eax, byte ptr [r8+rcx+{byte_rva:#x}]\n"
        index_reg = "rax"
    else:
        index_reg = "rcx"
    dispatch = asm(
        f"cmp ecx, {ncases - 1}\n ja 0x{BASE + text_rva + shift + 0x80:x}\n"
        + byte_part
        + f"mov eax, dword ptr [r8+{index_reg}*4+{dword_rva:#x}]\n add rax, r8\n jmp rax\n",
        text_rva + shift,
    )
    text = bytearray(b"\xcc" * (0x100 + shift)) + bodies
    text[shift : shift + len(dispatch)] = dispatch
    text[shift + 0x80 : shift + 0x81] = b"\xc3"  # default: ret
    rdata = bytearray(0x800)
    table = body_rvas if sparse is None else body_rvas
    for i, r in enumerate(table):
        rdata[4 * i : 4 * i + 4] = struct.pack("<I", r)
    if sparse is not None:
        for i, v in enumerate(sparse):
            rdata[0x400 + i] = v
    img = Image(BASE, [Section(".text", text_rva, bytes(text), True), Section(".rdata", rdata_rva, bytes(rdata), False)])
    return img, dword_rva
