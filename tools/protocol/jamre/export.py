"""Compact export of what the alignment needs, so the analysis does not need the whole executable.

An export holds, per build: the image base, the switch tables (targets per case slot) and small chunks of code: the first
CHUNK bytes at every distinct case target plus the first CHUNK bytes of each direct callee found in them, two levels deep.
`image_from_export` rebuilds an Image whose sections are those chunks, so the fingerprinting and alignment code runs on it unchanged.
The same JSON can be written by tools/protocol/Export-JamTables.ps1 (PowerShell + C#, no install) on the machine that has the client.
"""
from __future__ import annotations

import base64
import gzip
import json
import struct

import capstone

from .image import Image, Section
from .jumptables import SwitchTable, find_switch_tables

CHUNK = 512
FORMAT = "jamre-export-v1"


def _direct_calls(code: bytes, va: int, md: capstone.Cs) -> list[int]:
    out = []
    for i in md.disasm(code, va):
        if i.mnemonic == "call" and i.op_str.startswith("0x"):
            out.append(int(i.op_str, 16))
    return out


def export_image(img: Image, tables: list[SwitchTable], path: str, min_cases: int = 8, build: str = "") -> dict:
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    tables = [t for t in tables if t.cases >= min_cases]
    chunks: dict[int, bytes] = {}
    frontier = {t for tb in tables for t in tb.targets}
    for _level in range(3):  # case bodies, their callees, the callees' callees
        nxt = set()
        for rva in sorted(frontier):
            if rva in chunks:
                continue
            data = img.read(rva, CHUNK) or _short(img, rva)
            if not data:
                continue
            chunks[rva] = data
            for tgt in _direct_calls(data, img.base + rva, md):
                trva = tgt - img.base
                if img.is_code(trva) and trva not in chunks:
                    nxt.add(trva)
        frontier = nxt
    doc = {
        "format": FORMAT,
        "build": build,
        "image_base": img.base,
        "tables": [
            {"table_rva": t.table_rva, "cases": t.cases, "byte_table_rva": t.byte_table_rva, "load_rva": t.load_rva,
             "bound_from_cmp": t.bound_from_cmp, "targets": t.targets}
            for t in tables
        ],
        "chunks": [{"rva": r, "b64": base64.b64encode(b).decode()} for r, b in sorted(chunks.items())],
    }
    raw = json.dumps(doc, separators=(",", ":")).encode()
    with (gzip.open(path, "wb") if path.endswith(".gz") else open(path, "wb")) as f:
        f.write(raw)
    return {"tables": len(tables), "chunks": len(chunks), "bytes": len(raw)}


def _short(img: Image, rva: int) -> bytes | None:
    sec = img.section_at(rva)
    if not sec:
        return None
    return sec.data[rva - sec.rva : rva - sec.rva + CHUNK]


def load_export(path: str) -> dict:
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rb") as f:
        doc = json.loads(f.read())
    if doc.get("format") != FORMAT:
        raise ValueError(f"{path}: not a {FORMAT} file")
    return doc


def image_from_export(doc: dict) -> Image:
    secs = []
    for c in doc["chunks"]:
        secs.append(Section(f"chunk{c['rva']:x}", c["rva"], base64.b64decode(c["b64"]), True))
    return Image(doc["image_base"], secs)


def tables_from_export(doc: dict) -> list[SwitchTable]:
    out = []
    for t in doc["tables"]:
        out.append(SwitchTable(t["load_rva"], t["table_rva"], t["byte_table_rva"], t["cases"], t.get("bound_from_cmp", True), list(t["targets"])))
    out.sort(key=lambda t: -t.cases)
    return out


def open_any(path: str, min_cases: int = 8) -> tuple[Image, list[SwitchTable]]:
    """A .exe (parsed with pefile) or a jamre export (.json / .json.gz)."""
    if path.lower().endswith((".json", ".json.gz")):
        doc = load_export(path)
        return image_from_export(doc), tables_from_export(doc)
    img = Image.from_pe(path)
    return img, find_switch_tables(img, min_cases)
