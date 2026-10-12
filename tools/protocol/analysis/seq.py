import re
import sys, json
sys.path.insert(0,sys.argv[0].rsplit('/',1)[0])
from fn import Bin
b=Bin(sys.argv[1]); cases=json.load(open(sys.argv[2]))
cache={}
def prim(rva):
    if rva in cache: return cache[rva]
    r=None
    d=b.img.read(rva,0x60)
    if d and b.img.is_present(rva):
        size=None
        for i in b.md.disasm(d,b.base+rva):
            if i.mnemonic=='mov' and i.op_str.startswith('r8d, ') and size is None:
                try: size=int(i.op_str.split(', ')[1],16)
                except: pass
            if i.mnemonic=='call' and size and re.fullmatch(r'0x[0-9a-f]+',i.op_str) and int(i.op_str,16)-b.base==0x4739e80: r=f'u{size*8}'; break
            if i.mnemonic=='ret': break
    cache[rva]=r; return r
def calls(start,maxbytes):
    d=b.img.read(start,maxbytes); out=[]
    for i in b.md.disasm(d,b.base+start):
        if i.mnemonic=='call' and re.fullmatch(r'0x[0-9a-f]+',i.op_str): out.append(int(i.op_str,16)-b.base)
        if i.mnemonic=='ret':
            nxt=b.img.read(i.address-b.base+len(i.bytes),1)
            if nxt==b'\xcc': break
    return out
GUIDFN=0x47b4330
def tokens(fn,depth,seen):
    if fn in seen or depth>4: return []
    seen=seen|{fn}
    if fn==GUIDFN: return ['GUID']
    p=prim(fn)
    if p: return [p]
    if not b.img.is_present(fn): return ['?']
    out=[]
    for t in calls(fn,6000):
        sub=tokens(t,depth+1,seen)
        out+=sub
    return out
def seq(idx):
    ctor=cases[str(idx)]['calls'][0]
    if not b.img.is_present(ctor): return ctor,'(ctor page not decrypted)'
    toks=tokens(ctor,0,frozenset())
    out=[]
    for t in toks:
        if t=='?' and out and out[-1]=='?': continue
        out.append(t)
    return ctor,' '.join(out) if out else '(no fields)'
lo,hi=int(sys.argv[3],16),int(sys.argv[4],16)
for i in range(lo,hi+1):
    pass
out=[]
for i in range(lo,hi+1):
    try:
        rd,s=seq(i)
    except Exception as e:
        rd,s=None,'error '+str(e)
    c=cases[str(i)]
    ints=[x for x in c['calls'] if x!='slot']
    dt=('slot ' if 'slot' in c['calls'] else '')+('trivial dtor' if ints[-1]==0x1d9210 else f"dtor {ints[-1]:#x}")
    out.append(f"{i:#05x}\t{0x470000+i:#08x}\tctor {c['calls'][0]:#x}\treader {('%#x'%rd) if rd else '-'}\t{dt}\t{s[:300]}")
print('\n'.join(out))
