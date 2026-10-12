"""Ordered reads (and the bit tests that follow a byte read) of one reader function range: ordered.py DUMP START END"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from groups import Analyzer

a = Analyzer(sys.argv[1]); b = a.b
start = int(sys.argv[2], 16)
end = int(sys.argv[3], 16) if len(sys.argv) > 3 else b.func(start)[1]
d = a.img.read(start, end - start)
n = 0
for i in b.md.disasm(d, b.base + start):
    r = i.address - b.base
    if i.mnemonic == 'call' and re.fullmatch(r'0x[0-9a-f]+', i.op_str):
        t = int(i.op_str, 16) - b.base
        p = a.prim(t)
        tok = 'GUID' if t == 0x47b4330 else (p or f'fn{t:#x}')
        n += 1
        print(f"{n:3d} {r:#x} {tok}")
    elif i.mnemonic == 'cmp' and re.search(r', 0x(80|40)$', i.op_str) and re.match(r'(byte ptr \[|[abcd]l|r\d+b)', i.op_str):
        print(f"      {r:#x} bit-test {i.op_str}")
