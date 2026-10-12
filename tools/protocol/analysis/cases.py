import sys, struct, json
sys.path.insert(0,sys.argv[0].rsplit('/',1)[0])
from fn import Bin
b=Bin(sys.argv[1]); img=b.img
TABLE=0x8f2308; N=0x38f
tg=[img.u32(TABLE+4*i) for i in range(N)]
print('present target pages:', sum(1 for t in tg if img.is_present(t)), 'of', N, 'distinct', len(set(tg)))
crash=0x8e7c6c
print('crash case idx:', [hex(i) for i,t in enumerate(tg) if t==crash])
json.dump(tg, open(sys.argv[2],'w'))
