"""MIPS-I (R3000A + GTE) instruction decoder shared by analysis and codegen."""
from dataclasses import dataclass

REG = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3',
       't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
       's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7',
       't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


@dataclass
class Ins:
    addr: int
    word: int
    op: str          # mnemonic
    rs: int = 0
    rt: int = 0
    rd: int = 0
    sa: int = 0
    imm: int = 0     # sign- or zero-extended per op
    target: int = 0  # absolute branch/jump target

    @property
    def is_branch(self):
        return self.op in BRANCHES

    @property
    def has_delay(self):
        return self.op in BRANCHES or self.op in JUMPS

    def __str__(self):
        return disasm(self)


BRANCHES = {'beq', 'bne', 'blez', 'bgtz', 'bltz', 'bgez', 'bltzal', 'bgezal'}
JUMPS = {'j', 'jal', 'jr', 'jalr'}

_SPECIAL = {
    0x00: 'sll', 0x02: 'srl', 0x03: 'sra', 0x04: 'sllv', 0x06: 'srlv', 0x07: 'srav',
    0x08: 'jr', 0x09: 'jalr', 0x0c: 'syscall', 0x0d: 'break',
    0x10: 'mfhi', 0x11: 'mthi', 0x12: 'mflo', 0x13: 'mtlo',
    0x18: 'mult', 0x19: 'multu', 0x1a: 'div', 0x1b: 'divu',
    0x20: 'add', 0x21: 'addu', 0x22: 'sub', 0x23: 'subu',
    0x24: 'and', 0x25: 'or', 0x26: 'xor', 0x27: 'nor', 0x2a: 'slt', 0x2b: 'sltu',
}
_PRIMARY = {
    0x08: 'addi', 0x09: 'addiu', 0x0a: 'slti', 0x0b: 'sltiu',
    0x0c: 'andi', 0x0d: 'ori', 0x0e: 'xori', 0x0f: 'lui',
    0x20: 'lb', 0x21: 'lh', 0x22: 'lwl', 0x23: 'lw', 0x24: 'lbu', 0x25: 'lhu', 0x26: 'lwr',
    0x28: 'sb', 0x29: 'sh', 0x2a: 'swl', 0x2b: 'sw', 0x2e: 'swr',
    0x32: 'lwc2', 0x3a: 'swc2',
}
_ZEXT = {'andi', 'ori', 'xori', 'lui'}


def decode(word, addr):
    op = word >> 26
    rs = (word >> 21) & 31
    rt = (word >> 16) & 31
    rd = (word >> 11) & 31
    sa = (word >> 6) & 31
    imm16 = word & 0xffff
    if op == 0:
        fn = word & 63
        m = _SPECIAL.get(fn)
        if m is None:
            return Ins(addr, word, 'invalid')
        return Ins(addr, word, m, rs, rt, rd, sa)
    if op == 1:
        m = {0x00: 'bltz', 0x01: 'bgez', 0x10: 'bltzal', 0x11: 'bgezal'}.get(rt)
        if m is None:
            # R3000 decodes any rt with bit0 as bgez-ish; treat others invalid
            return Ins(addr, word, 'invalid')
        return Ins(addr, word, m, rs, rt, imm=sx16(imm16),
                   target=(addr + 4 + (sx16(imm16) << 2)) & 0xffffffff)
    if op in (2, 3):
        t = ((addr + 4) & 0xf0000000) | ((word & 0x3ffffff) << 2)
        return Ins(addr, word, 'j' if op == 2 else 'jal', target=t)
    if op in (4, 5, 6, 7):
        m = ['beq', 'bne', 'blez', 'bgtz'][op - 4]
        return Ins(addr, word, m, rs, rt, imm=sx16(imm16),
                   target=(addr + 4 + (sx16(imm16) << 2)) & 0xffffffff)
    if op == 0x10:  # COP0
        if rs == 0:
            return Ins(addr, word, 'mfc0', rs, rt, rd)
        if rs == 4:
            return Ins(addr, word, 'mtc0', rs, rt, rd)
        if rs == 0x10 and (word & 63) == 0x10:
            return Ins(addr, word, 'rfe')
        return Ins(addr, word, 'invalid')
    if op == 0x12:  # COP2 / GTE
        if rs & 0x10:
            return Ins(addr, word, 'cop2', imm=word & 0x1ffffff)
        m = {0: 'mfc2', 2: 'cfc2', 4: 'mtc2', 6: 'ctc2'}.get(rs)
        if m is None:
            return Ins(addr, word, 'invalid')
        return Ins(addr, word, m, rs, rt, rd)
    m = _PRIMARY.get(op)
    if m is None:
        return Ins(addr, word, 'invalid')
    imm = imm16 if m in _ZEXT else sx16(imm16)
    return Ins(addr, word, m, rs, rt, imm=imm)


