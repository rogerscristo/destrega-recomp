"""Control-flow analysis: function bodies and jump tables for one code image.

Seeds come from REA (Ghidra) procedure exports plus the entry point and any
`jal` targets discovered while tracing. Bodies are found by flow tracing from
each entry, so data interleaved in .text is never decoded as code.
"""
from dataclasses import dataclass, field
from mips import decode, reads_writes


@dataclass
class Func:
    entry: int
    insns: dict = field(default_factory=dict)       # addr -> Ins (incl. delay slots)
    labels: set = field(default_factory=set)        # intra-function branch targets
    jtables: dict = field(default_factory=dict)     # jr addr -> [targets]
    calls: set = field(default_factory=set)         # direct call targets
    tailcalls: set = field(default_factory=set)
    bad: list = field(default_factory=list)          # (addr, reason)


class Analyzer:
    def __init__(self, img, code_lo, code_hi, seeds, external_funcs=()):
        self.img = img
        self.code_lo, self.code_hi = code_lo, code_hi
        self.seeds = set(s for s in seeds if code_lo <= s < code_hi)
        # Function entries in other images (e.g. main exe for overlays).
        self.external = set(external_funcs)
        self.funcs = {}

    def in_code(self, a):
        return self.code_lo <= a < self.code_hi and (a & 3) == 0

    def ins(self, a):
        return self.img.ins(a)

    def run(self):
        work = sorted(self.seeds)
        known = set(work)
        while work:
            e = work.pop()
            f = self.trace(e, known)
            self.funcs[e] = f
            for t in sorted(f.calls):
                if self.in_code(t) and t not in known:
                    known.add(t)
                    work.append(t)
        return self.funcs

    # -- jump tables --------------------------------------------------------
    def const_of(self, f, reg, before):
        """Resolve `reg` to a constant built by lui(/addiu|ori) before `before`.

        First scans backwards linearly; if the defining instruction isn't
        found nearby, requires that every write to `reg` between the function
        entry and `before` forms the same constant.
        """
        a = before - 4
        add = 0
        for _ in range(32):
            if a < f.entry:
                break
            i = self.ins(a)
            _, w = reads_writes(i)
            if reg in w:
                if i.op in ('addiu', 'ori') and i.rs == reg:
                    add += i.imm
                elif i.op == 'addiu' and i.rs != reg:
                    c = self.const_of(f, i.rs, a)
                    return None if c is None else (c + i.imm + add) & 0xffffffff
                elif i.op == 'lui':
                    return ((i.imm << 16) + add) & 0xffffffff
                elif i.op == 'addu' and i.rt == 0:
                    c = self.const_of(f, i.rs, a)
                    return None if c is None else (c + add) & 0xffffffff
                else:
                    return None
            a -= 4
        # whole-function fallback
        vals = set()
        a = f.entry
        while a < before:
            i = self.ins(a)
            _, w = reads_writes(i)
            if reg in w:
                if i.op == 'addiu' and i.rs != reg:
                    j = self.ins(a - 4)
                    if j.op == 'lui' and j.rt == i.rs:
                        vals.add(((j.imm << 16) + i.imm) & 0xffffffff)
                    else:
                        return None
                elif i.op == 'lui':
                    j = self.ins(a + 4)
                    if not (j.op == 'addiu' and j.rs == reg and j.rt == reg):
                        vals.add((i.imm << 16) & 0xffffffff)
                else:
                    return None
            a += 4
        return vals.pop() if len(vals) == 1 else None

    def find_jtable(self, f, jr_addr):
        """Recognise GCC's `sll; addu base; lw; jr` switch idiom.

        Returns a list of target addresses or None.
        """
        reg = self.ins(jr_addr).rs
        lw_at = None
        a = jr_addr - 4
        for _ in range(8):
            i = self.ins(a)
            if i.op == 'lw' and i.rt == reg:
                lw_at = a
                break
            if reg in reads_writes(i)[1]:
                return None
            a -= 4
        if lw_at is None:
            return None
        lw = self.ins(lw_at)
        base = None
        a = lw_at - 4
        for _ in range(8):
            i = self.ins(a)
            if lw.rs in reads_writes(i)[1]:
                if i.op == 'addu':
                    for b in (i.rs, i.rt):
                        c = self.const_of(f, b, a)
                        if c is not None:
                            base = c
                            break
                elif i.op == 'addiu' or i.op == 'lui':
                    base = self.const_of(f, lw.rs, lw_at)
                break
            a -= 4
        if base is None:
            return None
        base = (base + lw.imm) & 0xffffffff
        count = None
        a = lw_at - 4
        for _ in range(16):
            i = self.ins(a)
            if i.op == 'sltiu':
                count = i.imm
                break
            a -= 4
        targets = []
        n = count if count is not None else 512
        for k in range(n):
            ta = base + 4 * k
            if not self.img.contains(ta):
                break
            t = self.img.word(ta)
            if not self.in_code(t) or (count is None and not (f.entry <= t < f.entry + 0x10000)):
                if count is not None:
                    f.bad.append((jr_addr, f'jtable entry {k} -> {t:#x} outside code'))
                break
            targets.append(t)
        return targets or None

    # -- tracing ----------------------------------------------------------
    def trace(self, entry, known):
        f = Func(entry)
        work = [entry]
        seen = set()
        while work:
            a = work.pop()
            while True:
                if a in seen:
                    break
                if not self.in_code(a):
                    f.bad.append((a, 'flow leaves code region'))
                    break
                seen.add(a)
                i = self.ins(a)
                f.insns[a] = i
                if i.op == 'invalid':
                    f.bad.append((a, 'invalid instruction'))
                    break
                if i.has_delay:
                    # The delay slot is decoded here but not marked as a flow
                    # position: a branch may target it, and execution then
                    # continues past it.
                    d = self.ins(a + 4)
                    f.insns[a + 4] = d
                    if d.has_delay:
                        f.bad.append((a, 'branch in delay slot'))
                    o = i.op
                    if o in ('jal',):
                        f.calls.add(i.target)
                        a += 8
                        continue
                    if o in ('bltzal', 'bgezal'):
                        f.calls.add(i.target)
                        a += 8
                        continue
                    if o == 'jalr':
                        a += 8
                        continue
                    if o == 'j':
                        t = i.target
                        if t != entry and (t in known or t in self.external or not self.in_code(t)):
                            f.tailcalls.add(t)
                            break
                        f.labels.add(t)
                        a = t
                        continue
                    if o == 'jr':
                        if i.rs == 31:
                            break
                        tbl = self.find_jtable(f, a)
                        if tbl:
                            f.jtables[a] = tbl
                            for t in tbl:
                                f.labels.add(t)
                                work.append(t)
                        break
                    # conditional branch
                    f.labels.add(i.target)
                    work.append(i.target)
                    if o == 'beq' and i.rs == 0 and i.rt == 0:
                        break  # unconditional `b`
                    if o == 'bgez' and i.rs == 0:
                        break
                    a += 8
                    continue
                a += 4
        return f
