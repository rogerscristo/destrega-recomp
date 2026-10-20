#!/usr/bin/env python3
"""Wrap a PS-X EXE (or a raw overlay given --load) in a MIPS32 LE ELF executable.

Usage:
  psxexe2elf.py <SLUS_xxx.xx> <out.elf>
  psxexe2elf.py --raw <overlay.bin> --load 0x80xxxxxx <out.elf>
"""
import argparse
import struct


def build_elf(code, vaddr, entry):
    ehsz, phsz, shsz = 52, 32, 40
    # EF_MIPS_ARCH_32 (no ABI bits): R3000 code decodes as a MIPS32 subset,
    # and REA's Ghidra lane only admits ARCH_32/32R2 headers.
    e_flags = 0x50000000
    shstr = b'\x00.text\x00.shstrtab\x00'
    code_off = 0x1000
    shstr_off = code_off + len(code)
    sh_off = (shstr_off + len(shstr) + 3) & ~3
    eh = b'\x7fELF' + bytes([1, 1, 1, 0]) + bytes(8)
    eh += struct.pack('<HHIIIIIHHHHHH', 2, 8, 1, entry, ehsz, sh_off,
                      e_flags, ehsz, phsz, 1, shsz, 3, 2)
    ph = struct.pack('<IIIIIIII', 1, code_off, vaddr, vaddr, len(code),
                     len(code), 7, 0x1000)
    out = bytearray(eh + ph)
    out += bytes(code_off - len(out))
    out += code + shstr
    out += bytes(sh_off - len(out))
    out += bytes(shsz)
    out += struct.pack('<IIIIIIIIII', 1, 1, 7, vaddr, code_off, len(code), 0, 0, 16, 0)
    out += struct.pack('<IIIIIIIIII', 7, 3, 0, 0, shstr_off, len(shstr), 0, 0, 1, 0)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--raw', action='store_true')
    ap.add_argument('--load', type=lambda s: int(s, 0))
    a = ap.parse_args()
    data = open(a.src, 'rb').read()
    if a.raw:
        code, vaddr, entry = data, a.load, a.load
    else:
        assert data[:8] == b'PS-X EXE', 'not a PS-X EXE'
        pc0, gp, taddr, tsize = struct.unpack_from('<IIII', data, 0x10)
        code, vaddr, entry = data[0x800:0x800 + tsize], taddr, pc0
        print(f'pc0={pc0:#x} gp={gp:#x} t_addr={taddr:#x} t_size={tsize:#x}')
    open(a.dst, 'wb').write(build_elf(code, vaddr, entry))


if __name__ == '__main__':
    main()
