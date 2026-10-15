# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# Lists the textures (size, format) and the sections in CGFX files, to check a converted banner.
# usage: txob_dump.py <banner.cgfx>...
import struct, sys
for path in sys.argv[1:]:
    d = open(path, 'rb').read()
    print(path.split('/')[-2] + '/' + path.split('/')[-1], len(d), 'bytes')
    i = 0
    while (i := d.find(b'TXOB', i)) >= 0:
        typ = struct.unpack_from('<I', d, i - 4)[0]
        if typ == 0x20000011:
            rev, name, udc, udo, h, w, glf, glt, mip, pad, loc, hwf = struct.unpack_from('<12I', d, i + 4)
            print(f'  image texture {w}x{h} hw_format {hwf} gl_format {glf:#x} gl_type {glt:#x} mipmaps {mip} location {loc:#x}')
        i += 4
    for sig in (b'CMDL', b'SOBJ', b'MTOB', b'CANM', b'LUTS', b'SHDR', b'CENV', b'CCAM', b'CFLT'):
        print(f'  {sig.decode()} x{d.count(sig)}', end='')
    print()
