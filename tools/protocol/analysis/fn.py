import sys, struct, bisect
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..'))
from jamre.image import Image
import capstone
class Bin:
    def __init__(self,path):
        self.img=Image.from_pe(path); self.base=self.img.base
        pd=[s for s in self.img.sections if s.name=='.pdata'][0]
        n=len(pd.data)//12
        self.rf=[struct.unpack_from('<III',pd.data,12*i) for i in range(n)]
        self.starts=[r[0] for r in self.rf]
        self.md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
    def func(self,rva):
        k=bisect.bisect_right(self.starts,rva)-1
        r=self.rf[k]; return r[0],r[1]
    def dis(self,start,end,mark=None):
        data=self.img.read(start,end-start)
        for i in self.md.disasm(data,self.base+start):
            r=i.address-self.base
            print(("=> " if r==mark else "   ")+f"{r:#x}  {i.mnemonic} {i.op_str}")
