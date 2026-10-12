import sys, re
sys.path.insert(0,sys.argv[0].rsplit('/',1)[0])
from fn import Bin
b=Bin(sys.argv[1]); lo=int(sys.argv[2],16); hi=int(sys.argv[3],16)
cache={}
def classify(rva):
    if rva in cache: return cache[rva]
    d=b.img.read(rva,0x60); r='fn'
    if d and b.img.is_present(rva):
        size=None; seen=False
        for i in b.md.disasm(d,b.base+rva):
            if i.mnemonic=='mov' and i.op_str.startswith('r8d, ') and size is None:
                try: size=int(i.op_str.split(', ')[1],16)
                except: pass
            if i.mnemonic=='call' and int(i.op_str,16)-b.base==0x4739e80 and size: seen=True; break
            if i.mnemonic=='ret': break
        if seen: r=f'prim{size}'
    cache[rva]=r; return r
data=b.img.read(lo,hi-lo)
last=None
for i in b.md.disasm(data,b.base+lo):
    r=i.address-b.base
    if i.mnemonic=='call' and i.op_str.startswith('0x'):
        t=int(i.op_str,16)-b.base; k=classify(t)
        # destination: preceding 'lea rdx, [...]' or 'lea rcx, [...]'
        print(f"{r:#x} call {t:#x} {k}  dst={last}")
    elif i.mnemonic=='lea' and (i.op_str.startswith('rdx,') or i.op_str.startswith('rcx,')):
        last=i.op_str
    elif i.mnemonic in('jmp','je','jne','jb','jbe','ja','jae','jg','jle','jns','jl','jge','js') and i.op_str.startswith('0x'):
        t=int(i.op_str,16)-b.base
        print(f"{r:#x}   {i.mnemonic} -> {t:#x}")
    elif i.mnemonic=='ret': print(f"{r:#x} RET")
