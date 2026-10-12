"""Heuristic, compiler-free check: the flat byte layout of our simple packet writers against the 70291 client reader specs.

usage: writer_vs_reader.py MAIN_GROUP_CASES.txt [--all]

Only Write() bodies made of plain `_worldPacket << x;` statements (casts of u8..u64/float, ObjectGuid and Bits<N> + FlushBits) are
understood; anything else is reported as "complex" and skipped. The opcode translation is the arithmetic one of ClassicOpcodes.cpp
(verified at the sniff anchors only), so a mismatch means: wrong writer, wrong mapping, or a loop/optional the flat spec cannot show.
This is a finder, the golden byte tests (tests/game/ClassicWireSpec.cpp) are the proof.
"""
import glob, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
PK = os.path.join(ROOT, "src/server/game/Server/Packets")
SIZES = {"uint8": "u8", "int8": "u8", "bool": "u8", "uint16": "u16", "int16": "u16", "uint32": "u32", "int32": "u32", "float": "u32",
         "uint64": "u64", "int64": "u64", "ObjectGuid": "GUID"}

ops = {}
for l in open(os.path.join(ROOT, "src/server/game/Server/Protocol/Opcodes.h"), encoding="latin-1"):
    m = re.match(r"\s*(SMSG_\w+)\s*=\s*(0x[0-9A-Fa-f]+)", l)
    if m:
        ops[m.group(1)] = int(m.group(2), 16)
OV = {"SMSG_ACCOUNT_DATA_TIMES": 0x4601B5, "SMSG_GAME_TIME_UPDATE": 0x4601B6, "SMSG_GAME_TIME_SET": 0x4601B7, "SMSG_LOGIN_SET_TIME_SPEED": 0x4601B8,
      "SMSG_SERVER_TIME_OFFSET": 0x4601BF, "SMSG_MIRROR_VARS": 0x460371, "SMSG_LAST_CATALOG_FETCH_RESPONSE": 0x460382}


def wire(name):
    c = ops[name]
    if name in OV:
        x = OV[name]
    else:
        g, i = c >> 16, c & 0xFFFF
        if 0x45 <= g <= 0x6A:
            if g == 0x45 and 0x17E <= i <= 0x1C0: i -= 1
            if g == 0x4A and i >= 4: i += 2
            if g == 0x51 and i >= 0x1C: i -= 1
            x = ((g + 1) << 16) | i
        else:
            x = c
    g, i = x >> 16, x & 0xFFFF
    if g == 0x46 and i >= 0x98: x += 1
    if 0x46 <= g <= 0x6B: x = (((x >> 16) + 1) << 16) | (x & 0xFFFF)
    return x


specs = {}
for l in open(sys.argv[1], encoding="utf-8"):
    r = l.rstrip("\n").split("\t")
    spec = r[-1]
    specs[int(r[1], 16)] = None if ("?" in spec.split() or "(ctor" in spec) else ("" if spec == "(no fields)" else spec)

heads = {}
for f in glob.glob(PK + "/*.h"):
    h = open(f, encoding="latin-1").read()
    for m in re.finditer(r"class (?:TC_GAME_API )?(\w+) final : public ServerPacket\s*\{(.*?)\n        \};", h, re.S):
        cls, body = m.group(1), m.group(2)
        om = re.search(r"ServerPacket\((SMSG_\w+)", body)
        mem = {}
        for t, n in re.findall(r"^\s+([A-Za-z_][\w:<>]*)\s+(\w+)\s*(?:=[^;]*)?;", body, re.M):
            mem[n] = t
        if om:
            heads.setdefault(cls, []).append((os.path.basename(f), om.group(1), mem))


def layout(body, mem):
    body = re.sub(r"//[^\n]*", "", body)
    out, bits = [], 0
    for st in [s.strip() for s in body.split(";") if s.strip()]:
        st = re.sub(r"\s+", " ", st)
        if st.startswith("return"): continue
        if st == "_worldPacket.FlushBits()":
            if bits: out.append("u8"); bits = 0
            continue
        m = re.fullmatch(r"_worldPacket << (.+)", st)
        if not m: return None
        e = m.group(1).strip()
        b = re.fullmatch(r"Bits<(\d+)>\(.*\)", e)
        if b:
            bits += int(b.group(1))
            while bits >= 8: out.append("u8"); bits -= 8
            continue
        if bits: out.append("u8"); bits = 0          # an append flushes pending bits
        a = re.fullmatch(r"As<(\w+)>\(.*\)", e)
        if a and a.group(1) in SIZES: out.append(SIZES[a.group(1)]); continue
        c = re.fullmatch(r"(\w+)\(.*\)", e)
        if c and c.group(1) in SIZES: out.append(SIZES[c.group(1)]); continue
        if re.fullmatch(r"\w+", e) and e in mem and mem[e] in SIZES: out.append(SIZES[mem[e]]); continue
        return None
    if bits: out.append("u8")
    return " ".join(out)


rows = []
for f in glob.glob(PK + "/*.cpp"):
    c = open(f, encoding="latin-1").read()
    for m in re.finditer(r"WorldPacket const\* (\w+)::Write\(\)\n\{(.*?)\n\}", c, re.S):
        cls = m.group(1)
        for hf, op, mem in heads.get(cls, []):
            lay = layout(m.group(2), mem)
            w = wire(op)
            spec = specs.get(w)
            rows.append((op, cls, hex(w), lay, spec))
bad = same = skipped = 0
for op, cls, w, lay, spec in sorted(rows):
    if lay is None: skipped += 1; continue
    if spec is None: continue
    if lay == spec: same += 1; continue
    bad += 1
    print(f"MISMATCH {op} ({cls}) wire {w}\n   ours   : {lay}\n   client : {spec}")
print(f"\nsimple writers compared: {same + bad}, equal {same}, differ {bad}; complex (skipped) {skipped}")
