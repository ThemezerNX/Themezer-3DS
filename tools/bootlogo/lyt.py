# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# Reads and writes CTR layouts (BCLYT) and layout animations (BCLAN), version 0x02020000, as used by the 3DS boot
# logo (logo.bin). Only the sections the logo uses: lyt1, txl1, mat1, pan1/pic1 (with pas1/pae1), grp1 (with grs1/gre1);
# pat1 and pai1 with Hermite curves.
import struct
from dataclasses import dataclass, field


def _name(b, o, n):
    return (b[o:o + n] + b'\0').split(b'\0')[0].decode('ascii')


def _fixed(s, n):
    e = s.encode('ascii')
    assert len(e) < n, s
    return e + b'\0' * (n - len(e))


def _pad4(b):
    return b + b'\0' * (-len(b) % 4)


@dataclass
class Material:
    name: str
    colors: list            # 7 RGBA tuples: black colour (TEV buffer), then 6 TEV constants (index 1 is the white colour)
    texmaps: list           # (texture index, flags S, flags T): wrap in bits 0-1 (0 clamp, 1 repeat, 2 mirror), filter bit 2 (linear)
    srts: list              # (translate u, translate v, rotate, scale u, scale v)
    gens: list              # 4-byte texture coordinate generators
    tail: bytes = b''       # TEV stages, alpha compare, blend modes as stored
    flags_high: int = 0     # flag bits 6 and up (TEV stage count, alpha compare, blend, ...)

    def write(self):
        flags = len(self.texmaps) | len(self.srts) << 2 | len(self.gens) << 4 | self.flags_high
        out = _fixed(self.name, 20) + b''.join(bytes(c) for c in self.colors) + struct.pack('<I', flags)
        out += b''.join(struct.pack('<HBB', *t) for t in self.texmaps)
        out += b''.join(struct.pack('<5f', *s) for s in self.srts)
        out += b''.join(bytes(g) for g in self.gens)
        return out + self.tail


@dataclass
class Pane:
    name: str
    kind: str = 'pan1'
    flags: int = 1          # bit 0 visible, bit 1 children take this pane's alpha
    origin: int = 4         # horizontal + 3 * vertical (0 left/top, 1 centre, 2 right/bottom)
    alpha: int = 255
    scale_flag: int = 0
    user: bytes = b'\0' * 8
    t: tuple = (0.0, 0.0, 0.0)
    r: tuple = (0.0, 0.0, 0.0)
    s: tuple = (1.0, 1.0)
    size: tuple = (0.0, 0.0)
    vcol: list = None       # pic1: 4 RGBA tuples, top left, top right, bottom left, bottom right
    mat: int = 0
    uvs: list = None        # pic1: sets of 8 floats (u, v of the same 4 corners)
    children: list = field(default_factory=list)

    def write_body(self):
        out = struct.pack('<4B', self.flags, self.origin, self.alpha, self.scale_flag) + _fixed(self.name, 16) + self.user
        out += struct.pack('<3f3f2f2f', *self.t, *self.r, *self.s, *self.size)
        if self.kind == 'pic1':
            out += b''.join(bytes(c) for c in self.vcol) + struct.pack('<HH', self.mat, len(self.uvs))
            out += b''.join(struct.pack('<8f', *uv) for uv in self.uvs)
        return out


@dataclass
class Group:
    name: str
    panes: list
    children: list = field(default_factory=list)


@dataclass
class Layout:
    width: float
    height: float
    textures: list
    materials: list
    root: Pane
    groups: Group
    origin: int = 1


def _section(magic, body):
    body = _pad4(body)
    return magic + struct.pack('<I', 8 + len(body)) + body


