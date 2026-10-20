#!/usr/bin/env python3
"""Extract the ISO9660 filesystem from a PS1 MODE2/2352 BIN image or from the
runtime's data pack (destrega.pak, see runtime/disc.c).

Usage: extract_disc.py <image.bin|destrega.pak> <out_dir>

Files are written with their 2048-byte user data (Form 1). Files flagged as
XA/interleaved (Form 2 sectors, e.g. STR/XA streams) are additionally dumped
as raw 2352-byte sectors to <name>.raw so nothing is lost.
"""
import os
import struct
import sys
import zlib

SECTOR = 2352


class Pack:  # noqa: D101
    """Read-only view of a destrega.pak with the file-like seek/read the
    extractor uses on raw images."""

    def __init__(self, f):
        self.f = f
        f.seek(8)
        self.sectors, self.bs, nb, _ = struct.unpack('<4I', f.read(16))
        self.offs = struct.unpack(f'<{nb + 1}Q', f.read(8 * (nb + 1)))
        self.pos = 0
        self.cache = (None, b'')

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.f.close()

    def seek(self, pos):
        self.pos = pos

    def read(self, n):
        out = bytearray()
        while n > 0:
            lba, off = divmod(self.pos, SECTOR)
            b = lba // self.bs
            if self.cache[0] != b:
                self.f.seek(self.offs[b])
                self.cache = (b, zlib.decompress(self.f.read(self.offs[b + 1] - self.offs[b])))
            blk = self.cache[1]
            start = (lba % self.bs) * SECTOR + off
            take = min(n, len(blk) - start)
            if take <= 0:
                break
            out += blk[start:start + take]
            self.pos += take
            n -= take
        return bytes(out)


def open_image(path):
    f = open(path, 'rb')
    if f.read(8) == b'DSTRPAK1':
        return Pack(f)
    f.seek(0)
    return f


def read_sector(f, lba):
    f.seek(lba * SECTOR)
    return f.read(SECTOR)


def user_data(raw):
    # MODE2 XA: 12 sync + 4 header + 8 subheader, then data.
    submode = raw[18]
    if submode & 0x20:  # Form 2
        return raw[24:24 + 2324], True
    return raw[24:24 + 2048], False


def read_extent(f, lba, size):
    out = bytearray()
    form2 = False
    n = (size + 2047) // 2048
    for i in range(n):
        data, f2 = user_data(read_sector(f, lba + i))
        form2 |= f2
        out += data[:2048]
    return bytes(out[:size]), form2


def walk(f, lba, size, path, out_dir, listing):
    data, _ = read_extent(f, lba, size)
    pos = 0
    while pos < len(data):
        ln = data[pos]
        if ln == 0:
            pos = (pos // 2048 + 1) * 2048
            continue
        rec = data[pos:pos + ln]
        ext = struct.unpack_from('<I', rec, 2)[0]
        sz = struct.unpack_from('<I', rec, 10)[0]
        flags = rec[25]
        nl = rec[32]
        name = rec[33:33 + nl]
        pos += ln
        if name in (b'\x00', b'\x01'):
            continue
        name = name.decode('ascii', 'replace').split(';')[0]
        full = os.path.join(path, name)
        if flags & 2:
            os.makedirs(os.path.join(out_dir, full), exist_ok=True)
            walk(f, ext, sz, full, out_dir, listing)
        else:
            body, form2 = read_extent(f, ext, sz)
            with open(os.path.join(out_dir, full), 'wb') as o:
                o.write(body)
            if form2:
                nsec = (sz + 2047) // 2048
                f.seek(ext * SECTOR)
                with open(os.path.join(out_dir, full + '.raw'), 'wb') as o:
                    o.write(f.read(nsec * SECTOR))
            listing.append((full, ext, sz, form2))


def main():
    img, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    with open_image(img) as f:
        pvd, _ = user_data(read_sector(f, 16))
        assert pvd[1:6] == b'CD001', 'no ISO9660 PVD'
        root = pvd[156:156 + 34]
        lba = struct.unpack_from('<I', root, 2)[0]
        sz = struct.unpack_from('<I', root, 10)[0]
        listing = []
        walk(f, lba, sz, '', out_dir, listing)
    with open(os.path.join(out_dir, '_listing.txt'), 'w') as o:
        for p, e, s, x in listing:
            o.write(f'{e:8d} {s:10d} {"XA" if x else "  "} {p}\n')
    for p, e, s, x in listing:
        print(f'{e:8d} {s:10d} {"XA" if x else "  "} {p}')


if __name__ == '__main__':
    main()
