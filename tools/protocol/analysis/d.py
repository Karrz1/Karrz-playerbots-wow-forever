import sys; sys.path.insert(0,sys.argv[0].rsplit('/',1)[0])
from fn import Bin
b=Bin(sys.argv[1]); a=int(sys.argv[2],16); lo=int(sys.argv[3],16) if len(sys.argv)>3 else None; hi=int(sys.argv[4],16) if len(sys.argv)>4 else None
s,e=b.func(a); print(f"func {s:#x}-{e:#x}")
b.dis(lo if lo else s, hi if hi else e, a)
