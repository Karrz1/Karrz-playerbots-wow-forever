from jamre.align import align, case_fingerprints, index_map
from jamre.fingerprint import Fingerprinter, call_sites
from jamre.jumptables import find_switch_tables
from .synth import BASE, asm, switch_image


def fps(case_asm, shift=0):
    img, _ = switch_image(case_asm, shift=shift)
    t = find_switch_tables(img, 4)[0]
    return case_fingerprints(Fingerprinter(img), t)


def bodies(n):
    return [f"mov eax, {k * 7 + 1}\n add eax, ecx\n ret" for k in range(n)]


def test_fingerprint_is_position_independent():
    assert fps(bodies(6)) == fps(bodies(6), shift=0x40)


def test_fingerprint_changes_when_an_instruction_changes():
    b = bodies(6)
    c = list(b)
    c[3] = "mov eax, 99\n add eax, ecx\n ret"
    a, d = fps(b), fps(c)
    assert [i for i in range(6) if a[i] != d[i]] == [3]


def test_align_finds_inserted_case():
    old = bodies(8)
    new = old[:3] + ["xor eax, eax\n sub eax, 5\n ret"] + old[3:]
    blocks = align(fps(old), fps(new))
    ins = [b for b in blocks if b.kind == "insert"]
    assert len(ins) == 1 and ins[0].new == (3, 4)
    m = index_map(blocks)
    assert m[2] == 2 and m[3] == 4 and m[7] == 8


def test_call_sites_report_argument_constants():
    code = asm("mov edx, 6\n call 0x140002000\n xor r8d, r8d\n mov edx, 0x20\n call 0x140002010\n ret", 0x1000)
    from jamre.image import Image, Section

    img = Image(BASE, [Section(".text", 0x1000, code + b"\xcc" * 16, True), Section(".t2", 0x2000, b"\xc3" * 0x20, True)])
    cs = call_sites(img, 0x1000)
    assert [(c.callee, c.args.get("edx"), c.args.get("r8d")) for c in cs] == [(0x2000, 6, None), (0x2010, 0x20, 0)]


def test_callee_change_changes_fingerprint_but_not_its_address():
    def img_with_callee(callee_body, callee_rva):
        main = asm(f"mov ecx, 1\n call {BASE + callee_rva:#x}\n ret", 0x1000)
        from jamre.image import Image, Section

        text = bytearray(0x2000)
        text[0 : len(main)] = main
        cb = asm(callee_body, callee_rva - 0x1000 + 0x1000)
        text[callee_rva - 0x1000 : callee_rva - 0x1000 + len(cb)] = cb
        return Image(BASE, [Section(".text", 0x1000, bytes(text), True)])

    a = Fingerprinter(img_with_callee("mov eax, 1\n ret", 0x1800)).fingerprint(0x1000)
    b = Fingerprinter(img_with_callee("mov eax, 1\n ret", 0x1900)).fingerprint(0x1000)
    c = Fingerprinter(img_with_callee("mov eax, 2\n ret", 0x1800)).fingerprint(0x1000)
    assert a == b and a != c
