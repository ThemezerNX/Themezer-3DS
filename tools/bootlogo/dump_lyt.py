# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# Dumps BCLYT/BCLAN (CTR layout, version 0x02020000) files: panes, materials, groups, animation curves.
# usage: dump_lyt.py <file.bclyt | file.bclan>...  (darc.py unpacks them from a logo.bin)
import struct, sys

def cstr(b, o, n=None):
    e = b.index(b'\0', o) if n is None else o + (b[o:o + n] + b'\0').index(b'\0')
    return b[o:e].decode('ascii', 'replace')

def dump_clyt(b):
    hsize, nsec = struct.unpack_from('<H', b, 6)[0], struct.unpack_from('<I', b, 16)[0]
    p, depth = hsize, 0
    for _ in range(nsec):
        m, s = struct.unpack_from('<4sI', b, p); m = m.decode(); q = p + 8
        ind = '  ' * depth
        if m == 'lyt1':
            origin, w, h = struct.unpack_from('<Iff', b, q)
            print(f'lyt1 origin {origin} size {w}x{h}')
        elif m in ('txl1', 'fnl1'):
            n = struct.unpack_from('<I', b, q)[0]
            offs = struct.unpack_from(f'<{n}I', b, q + 4)
            print(m, [cstr(b, q + 4 + o) for o in offs])
        elif m == 'mat1':
            n = struct.unpack_from('<I', b, q)[0]
            offs = struct.unpack_from(f'<{n}I', b, q + 4)
            for k, o in enumerate(offs):
                a = p + o
                end = p + (offs[k + 1] if k + 1 < n else s)
                name = cstr(b, a, 20)
                cols = struct.unpack_from('<28B', b, a + 20)
                flags = struct.unpack_from('<I', b, a + 48)[0]
                ntex, nmtx, ngen, ntev = flags & 3, (flags >> 2) & 3, (flags >> 4) & 3, (flags >> 6) & 7
                r = a + 52
                tex = [struct.unpack_from('<HBB', b, r + 4 * i) for i in range(ntex)]; r += 4 * ntex
                mtx = [struct.unpack_from('<5f', b, r + 20 * i) for i in range(nmtx)]; r += 20 * nmtx
                gen = [struct.unpack_from('<4B', b, r + 4 * i) for i in range(ngen)]; r += 4 * ngen
                rest = b[r:end]
                print(f'mat {k} {name!r} flags {flags:#x} (tex {ntex} mtx {nmtx} gen {ngen} tev {ntev} alphacmp {(flags >> 9) & 1} blend {(flags >> 10) & 1} texonly {(flags >> 11) & 1} logic {(flags >> 12) & 1})')
                print(f'    colors {[cols[i:i + 4] for i in range(0, 28, 4)]}')
                print(f'    tex {tex} mtx {[tuple(round(v, 3) for v in m_) for m_ in mtx]} gen {gen} rest {rest.hex()} ({len(rest)} bytes)')
        elif m in ('pan1', 'pic1', 'txt1', 'wnd1', 'bnd1'):
            flags, origin, alpha, scaleflag = struct.unpack_from('<4B', b, q)
            name = cstr(b, q + 4, 16); ud = b[q + 20:q + 28]
            t = struct.unpack_from('<3f', b, q + 28); r_ = struct.unpack_from('<3f', b, q + 40)
            sc = struct.unpack_from('<2f', b, q + 52); sz = struct.unpack_from('<2f', b, q + 60)
            extra = ''
            if m == 'pic1':
                vc = struct.unpack_from('<16B', b, q + 68)
                mat, ntc = struct.unpack_from('<HH', b, q + 84)
                tcs = [struct.unpack_from('<8f', b, q + 88 + 32 * i) for i in range(ntc)]
                extra = f' vcol {[vc[i:i + 4] for i in range(0, 16, 4)]} mat {mat} uv {[tuple(round(v, 3) for v in tc) for tc in tcs]}'
                assert 8 + 88 + 32 * ntc == s, (s, ntc)
            print(f'{ind}{m} {name!r} flags {flags:#x} origin {origin} alpha {alpha} sf {scaleflag} ud {ud.hex()} T {tuple(round(v, 2) for v in t)} R {tuple(round(v, 2) for v in r_)} S {tuple(round(v, 3) for v in sc)} size {tuple(round(v, 2) for v in sz)}{extra}')
        elif m in ('pas1', 'grs1'):
            depth += 1
        elif m in ('pae1', 'gre1'):
            depth -= 1
        elif m == 'grp1':
            name = cstr(b, q, 16); n = struct.unpack_from('<I', b, q + 16)[0]
            panes = [cstr(b, q + 20 + 16 * i, 16) for i in range(n)]
            assert 8 + 20 + 16 * n == s
            print(f'{ind}grp1 {name!r} {panes}')
        else:
            print(f'{ind}{m} {s} bytes: {b[q:p + s].hex()}')
        p += s

