import sys, json, collections
sys.path.insert(0,sys.argv[0].rsplit('/',1)[0])
from fn import Bin
import capstone
b=Bin(sys.argv[1]); img=b.img; base=b.base
tg=json.load(open(sys.argv[2]))
fs,fe=b.func(0x8e5a90)
starts=sorted(set(tg))
out={}
for i,t in enumerate(tg):
    # case body: until the next instruction that is an unconditional jmp out (jmp to epilog) -- bounded to 400 bytes
    data=img.read(t,400)
    calls=[];slot=None;locs=[];term=None
    for ins in b.md.disasm(data,base+t):
        r=ins.address-base
        if ins.mnemonic=='call' and ins.op_str.startswith('0x'):
            calls.append(int(ins.op_str,16)-base)
        elif ins.mnemonic=='call' and 'rip' not in ins.op_str and ins.op_str in('rax','rcx'):
            calls.append('slot')
        if ins.mnemonic=='mov' and ins.op_str.startswith('rax, qword ptr [rip +'):
            slot=ins.op_str
        if ins.mnemonic=='lea' and ins.op_str.startswith('rcx, [rbp +') and not locs:
            locs.append(ins.op_str.split('+')[1].strip(' ]'))
        if ins.mnemonic=='jmp' and ins.op_str.startswith('0x'):
            term=r; break
        if ins.mnemonic in ('ret','int3'): break
    out[i]={'t':t,'calls':calls,'loc':locs[0] if locs else None,'hasslot':'slot' in calls}
json.dump(out,open(sys.argv[3],'w'))
c=collections.Counter(len(v['calls']) for v in out.values()); print(sorted(c.items()))
print('with handler slot:',sum(v['hasslot'] for v in out.values()))
for i in list(range(0x90,0xB0)):
    v=out[i]; print(hex(i), hex(v['t']), v['loc'], [hex(x) if x!='slot' else 'slot' for x in v['calls']][:5])
