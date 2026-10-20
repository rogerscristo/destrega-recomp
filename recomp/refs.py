"""Find lui/lo-pair references to an address range: refs.py LO [HI]"""
import sys
sys.path.insert(0, 'recomp')
from image import Image
img = Image.from_psxexe('disc/SLUS_007.32')
lo = int(sys.argv[1], 0); hi = int(sys.argv[2], 0) if len(sys.argv) > 2 else lo + 1
CODE_END = 0x800aa000
for a in range(0x80026600, CODE_END, 4):
    i = img.ins(a)
    if i.op != 'lui':
        continue
    for b in range(a + 4, min(a + 64, CODE_END), 4):
        j = img.ins(b)
        if j.rs == i.rt and j.op in ('addiu', 'lw', 'lh', 'lhu', 'lb', 'lbu', 'sw', 'sh', 'sb', 'ori'):
            v = ((i.imm << 16) + (j.imm if j.op != 'ori' else j.imm)) & 0xffffffff
            if lo <= v < hi:
                print(f'{a:08x} {i}  ->  {b:08x} {j}   = {v:#x}')
