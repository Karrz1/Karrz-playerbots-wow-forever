"""All server message groups of the 70291 client: dispatcher, jump table, and per message the field sequence the constructor reads.

usage: groups.py DUMP OUTDIR     writes OUTDIR/group-XX.txt (one line per message) and OUTDIR/groups.json
"""
import json, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fn import Bin

GUIDFN = 0x47b4330      # packed guid reader of build 70291
CHECKFN = 0x4739e80     # "enough bytes left" check every primitive reader calls


class Analyzer:
    def __init__(self, path):
        self.b = Bin(path)
        self.img = self.b.img
        self._prim = {}

    def rel_calls(self, start, maxbytes):
        d = self.img.read(start, maxbytes)
        out = []
        if not d:
            return out
        for i in self.b.md.disasm(d, self.b.base + start):
            if i.mnemonic == 'call' and re.fullmatch(r'0x[0-9a-f]+', i.op_str):
                out.append(int(i.op_str, 16) - self.b.base)
            if i.mnemonic == 'ret' and self.img.read(i.address - self.b.base + len(i.bytes), 1) == b'\xcc':
                break
        return out

    def prim(self, rva):
        if rva in self._prim:
            return self._prim[rva]
        r = None
        d = self.img.read(rva, 0x60)
        if d and self.img.is_present(rva):
            size = None
            for i in self.b.md.disasm(d, self.b.base + rva):
                if i.mnemonic == 'mov' and i.op_str.startswith('r8d, ') and size is None:
                    try:
                        size = int(i.op_str.split(', ')[1], 16)
                    except ValueError:
                        pass
                if i.mnemonic == 'call' and size and re.fullmatch(r'0x[0-9a-f]+', i.op_str) and int(i.op_str, 16) - self.b.base == CHECKFN:
                    r = f'u{size * 8}'
                    break
                if i.mnemonic == 'ret':
                    break
        self._prim[rva] = r
        return r

    def tokens(self, fn, depth=0, seen=frozenset()):
        if fn in seen or depth > 4:
            return []
        seen = seen | {fn}
        if fn == GUIDFN:
            return ['GUID']
        p = self.prim(fn)
        if p:
            return [p]
        if not self.img.is_present(fn):
            return ['?']
        out = []
        for t in self.rel_calls(fn, 6000):
            out += self.tokens(t, depth + 1, seen)
        return out

    def case(self, target):
        """ctor / second call / destructor of one dispatcher case, and the callback via the global slot of the second call."""
        d = None
        for size in (400, 200, 100, 40, 16):
            d = self.img.read(target, size)
            if d:
                break
        calls = []
        if not d:
            return calls
        for i in self.b.md.disasm(d, self.b.base + target):
            if i.mnemonic == 'call' and re.fullmatch(r'0x[0-9a-f]+', i.op_str):
                calls.append(int(i.op_str, 16) - self.b.base)
            elif i.mnemonic == 'call':
                calls.append(None)
            if i.mnemonic in ('jmp', 'ret', 'int3') and calls:
                break
        # 0x469df90 is the "assert and continue" helper that some cases call first; it is not part of the message
        return [c for c in calls if c != 0x469df90] or calls

    def fields(self, ctor):
        if not self.img.is_present(ctor):
            return '(ctor page not decrypted)'
        toks = self.tokens(ctor)
        out = []
        for t in toks:
            if t == '?' and out and out[-1] == '?':
                continue
            out.append(t)
        return ' '.join(out) if out else '(no fields)'

    def dispatchers(self):
        t = [s for s in self.img.sections if s.name == '.text'][0]
        found = {}
        for g in range(0x45, 0x70):
            imm = struct.pack('<I', (-(g << 16)) & 0xffffffff)
            for m in re.finditer(re.escape(imm), t.data):
                o = m.start()
                if not (t.data[o - 2] == 0x81 and 0xc0 <= t.data[o - 1] <= 0xc7):
                    continue
                rva = t.rva + o - 2
                s, e = self.b.func(rva)
                if e - s < 0x180:
                    continue
                code = self.img.read(rva, 0x50)
                n = table = None
                for i in self.b.md.disasm(code, self.b.base + rva):
                    mm = re.fullmatch(r'e?[a-z0-9]+, (0x[0-9a-f]+)', i.op_str) if i.mnemonic == 'cmp' else None
                    if mm and n is None:
                        n = int(mm.group(1), 16)
                    mm = re.search(r'\*4 \+ (0x[0-9a-f]+)\]', i.op_str)
                    if i.mnemonic == 'mov' and mm:
                        table = int(mm.group(1), 16)
                        break
                if n is not None and table is not None:
                    found[g] = (s, n, table)
        return found


def main():
    an = Analyzer(sys.argv[1])
    outdir = sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    disp = an.dispatchers()
    meta = {}
    for g, (fn, n, table) in sorted(disp.items()):
        lines = []
        for i in range(n + 1):
            tgt = an.img.u32(table + 4 * i)
            calls = an.case(tgt)
            ctor = calls[0] if calls else None
            dtor = calls[-1] if calls else None
            fields = an.fields(ctor) if ctor else '-'
            dt = 'trivial dtor' if dtor == 0x1d9210 else (f'dtor {dtor:#x}' if dtor else '-')
            lines.append(f"{i:#05x}\t{(g << 16) + i:#08x}\tcase {tgt:#x}\tctor {ctor if ctor is None else hex(ctor)}\t{dt}\t{fields[:400]}")
        with open(os.path.join(outdir, f'group-{g:02x}.txt'), 'w') as f:
            f.write('\n'.join(lines) + '\n')
        meta[g] = {'dispatcher': fn, 'cases': n + 1, 'jump_table': table}
        print(f"group {g:#x}: dispatcher {fn:#x}, {n + 1} cases, table {table:#x}")
    json.dump(meta, open(os.path.join(outdir, 'groups.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
