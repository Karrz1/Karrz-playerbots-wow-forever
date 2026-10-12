import sys
sys.path.insert(0,sys.argv[0].rsplit('/',1)[0])
from fn import Bin
b=Bin(sys.argv[1])
def rva(a): return a-b.base
for t in sys.argv[2:]:
    r=int(t,16); s,e=b.func(r)
    print(f"--- {r:#x} func {s:#x}-{e:#x} present={b.img.is_present(r)}")
    data=b.img.read(r,min(0x70,max(e-r,0x40)))
    n=0
    for i in b.md.disasm(data,b.base+r):
        txt=i.op_str
        if i.mnemonic in('call','jmp') and txt.startswith('0x'): txt=f"RVA {int(txt,16)-b.base:#x}"
        print(f"   {i.address-b.base:#x} {i.mnemonic} {txt}"); n+=1
        if i.mnemonic=='ret' or n>26: break