def disasm(i):
    r = REG
    o = i.op
    if o in ('sll', 'srl', 'sra'):
        if i.word == 0:
            return 'nop'
        return f'{o} {r[i.rd]}, {r[i.rt]}, {i.sa}'
    if o in ('sllv', 'srlv', 'srav'):
        return f'{o} {r[i.rd]}, {r[i.rt]}, {r[i.rs]}'
    if o == 'jr':
        return f'jr {r[i.rs]}'
    if o == 'jalr':
        return f'jalr {r[i.rd]}, {r[i.rs]}'
    if o in ('mfhi', 'mflo'):
        return f'{o} {r[i.rd]}'
    if o in ('mthi', 'mtlo'):
        return f'{o} {r[i.rs]}'
    if o in ('mult', 'multu', 'div', 'divu'):
        return f'{o} {r[i.rs]}, {r[i.rt]}'
    if o in _SPECIAL.values():
        if o in ('syscall', 'break'):
            return o
        return f'{o} {r[i.rd]}, {r[i.rs]}, {r[i.rt]}'
    if o in ('beq', 'bne'):
        return f'{o} {r[i.rs]}, {r[i.rt]}, 0x{i.target:08x}'
    if o in ('blez', 'bgtz', 'bltz', 'bgez', 'bltzal', 'bgezal'):
        return f'{o} {r[i.rs]}, 0x{i.target:08x}'
    if o in ('j', 'jal'):
        return f'{o} 0x{i.target:08x}'
    if o == 'lui':
        return f'lui {r[i.rt]}, 0x{i.imm:04x}'
    if o in ('andi', 'ori', 'xori'):
        return f'{o} {r[i.rt]}, {r[i.rs]}, 0x{i.imm:x}'
    if o in ('addi', 'addiu', 'slti', 'sltiu'):
        return f'{o} {r[i.rt]}, {r[i.rs]}, {i.imm}'
    if o in ('lwc2', 'swc2'):
        return f'{o} ${i.rt}, {i.imm}({r[i.rs]})'
    if o in _PRIMARY.values():
        return f'{o} {r[i.rt]}, {i.imm}({r[i.rs]})'
    if o in ('mfc0', 'mtc0', 'mfc2', 'mtc2', 'cfc2', 'ctc2'):
        return f'{o} {r[i.rt]}, ${i.rd}'
    if o == 'cop2':
        return f'cop2 0x{i.imm:07x}'
    if o == 'rfe':
        return 'rfe'
    return f'.word 0x{i.word:08x}'


def reads_writes(i):
    """Return (reads, writes) GPR sets for simple dataflow."""
    o = i.op
    if o in ('sll', 'srl', 'sra'):
        return {i.rt}, {i.rd}
    if o in ('sllv', 'srlv', 'srav', 'add', 'addu', 'sub', 'subu', 'and', 'or',
             'xor', 'nor', 'slt', 'sltu'):
        return {i.rs, i.rt}, {i.rd}
    if o == 'jr':
        return {i.rs}, set()
    if o == 'jalr':
        return {i.rs}, {i.rd}
    if o in ('mfhi', 'mflo'):
        return set(), {i.rd}
    if o in ('mthi', 'mtlo'):
        return {i.rs}, set()
    if o in ('mult', 'multu', 'div', 'divu', 'beq', 'bne'):
        return {i.rs, i.rt}, set()
    if o in ('blez', 'bgtz', 'bltz', 'bgez'):
        return {i.rs}, set()
    if o in ('bltzal', 'bgezal'):
        return {i.rs}, {31}
    if o == 'jal':
        return set(), {31}
    if o in ('addi', 'addiu', 'slti', 'sltiu', 'andi', 'ori', 'xori'):
        return {i.rs}, {i.rt}
    if o == 'lui':
        return set(), {i.rt}
    if o in ('lb', 'lh', 'lw', 'lbu', 'lhu'):
        return {i.rs}, {i.rt}
    if o in ('lwl', 'lwr'):
        return {i.rs, i.rt}, {i.rt}
    if o in ('sb', 'sh', 'sw', 'swl', 'swr'):
        return {i.rs, i.rt}, set()
    if o in ('lwc2', 'swc2'):
        return {i.rs}, set()
    if o in ('mfc0', 'mfc2', 'cfc2'):
        return set(), {i.rt}
    if o in ('mtc0', 'mtc2', 'ctc2'):
        return {i.rt}, set()
    return set(), set()