def parse_clyt(b):
    hsize, nsec = struct.unpack_from('<H', b, 6)[0], struct.unpack_from('<I', b, 16)[0]
    p = hsize
    lay = Layout(0, 0, [], [], None, None)
    stack, last, gstack, glast = [], None, [], None
    for _ in range(nsec):
        m, s = struct.unpack_from('<4sI', b, p); m = m.decode(); q = p + 8
        if m == 'lyt1':
            lay.origin, lay.width, lay.height = struct.unpack_from('<Iff', b, q)
        elif m == 'txl1':
            n = struct.unpack_from('<I', b, q)[0]
            offs = struct.unpack_from(f'<{n}I', b, q + 4)
            lay.textures = [b[q + 4 + o:b.index(b'\0', q + 4 + o)].decode() for o in offs]
        elif m == 'mat1':
            n = struct.unpack_from('<I', b, q)[0]
            offs = struct.unpack_from(f'<{n}I', b, q + 4)
            for k, o in enumerate(offs):
                a, end = p + o, p + (offs[k + 1] if k + 1 < n else s)
                cols = struct.unpack_from('<28B', b, a + 20)
                flags = struct.unpack_from('<I', b, a + 48)[0]
                nt, nm, ng = flags & 3, (flags >> 2) & 3, (flags >> 4) & 3
                r = a + 52
                tex = [struct.unpack_from('<HBB', b, r + 4 * i) for i in range(nt)]; r += 4 * nt
                srt = [struct.unpack_from('<5f', b, r + 20 * i) for i in range(nm)]; r += 20 * nm
                gen = [tuple(b[r + 4 * i:r + 4 * i + 4]) for i in range(ng)]; r += 4 * ng
                lay.materials.append(Material(_name(b, a, 20), [cols[i:i + 4] for i in range(0, 28, 4)], tex, srt, gen, b[r:end], flags & ~0x3F))
        elif m in ('pan1', 'pic1'):
            f = struct.unpack_from('<4B', b, q)
            pane = Pane(_name(b, q + 4, 16), m, *f, b[q + 20:q + 28], struct.unpack_from('<3f', b, q + 28), struct.unpack_from('<3f', b, q + 40),
                        struct.unpack_from('<2f', b, q + 52), struct.unpack_from('<2f', b, q + 60))
            if m == 'pic1':
                v = struct.unpack_from('<16B', b, q + 68)
                pane.vcol = [v[i:i + 4] for i in range(0, 16, 4)]
                pane.mat, n = struct.unpack_from('<HH', b, q + 84)
                pane.uvs = [struct.unpack_from('<8f', b, q + 88 + 32 * i) for i in range(n)]
            if stack: stack[-1].children.append(pane)
            else: lay.root = pane
            last = pane
        elif m == 'pas1': stack.append(last)
        elif m == 'pae1': stack.pop()
        elif m == 'grp1':
            n = struct.unpack_from('<I', b, q + 16)[0]
            g = Group(_name(b, q, 16), [_name(b, q + 20 + 16 * i, 16) for i in range(n)])
            if gstack: gstack[-1].children.append(g)
            else: lay.groups = g
            glast = g
        elif m == 'grs1': gstack.append(glast)
        elif m == 'gre1': gstack.pop()
        else:
            raise ValueError(m)
        p += s
    return lay


def write_clyt(lay):
    secs = [_section(b'lyt1', struct.pack('<Iff', lay.origin, lay.width, lay.height))]
    names = b''
    offs = []
    for t in lay.textures:
        offs.append(4 * len(lay.textures) + len(names)); names += t.encode() + b'\0'
    secs.append(_section(b'txl1', struct.pack('<I', len(lay.textures)) + struct.pack(f'<{len(offs)}I', *offs) + names))
    mats = [m.write() for m in lay.materials]
    o, offs = 8 + 4 + 4 * len(mats), []
    for m in mats:
        offs.append(o); o += len(m)
    secs.append(_section(b'mat1', struct.pack('<I', len(mats)) + struct.pack(f'<{len(offs)}I', *offs) + b''.join(mats)))

    def panes(p):
        out = [_section(p.kind.encode(), p.write_body())]
        if p.children:
            out.append(_section(b'pas1', b''))
            for c in p.children: out += panes(c)
            out.append(_section(b'pae1', b''))
        return out
    secs += panes(lay.root)

    def groups(g):
        out = [_section(b'grp1', _fixed(g.name, 16) + struct.pack('<I', len(g.panes)) + b''.join(_fixed(n, 16) for n in g.panes))]
        if g.children:
            out.append(_section(b'grs1', b''))
            for c in g.children: out += groups(c)
            out.append(_section(b'gre1', b''))
        return out
    secs += groups(lay.groups)
    body = b''.join(secs)
    return b'CLYT' + struct.pack('<HHIII', 0xFEFF, 0x14, 0x02020000, 0x14 + len(body), len(secs)) + body


# animation targets
PANE_SRT = 'CLPA'      # 0 TX 1 TY 2 TZ 3 RX 4 RY 5 RZ 6 SX 7 SY 8 W 9 H
VTX_COLOR = 'CLVC'     # 0-15 corner colours (TL, TR, BL, BR, RGBA each), 16 pane alpha
TEX_SRT = 'CLTS'       # 0 U 1 V 2 R 3 SU 4 SV, index = texture SRT
MAT_COLOR = 'CLMC'
TX, TY, TZ, RX, RY, RZ, SX, SY, W, H = range(10)
ALPHA = 16


@dataclass
class Curve:
    tag: str
    target: int
    keys: list              # (frame, value, slope)
    index: int = 0
    kind: int = 2           # 2 Hermite


