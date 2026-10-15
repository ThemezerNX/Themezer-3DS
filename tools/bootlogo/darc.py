# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# Unpacks an LZ11-compressed DARC (3DS logo.bin) and lists its files.
# usage: darc.py <logo.bin>  -> <logo.bin>.darc
import struct, sys
def lz11_decompress(data):
    assert data[0] == 0x11
    size = data[1] | data[2] << 8 | data[3] << 16
    pos = 4
    if size == 0:
        size = struct.unpack_from('<I', data, 4)[0]; pos = 8
    out = bytearray()
    while len(out) < size:
        flags = data[pos]; pos += 1
        for bit in range(8):
            if len(out) >= size: break
            if flags & (0x80 >> bit):
                b = data[pos] >> 4
                if b == 0:
                    length = (((data[pos] & 0xF) << 4) | (data[pos+1] >> 4)) + 0x11
                    disp = (((data[pos+1] & 0xF) << 8) | data[pos+2]) + 1; pos += 3
                elif b == 1:
                    length = (((data[pos] & 0xF) << 12) | (data[pos+1] << 4) | (data[pos+2] >> 4)) + 0x111
                    disp = (((data[pos+2] & 0xF) << 8) | data[pos+3]) + 1; pos += 4
                else:
                    length = b + 1
                    disp = (((data[pos] & 0xF) << 8) | data[pos+1]) + 1; pos += 2
                for _ in range(length):
                    out.append(out[-disp])
            else:
                out.append(data[pos]); pos += 1
    return bytes(out)
def darc_files(d):
    assert d[:4] == b'darc'
    hdr_len = struct.unpack_from('<H', d, 6)[0]
    table_off, table_len, data_off = struct.unpack_from('<III', d, 0x10)
    first = struct.unpack_from('<III', d, table_off)
    count = first[2]
    names_off = table_off + count * 12
    files, stack = [], []
    for i in range(count):
        name_off, off, size = struct.unpack_from('<III', d, table_off + i * 12)
        is_dir = name_off & 0x01000000
        name_off &= 0xFFFFFF
        n = names_off + name_off
        end = n
        while d[end:end+2] != b'\0\0': end += 2
        name = d[n:end].decode('utf-16-le')
        files.append((i, name, bool(is_dir), off, size))
    return files
raw = open(sys.argv[1], 'rb').read()
d = lz11_decompress(raw)
open(sys.argv[1] + '.darc', 'wb').write(d)
for i, name, is_dir, off, size in darc_files(d):
    extra = ''
    if not is_dir and d[off+size-0x28:off+size-0x24] == b'CLIM':
        imag = off + size - 0x14
        w, h, fmt = struct.unpack_from('<HHI', d, imag + 8)
        extra = f'  BCLIM {w}x{h} format {fmt}'
    print(f'{i:2} {"dir " if is_dir else "file"} {name:30} off {off:6} size {size:6}{extra}')
