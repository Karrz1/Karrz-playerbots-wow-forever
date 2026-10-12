"""Minimal PE image model: sections by RVA, no execution."""
from __future__ import annotations

import bisect
import struct
from dataclasses import dataclass, field


@dataclass
class Section:
    name: str
    rva: int
    data: bytes
    executable: bool
    present: bytes | None = None  # dump only: bit per 4 KB page, set if the page was readable in the running client

    @property
    def end(self) -> int:
        return self.rva + len(self.data)


@dataclass
class Image:
    base: int
    sections: list[Section] = field(default_factory=list)
    _sorted: list = field(default_factory=list, repr=False)
    _starts: list = field(default_factory=list, repr=False)
    _index_len: int = field(default=-1, repr=False)

    @classmethod
    def from_dump(cls, path: str) -> "Image":
        """Load a Dump-WowBMemory.ps1 file: the sections of the running (decrypted) client, gzip, see that script."""
        import base64
        import gzip
        import json

        with gzip.open(path, "rb") as f:
            if f.read(8) != b"WOWMEM01":
                raise ValueError("not a WOWMEM01 dump")
            head = json.loads(f.read(struct.unpack("<I", f.read(4))[0]))
            img = cls(base=head["image_base"])
            for s in head["sections"]:
                data = f.read(s["length"])
                if len(data) != s["length"]:
                    raise ValueError("truncated dump")
                present = base64.b64decode(s["present_b64"]) if s.get("present_b64") else None
                img.sections.append(Section(s["name"], s["rva"], data, s["exec"], present))
        return img

    @classmethod
    def from_pe(cls, path: str) -> "Image":
        """A PE file on disk, or a memory dump of the running client (detected by the gzip magic)."""
        with open(path, "rb") as f:
            if f.read(2) == b"\x1f\x8b":
                return cls.from_dump(path)
        import pefile  # imported lazily so the tests of pure helpers do not need it

        pe = pefile.PE(path, fast_load=True)
        img = cls(base=pe.OPTIONAL_HEADER.ImageBase)
        for s in pe.sections:
            name = s.Name.rstrip(b"\0").decode("ascii", "replace")
            data = s.get_data()
            # IMAGE_SCN_MEM_EXECUTE = 0x20000000, IMAGE_SCN_CNT_CODE = 0x20
            executable = bool(s.Characteristics & 0x20000000) or bool(s.Characteristics & 0x20)
            img.sections.append(Section(name, s.VirtualAddress, data, executable))
        return img

    def section_at(self, rva: int) -> Section | None:
        # sections are few for a real PE but thousands for an export (code chunks): keep a sorted index
        if self._index_len != len(self.sections):
            self._sorted = sorted(self.sections, key=lambda s: s.rva)
            self._starts = [s.rva for s in self._sorted]
            self._index_len = len(self.sections)
        k = bisect.bisect_right(self._starts, rva) - 1
        while k >= 0:
            s = self._sorted[k]
            if rva < s.end:
                return s
            if s.rva + 0x100000 < rva:  # sections do not overlap in a PE; chunks of an export are small
                break
            k -= 1
        return None

    def is_present(self, rva: int) -> bool:
        """False only for a page of a memory dump that the running client had not decrypted/mapped when it was read."""
        s = self.section_at(rva)
        if not s:
            return False
        if s.present is None:
            return True
        pg = (rva - s.rva) >> 12
        return bool(s.present[pg >> 3] >> (pg & 7) & 1)

    def is_code(self, rva: int) -> bool:
        s = self.section_at(rva)
        return bool(s and s.executable)

    def read(self, rva: int, size: int) -> bytes | None:
        s = self.section_at(rva)
        if not s or rva + size > s.end:
            return None
        o = rva - s.rva
        return s.data[o : o + size]

    def u32(self, rva: int) -> int | None:
        b = self.read(rva, 4)
        return struct.unpack("<I", b)[0] if b else None

    def u8(self, rva: int) -> int | None:
        b = self.read(rva, 1)
        return b[0] if b else None

    @property
    def code_sections(self) -> list[Section]:
        return [s for s in self.sections if s.executable]