@dataclass
class AnimTarget:
    name: str
    curves: list
    is_material: bool = False


@dataclass
class Anim:
    name: str
    order: int
    groups: list
    start: int
    end: int
    frames: int
    loop: bool
    targets: list
    child_binding: int = 1
    group_flags: list = None


def parse_clan(b):
    hsize, nsec = struct.unpack_from('<H', b, 6)[0], struct.unpack_from('<I', b, 16)[0]
    p = hsize
    an = Anim('', 0, [], 0, 0, 0, False, [])
    for _ in range(nsec):
        m, s = struct.unpack_from('<4sI', b, p); m = m.decode(); q = p + 8
        if m == 'pat1':
            an.order, ng, no, go, an.start, an.end, an.child_binding = struct.unpack_from('<HHIIhhB', b, q)
            an.name = b[p + no:b.index(b'\0', p + no)].decode()
            an.groups = [_name(b, p + go + 20 * i, 16) for i in range(ng)]
            an.group_flags = [b[p + go + 20 * i + 16:p + go + 20 * i + 20] for i in range(ng)]
        elif m == 'pai1':
            an.frames, loop, _, ntex, nanim, ao = struct.unpack_from('<HBBHHI', b, q)
            an.loop = bool(loop)
            assert ntex == 0
            for o in struct.unpack_from(f'<{nanim}I', b, p + ao):
                a = p + o
                ntag, kind = struct.unpack_from('<BB', b, a + 20)
                tgt = AnimTarget(_name(b, a, 20), [], bool(kind))
                for to in struct.unpack_from(f'<{ntag}I', b, a + 24):
                    t = a + to
                    tag, nent = b[t:t + 4].decode(), b[t + 4]
                    for eo in struct.unpack_from(f'<{nent}I', b, t + 8):
                        e = t + eo
                        idx, target, kind_, _, nkeys, _, ko = struct.unpack_from('<BBBBHHI', b, e)
                        assert kind_ == 2 and ko == 12
                        tgt.curves.append(Curve(tag, target, [struct.unpack_from('<3f', b, e + ko + 12 * k) for k in range(nkeys)], idx, kind_))
                an.targets.append(tgt)
        p += s
    return an


def write_clan(an):
    name = _pad4(an.name.encode() + b'\0')
    flags = an.group_flags or [b'\0' * 4] * len(an.groups)
    pat = struct.pack('<HHIIhhB3x', an.order, len(an.groups), 28, 28 + len(name), an.start, an.end, an.child_binding) + name
    pat += b''.join(_fixed(g, 16) + f for g, f in zip(an.groups, flags))
    contents = []
    for tgt in an.targets:
        tags = []
        for tag in dict.fromkeys(c.tag for c in tgt.curves):
            curves = [c for c in tgt.curves if c.tag == tag]
            entries = [struct.pack('<BBBBHHI', c.index, c.target, c.kind, 0, len(c.keys), 0, 12) + b''.join(struct.pack('<3f', *k) for k in c.keys) for c in curves]
            o, offs = 8 + 4 * len(entries), []
            for e in entries:
                offs.append(o); o += len(e)
            tags.append(tag.encode() + struct.pack('<B3x', len(entries)) + struct.pack(f'<{len(offs)}I', *offs) + b''.join(entries))
        o, offs = 24 + 4 * len(tags), []
        for t in tags:
            offs.append(o); o += len(t)
        contents.append(_fixed(tgt.name, 20) + struct.pack('<BBH', len(tags), int(tgt.is_material), 0) + struct.pack(f'<{len(offs)}I', *offs) + b''.join(tags))
    o, offs = 8 + 12 + 4 * len(contents), []
    for c in contents:
        offs.append(o); o += len(c)
    pai = struct.pack('<HBBHHI', an.frames, int(an.loop), 0, 0, len(contents), 20) + struct.pack(f'<{len(offs)}I', *offs) + b''.join(contents)
    body = _section(b'pat1', pat) + _section(b'pai1', pai)
    return b'CLAN' + struct.pack('<HHIII', 0xFEFF, 0x14, 0x02020000, 0x14 + len(body), 2) + body


if __name__ == '__main__':
    import sys
    for path in sys.argv[1:]:
        b = open(path, 'rb').read()
        w = write_clyt(parse_clyt(b)) if b[:4] == b'CLYT' else write_clan(parse_clan(b))
        same = w == b
        diff = next((i for i in range(min(len(w), len(b))) if w[i] != b[i]), None)
        print(path.split('/')[-1], 'round trip', 'identical' if same else f'DIFFERS at {diff} (lengths {len(b)} vs {len(w)})')
