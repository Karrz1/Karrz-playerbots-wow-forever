import gzip
import json
import os
import struct
import tempfile
import unittest

from jamre.image import Image


def write_dump(path, sections, image_base=0x140000000, present=None):
    head = {"build": "t", "pid": 1, "runtime_base": 0x7FF600000000, "image_base": image_base,
            "sections": [{"name": n, "rva": rva, "length": len(d), "exec": ex, "unreadable_pages": 0, **({"present_b64": present[n]} if present else {})} for n, rva, d, ex in sections]}
    hb = json.dumps(head).encode()
    with gzip.open(path, "wb") as f:
        f.write(b"WOWMEM01" + struct.pack("<I", len(hb)) + hb)
        for _, _, d, _ in sections:
            f.write(d)


class DumpTest(unittest.TestCase):
    def test_roundtrip_through_from_pe(self):
        code = b"\x48\x83\xec\x28" + b"\xcc" * 4092
        data = struct.pack("<I", 0x1000) * 1024
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "w.mem.gz")
            write_dump(p, [(".text", 0x1000, code, True), (".rdata", 0x2000, data, False)])
            img = Image.from_pe(p)
        self.assertEqual(img.base, 0x140000000)
        self.assertTrue(img.is_code(0x1000))
        self.assertFalse(img.is_code(0x2000))
        self.assertEqual(img.u32(0x2000), 0x1000)
        self.assertEqual(img.read(0x1000, 4), b"\x48\x83\xec\x28")

    def test_present_bitmap(self):
        import base64
        code = b"\x90" * 8192
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "w.mem.gz")
            write_dump(p, [(".text", 0x1000, code, True)], present={".text": base64.b64encode(bytes([0b01])).decode()})
            img = Image.from_pe(p)
        self.assertTrue(img.is_present(0x1000))
        self.assertFalse(img.is_present(0x2000))

    def test_rejects_other_gzip(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "x.gz")
            with gzip.open(p, "wb") as f:
                f.write(b"nope")
            with self.assertRaises(ValueError):
                Image.from_pe(p)


if __name__ == "__main__":
    unittest.main()
