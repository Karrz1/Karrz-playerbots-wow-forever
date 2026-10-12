"""Command line: exploration and alignment.  Run with the venv's python:  python -m jamre.cli <command> ..."""
from __future__ import annotations

import argparse
import json
import sys

from .align import align, case_fingerprints, index_map
from .fingerprint import Fingerprinter, call_sites, read_function
from .export import export_image, open_any
from .image import Image
from .jumptables import find_switch_tables


def _pick(tables, rva):
    for t in tables:
        if t.table_rva == rva:
            return t
    raise SystemExit(f"no switch table at {rva:#x}; run `tables` first")


def cmd_tables(a):
    img = Image.from_pe(a.image)
    tables = find_switch_tables(img, a.min_cases)
    for t in tables[: a.top]:
        print(f"table {t.table_rva:#x} cases {t.cases:5d} distinct {t.distinct_targets():5d} "
              f"bytetable {('%#x' % t.byte_table_rva) if t.byte_table_rva else '-':>10} cmp-bound {t.bound_from_cmp} load@{t.load_rva:#x}")


def cmd_disasm(a):
    img = Image.from_pe(a.image)
    fn = read_function(img, a.rva, stop_on_jmp=a.case)
    if not fn:
        raise SystemExit("not code")
    for i in fn.insns[: a.n]:
        print(f"{i.address - img.base:#010x}  {i.mnemonic} {i.op_str}")


def cmd_case(a):
    img = Image.from_pe(a.image)
    t = _pick(find_switch_tables(img, 4), a.table)
    target = t.targets[a.index]
    print(f"case {a.index:#x} of table {t.table_rva:#x} -> {target:#x}")
    for cs in call_sites(img, target):
        args = " ".join(f"{k}={v}" for k, v in sorted(cs.args.items()))
        print(f"  call {cs.callee:#x} at {cs.rva:#x}  {args}")


def cmd_export(a):
    img = Image.from_pe(a.image)
    info = export_image(img, find_switch_tables(img, a.min_cases), a.out, a.min_cases, a.build)
    print(info)


def cmd_align(a):
    (old, told), (new, tnew) = open_any(a.old), open_any(a.new)
    to = _pick(told, a.old_table)
    tn = _pick(tnew, a.new_table)
    fo, fn = Fingerprinter(old, a.depth), Fingerprinter(new, a.depth)
    blocks = align(case_fingerprints(fo, to), case_fingerprints(fn, tn))
    out = []
    for b in blocks:
        if b.kind != "equal" or a.all:
            out.append({"kind": b.kind, "old": [b.old[0], b.old[1]], "new": [b.new[0], b.new[1]]})
    print(json.dumps({"old_cases": to.cases, "new_cases": tn.cases, "blocks": out}, indent=1))
    if a.map:
        json.dump({str(k): v for k, v in index_map(blocks).items()}, open(a.map, "w"))


def main(argv=None):
    p = argparse.ArgumentParser(prog="jamre")
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("tables"); s.add_argument("image"); s.add_argument("--min-cases", type=int, default=8); s.add_argument("--top", type=int, default=60); s.set_defaults(f=cmd_tables)
    s = sub.add_parser("export"); s.add_argument("image"); s.add_argument("out"); s.add_argument("--min-cases", type=int, default=8); s.add_argument("--build", default=""); s.set_defaults(f=cmd_export)
    s = sub.add_parser("disasm"); s.add_argument("image"); s.add_argument("rva", type=lambda x: int(x, 0)); s.add_argument("-n", type=int, default=80); s.add_argument("--case", action="store_true"); s.set_defaults(f=cmd_disasm)
    s = sub.add_parser("case"); s.add_argument("image"); s.add_argument("table", type=lambda x: int(x, 0)); s.add_argument("index", type=lambda x: int(x, 0)); s.set_defaults(f=cmd_case)
    s = sub.add_parser("align"); s.add_argument("old"); s.add_argument("new"); s.add_argument("old_table", type=lambda x: int(x, 0)); s.add_argument("new_table", type=lambda x: int(x, 0))
    s.add_argument("--depth", type=int, default=2); s.add_argument("--all", action="store_true"); s.add_argument("--map"); s.set_defaults(f=cmd_align)
    a = p.parse_args(argv)
    a.f(a)


if __name__ == "__main__":
    main()
