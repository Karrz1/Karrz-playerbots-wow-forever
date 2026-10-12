import sys, json, struct, re
sys.path.insert(0,sys.argv[0].rsplit('/',1)[0])
from fn import Bin
b=Bin(sys.argv[1]); cases=json.load(open(sys.argv[2])); img=b.img
def cstr(rva):
    d=img.read(rva,64)
    if not d: return None
    m=re.match(rb'[A-Za-z0-9_ .:%\-]{4,60}\x00',d)
    return m.group(0)[:-1].decode() if m else None
def strings_in(start,end):
    out=[]
    d=img.read(start,end-start)
    if not d: return out
    for ins in b.md.disasm(d,b.base+start):
        if ins.mnemonic=='lea' and 'rip' in ins.op_str:
            m=re.search(r'rip ([+-]) (0x[0-9a-f]+)\]',ins.op_str)
            if m:
                disp=int(m.group(2),16)*(1 if m.group(1)=='+' else -1)
                t=ins.address-b.base+len(ins.bytes)+disp
                s=cstr(t)
                if s: out.append(s)
    return out
def callback(i):
    c=cases[str(i)]
    ints=[x for x in c['calls'] if x!='slot']
    proc=ints[1] if len(ints)>1 else None
    if proc is None: return None
    d=img.read(proc,16)
    ins=next(b.md.disasm(d,b.base+proc))
    if ins.mnemonic!='mov' or 'rip' not in ins.op_str: return ('noslot',proc)
    m=re.search(r'rip \+ (0x[0-9a-f]+)\]',ins.op_str)
    slot=proc+len(ins.bytes)+int(m.group(1),16)
    v=img.u32(slot); hi=img.u32(slot+4)
    ptr=(hi<<32)|v; rva=ptr-b.base
    return ('cb',rva if 0<=rva<0x8600000 else None)
lo,hi=int(sys.argv[3],16),int(sys.argv[4],16)
for i in range(lo,hi+1):
    r=callback(i)
    if not r or r[0]!='cb' or r[1] is None: print(f"{i:#05x} {r}"); continue
    cb=r[1]
    if not img.is_present(cb): print(f"{i:#05x} cb {cb:#x} (page not decrypted)"); continue
    s,e=b.func(cb)
    strs=strings_in(s,e)
    # also one level of callees
    print(f"{i:#05x} cb {cb:#x} size {e-s} :: {sorted(set(strs))[:8]}")
