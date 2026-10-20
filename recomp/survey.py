import json, sys, collections
sys.path.insert(0, 'recomp')
from image import Image
from mips import REG
img = Image.from_psxexe('disc/SLUS_007.32')
rea = [int(p['address'], 16) for p in json.load(open('analysis/rea_list_procedures_main.json'))['normalized_result']]
reaset = set(rea)
ops = collections.Counter(); jrs = []; jals = set(); bios = collections.Counter()
for a in range(img.base, img.end, 4):
    i = img.ins(a); ops[i.op] += 1
    if i.op == 'jal': jals.add(i.target)
    if i.op == 'jr' and i.rs != 31: jrs.append(a)
    # bios stub: addiu t2,zero,0xA0/B0/C0 ; jr t2 ; addiu t1,zero,N
    if i.op == 'addiu' and i.rs == 0 and i.rt == 10 and i.imm in (0xa0, 0xb0, 0xc0):
        n1 = img.ins(a + 4); n2 = img.ins(a + 8)
        if n1.op == 'jr' and n1.rs == 10 and n2.rt == 9:
            bios[(hex(i.imm), hex(n2.imm), hex(a))] += 1
print('ops', ops.most_common())
print('jr non-ra', len(jrs), [hex(x) for x in jrs])
print('jal targets', len(jals), 'not in REA', sorted(hex(x) for x in jals - reaset))
print('REA not jal targets', len(reaset - jals))
for k in sorted(bios): print('bios', k)
