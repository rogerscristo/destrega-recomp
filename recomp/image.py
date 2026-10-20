"""Loading code images (main EXE and overlays) for analysis/codegen."""
import struct
from mips import decode


class Image:
    def __init__(self, name, base, data, entry=None, gp=0):
        self.name, self.base, self.data, self.entry, self.gp = name, base, data, entry, gp
        self.end = base + len(data)

    @classmethod
    def from_psxexe(cls, path, name='main'):
        d = open(path, 'rb').read()
        assert d[:8] == b'PS-X EXE'
        pc0, gp, taddr, tsize = struct.unpack_from('<IIII', d, 0x10)
        return cls(name, taddr, d[0x800:0x800 + tsize], pc0, gp)

    def contains(self, a):
        return self.base <= a < self.end

    def word(self, a):
        return struct.unpack_from('<I', self.data, a - self.base)[0]

    def ins(self, a):
        return decode(self.word(a), a)
