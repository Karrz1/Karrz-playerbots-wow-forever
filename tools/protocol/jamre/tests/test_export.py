from jamre.align import align, case_fingerprints
from jamre.export import export_image, image_from_export, load_export, open_any, tables_from_export
from jamre.fingerprint import Fingerprinter
from jamre.jumptables import find_switch_tables
from .synth import BASE, asm, switch_image


def test_export_roundtrip_keeps_case_fingerprints(tmp_path):
    cases = [f"mov eax, {k * 7 + 1}\n call {BASE + 0x1800:#x}\n ret" for k in range(10)]
    img, _ = switch_image(cases)
    # a callee for the calls
    img.sections[0].data = img.sections[0].data.ljust(0x900, b"\xcc")
    callee = asm("mov eax, 5\n ret", 0x1800)
    d = bytearray(img.sections[0].data)
    d[0x800 : 0x800 + len(callee)] = callee
    img.sections[0].data = bytes(d)
    tables = find_switch_tables(img, 4)
    path = str(tmp_path / "x.json.gz")
    info = export_image(img, tables, path, min_cases=4)
    assert info["tables"] == 1 and info["chunks"] >= 11
    img2, tables2 = open_any(path)
    assert [t.cases for t in tables2] == [10]
    f1 = case_fingerprints(Fingerprinter(img), tables[0])
    f2 = case_fingerprints(Fingerprinter(img2), tables2[0])
    assert f1 == f2


def test_align_between_two_exports(tmp_path):
    old = [f"mov eax, {k * 7 + 1}\n ret" for k in range(9)]
    new = old[:4] + ["xor eax, eax\n ret"] + old[4:]
    paths = []
    for name, cases in (("old", old), ("new", new)):
        img, _ = switch_image(cases)
        p = str(tmp_path / f"{name}.json.gz")
        export_image(img, find_switch_tables(img, 4), p, min_cases=4)
        paths.append(p)
    (io, to), (i2, tn) = open_any(paths[0]), open_any(paths[1])
    blocks = align(case_fingerprints(Fingerprinter(io), to[0]), case_fingerprints(Fingerprinter(i2), tn[0]))
    assert [(b.kind, b.new) for b in blocks if b.kind == "insert"] == [("insert", (4, 5))]
