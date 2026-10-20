import json, sys
sys.path.insert(0, 'recomp')
from image import Image
from analyze import Analyzer
img = Image.from_psxexe('disc/SLUS_007.32')
rea = [int(p['address'], 16) for p in json.load(open('analysis/rea_list_procedures_main.json'))['normalized_result']]
an = Analyzer(img, 0x80026000, 0x800aa000, rea + [img.entry])
fs = an.run()
print('funcs', len(fs), 'new', len(set(fs) - set(rea)))
ni = sum(len(f.insns) for f in fs.values()); print('insns', ni)
cov = set()
for f in fs.values(): cov |= set(f.insns)
print('covered words', len(cov), 'of', (0x800aa000 - 0x80026618) // 4)
jr_unres = []
for f in fs.values():
    for a, i in f.insns.items():
        if i.op == 'jr' and i.rs != 31 and a not in f.jtables:
            jr_unres.append((a, f.entry))
    for b in f.bad: print('bad', hex(f.entry), hex(b[0]), b[1])
print('jtables', sum(len(f.jtables) for f in fs.values()))
for f in fs.values():
    for a, t in f.jtables.items(): print('  jt', hex(a), len(t))
print('unresolved jr', len(jr_unres))
for a, e in sorted(jr_unres):
    prev = [str(img.ins(a - 4 * k)) for k in range(3, -2, -1)]
    print(' ', hex(a), 'in', hex(e), ' | '.join(prev))