TAGS = {'CLPA': 'TX TY TZ RX RY RZ SX SY W H'.split(), 'CLVC': 'TL_R TL_G TL_B TL_A TR_R TR_G TR_B TR_A BL_R BL_G BL_B BL_A BR_R BR_G BR_B BR_A ALPHA'.split(),
        'CLVI': ['VIS'], 'CLMC': 'BLACK_R BLACK_G BLACK_B BLACK_A WHITE_R WHITE_G WHITE_B WHITE_A'.split(), 'CLTS': 'U V R SU SV'.split(), 'CLTP': ['IMG']}

def dump_clan(b):
    hsize, nsec = struct.unpack_from('<H', b, 6)[0], struct.unpack_from('<I', b, 16)[0]
    p = hsize
    for _ in range(nsec):
        m, s = struct.unpack_from('<4sI', b, p); m = m.decode(); q = p + 8
        if m == 'pat1':
            order, ngroups, name_off, groups_off, start, end, child = struct.unpack_from('<HHIIhhB', b, q)
            groups = [cstr(b, p + groups_off + 20 * i, 16) for i in range(ngroups)]
            print(f'pat1 order {order} name {cstr(b, p + name_off)!r} groups {groups} frames {start}..{end} childbinding {child} raw {b[q:p + s].hex()}')
        elif m == 'pai1':
            frames, loop, pad, ntex, nanim, anims_off = struct.unpack_from('<HBBHHI', b, q)
            print(f'pai1 frames {frames} loop {loop} textures {ntex} animated {nanim}')
            if ntex:
                toffs = struct.unpack_from(f'<{ntex}I', b, q + 12)
                print('   textures', [cstr(b, q + 12 + o) for o in toffs])
            aoffs = struct.unpack_from(f'<{nanim}I', b, p + anims_off)
            for ao in aoffs:
                a = p + ao
                name = cstr(b, a, 20); ntag, kind = struct.unpack_from('<BB', b, a + 20)
                toffs = struct.unpack_from(f'<{ntag}I', b, a + 24)
                print(f'  {"material" if kind else "pane"} {name!r}')
                for to in toffs:
                    t = a + to
                    tag = b[t:t + 4].decode(); nent = b[t + 4]
                    eoffs = struct.unpack_from(f'<{nent}I', b, t + 8)
                    for eo in eoffs:
                        e = t + eo
                        idx, target, ctype, pad_, nkeys, pad2, koff = struct.unpack_from('<BBBBHHI', b, e)
                        keys = []
                        for k in range(nkeys):
                            if ctype == 2:
                                keys.append(tuple(round(v, 2) for v in struct.unpack_from('<3f', b, e + koff + 12 * k)))
                            else:
                                f_, v_ = struct.unpack_from('<fH', b, e + koff + 8 * k); keys.append((round(f_, 2), v_))
                        tn = TAGS.get(tag, [])
                        print(f'    {tag} {tn[target] if target < len(tn) else target} idx {idx} curve {ctype} keys {keys}')
        p += s

for path in sys.argv[1:]:
    b = open(path, 'rb').read()
    print('=====', path.split('/')[-1])
    (dump_clyt if b[:4] == b'CLYT' else dump_clan)(b)
