# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# Builds Themezer's boot logo (logo.bin) after the Homebrew App Store icon (design/hbappstore/icon.svg): the icon's
# chevron background moving up on both screens, its logo and wordmark over it on the top screen, faded in from black and out
# to black.
# usage: build_logo.py <fg.png> <out dir> [--key <hmac key file>]
# Input: fg.png, the icon rendered at 2560x1500 (10x) with its BG layer hidden: the logo and the wordmark. The chevrons
# are drawn from the background's pattern cell, not from a render. Layouts, animations and textures go into a DARC whose
# last 32 bytes are an HMAC-SHA256 of the rest (the HOME Menu panics at launch otherwise), LZ11-compressed into exactly
# 0x2000 bytes.
import hashlib, hmac, math, os, pickle, struct, sys
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from lyt import Layout, Material, Pane, Group, Anim, AnimTarget, Curve, write_clyt, write_clan, PANE_SRT, VTX_COLOR, TY, TZ, ALPHA
from lz11 import lz11_compress, lz11_decompress

FG, OUT = sys.argv[1], sys.argv[2]
KEY = sys.argv[sys.argv.index('--key') + 1] if '--key' in sys.argv else None
os.makedirs(OUT, exist_ok=True)

SCALE = 1.6                 # the 256x150 icon on the 240-pixel-high top screen
RENDER = 10.0               # render pixels per icon pixel
PX = RENDER / SCALE         # render pixels per screen pixel

# ---------------------------------------------------------------- textures


def po2(n):
    p = 8
    while p < n: p *= 2
    return p


def morton_xy(m):
    return sum(((m >> (2 * b)) & 1) << b for b in range(3)), sum(((m >> (2 * b + 1)) & 1) << b for b in range(3))


def clim(data, w, h, fmt):
    return data + b'CLIM' + struct.pack('<HHIII', 0xFEFF, 0x14, 0x02020000, len(data) + 0x28, 1) + b'imag' + struct.pack('<IHHII', 0x10, w, h, fmt, len(data))


def bclim_a4(alpha):
    # alpha: 0..1, rows top first, power-of-two size; 8x8 tiles in rows, Morton order inside, low nibble first
    h, w = alpha.shape
    assert (w, h) == (po2(w), po2(h)), (w, h)
    q = np.clip(np.round(alpha * 15), 0, 15).astype(int)
    out = bytearray()
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for p in range(0, 64, 2):
                (x0, y0), (x1, y1) = morton_xy(p), morton_xy(p + 1)
                out.append(q[ty + y0, tx + x0] | q[ty + y1, tx + x1] << 4)
    return clim(bytes(out), w, h, 13), q / 15


def resample(a, w, h):
    return np.asarray(Image.fromarray(np.round(np.clip(a, 0, 1) * 255).astype(np.uint8)).resize((w, h), Image.Resampling.LANCZOS)).astype(float) / 255


fg = np.asarray(Image.open(FG).convert('RGBA')).astype(float) / 255
FG_H, FG_W = fg.shape[:2]
alpha_fg = fg[..., 3]

# logo and wordmark: the logo above the first empty row band, the wordmark below it
rows = alpha_fg.sum(1)
content = np.nonzero(rows > 0)[0]
gap = [y for y in range(content[0], content[-1]) if rows[y] == 0]
split = (gap[0] + gap[-1]) // 2


def bbox(y0, y1):
    ys, xs = np.nonzero(alpha_fg[y0:y1] > 0)
    return xs.min(), xs.max() + 1, y0 + ys.min(), y0 + ys.max() + 1


LX0, LX1, LY0, LY1 = bbox(0, split)
WX0, WX1, WY0, WY1 = bbox(split, FG_H)


def screen(x, y):
    # render pixel -> top screen position (origin at the centre, y up)
    return (x - FG_W / 2) / PX, (FG_H / 2 - y) / PX




def snap(size, centre):
    # a texel per screen pixel: the pane as many pixels wide and high as its texture, its edges on pixel edges
    t = round(size[0]), round(size[1])
    left, bottom = round(centre[0] - t[0] / 2), round(centre[1] - t[1] / 2)
    return t, (float(t[0]), float(t[1])), (left + t[0] / 2, bottom + t[1] / 2)


LOGO_EXACT = ((LX1 - LX0) / PX, (LY1 - LY0) / PX)
LOGO_T, LOGO_SIZE, LOGO_POS = snap(LOGO_EXACT, screen((LX0 + LX1) / 2, (LY0 + LY1) / 2))
WORD_T, WORD_SIZE, WORD_POS = snap(((WX1 - WX0) / PX, (WY1 - WY0) / PX), screen((WX0 + WX1) / 2, (WY0 + WY1) / 2))

# the gradient, from the SVG: #ff00ce to #00d079 from (88.87, 146.28) to (189.62, 206.24) in the icons layer
# (controller; the brush's is the same line within a pixel), which sits at 0.10683167 x + (19.394535, 3.5353702 -
# 4.1973397) in the 67.733 x 39.6875 document
MAGENTA, GREEN = (0xFF, 0x00, 0xCE, 255), (0x00, 0xD0, 0x79, 255)
DOC = FG_W / 67.733341


def layer1(x, y):
    return (0.10683167 * x + 19.394535) * DOC, (0.10683167 * y + 3.5353702 - 4.1973397) * DOC


G1, G2 = layer1(88.871132, 146.27939), layer1(189.62062, 206.23686)

# fill coverage: how far each pixel is from the white outline towards its gradient colour
rgb = fg[..., :3]
yy, xx = np.mgrid[0:FG_H, 0:FG_W]
gx, gy = G2[0] - G1[0], G2[1] - G1[1]
t = np.clip(((xx - G1[0]) * gx + (yy - G1[1]) * gy) / (gx * gx + gy * gy), 0, 1)[..., None]
grad = (np.array(MAGENTA[:3]) * (1 - t) + np.array(GREEN[:3]) * t) / 255
fill_fg = np.clip(((1 - rgb) * (1 - grad)).sum(2) / ((1 - grad) ** 2).sum(2), 0, 1) * alpha_fg
fill_fg[split:] = 0

# logo silhouette and fill, a texel per screen pixel, each in its own texture with an empty border: the fill strips
# reach past the logo, and clamping then reads that border
assert LOGO_T[0] + 2 <= 128 and LOGO_T[1] + 2 <= 128
crop = lambda a: a[LY0:LY1, LX0:LX1]
logo_out, logo_fill = np.zeros((128, 128)), np.zeros((128, 128))
logo_out[1:1 + LOGO_T[1], 1:1 + LOGO_T[0]] = resample(crop(alpha_fg), *LOGO_T)
logo_fill[1:1 + LOGO_T[1], 1:1 + LOGO_T[0]] = resample(crop(fill_fg), *LOGO_T)

word_tex = np.zeros((64, 256))
word_tex[:WORD_T[1], :WORD_T[0]] = resample(alpha_fg[WY0:WY1, WX0:WX1], *WORD_T)

# chevrons: the SVG pattern cell (30 x 9 units, filled #1a1a1a on #292929), one cell per texture; 55.2 x 16.55 icon
# pixels per cell
CELL = [[(8, 9), (15, 5.5), (22, 9)], [(0, 9), (0, 4), (8, 0), (22, 0), (30, 4), (30, 9), (15, 1)]]
CHEV_T = (64, 32)
CHEV_PX = (55.14 * SCALE, 16.54 * SCALE)
SS = 8
img = Image.new('L', (CHEV_T[0] * SS, CHEV_T[1] * SS), 0)
d = ImageDraw.Draw(img)
for poly in CELL:
    for ox in (-30, 0, 30):
        for oy in (-9, 0, 9):
            d.polygon([((x + ox) / 30 * CHEV_T[0] * SS, (y + oy) / 9 * CHEV_T[1] * SS) for x, y in poly], fill=255)
chev_tex = np.asarray(img.resize(CHEV_T, Image.Resampling.BOX)).astype(float) / 255

textures = {}
for name, a in (('tz_white.bclim', np.ones((8, 8))), ('tz_chev.bclim', chev_tex), ('tz_logo.bclim', logo_out), ('tz_fill.bclim', logo_fill), ('tz_word.bclim', word_tex)):
    data, q = bclim_a4(a)
    textures[name] = (data, q)
TEX_NAMES = list(textures)
TI = {n: i for i, n in enumerate(TEX_NAMES)}

# ---------------------------------------------------------------- materials

# TEV of the original logo's single-texture materials: texture, then black/white colour interpolation, then times the
# vertex colour; alpha compare off, alpha blending. With the black colour white (and transparent), an A4 texture
# draws in its vertex colours.
TEV_TAIL = bytes.fromhex('400400004004000011000000740600047406000411000000650600016506000111000000070000000000000001040500')
TEV_FLAGS = 0x6C0
CLAMP, REPEAT = 4, 5                          # linear filtering, clamped or repeated


def material(name, tex, wrap=CLAMP):
    return Material(name, [(255, 255, 255, 0)] + [(255, 255, 255, 255)] * 6, [(TI[tex], wrap, wrap)], [(0.0, 0.0, 0.0, 1.0, 1.0)], [(0, 0, 0, 0)], TEV_TAIL, TEV_FLAGS)


MATS = [material('M_Solid', 'tz_white.bclim'), material('M_Chev', 'tz_chev.bclim', REPEAT), material('M_Logo', 'tz_logo.bclim'), material('M_Fill', 'tz_fill.bclim'), material('M_Word', 'tz_word.bclim')]
MI = {m.name: i for i, m in enumerate(MATS)}

# ---------------------------------------------------------------- panes

BG, CHEV = (0x29, 0x29, 0x29, 255), (0x1A, 0x1A, 0x1A, 255)
WHITE = (255, 255, 255, 255)
UV_FULL = (0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0)
PERIOD = CHEV_PX[1]                           # one cell up per loop


def pic(name, mat, size, t=(0.0, 0.0, 0.0), vcol=None, uv=UV_FULL, r=0.0):
    return Pane(name, 'pic1', t=t, r=(0.0, 0.0, r), size=size, vcol=vcol or [WHITE] * 4, mat=MI[mat], uvs=[uv])


def background(w, h, z):
    # the chevrons pane is a cell and a half higher than needed on both sides, so moving it from -PERIOD/2 to +PERIOD
    # always covers the screen; a chevron's point is at the centre of the screen
    ch = h + 2 * PERIOD + 4
    u = w / CHEV_PX[0] / 2
    v = ch / CHEV_PX[1]
    return [pic('P_Bg', 'M_Solid', (w, h), (0.0, 0.0, z), [BG] * 4),
            pic('P_Chev', 'M_Chev', (w, ch), (0.0, 0.0, z), [CHEV] * 4, (0.5 - u, 0.0, 0.5 + u, 0.0, 0.5 - u, v, 0.5 + u, v))]


def sub_uv(x, y, w, h, tw, th):
    return (x / tw, y / th, (x + w) / tw, y / th, x / tw, (y + h) / th, (x + w) / tw, (y + h) / th)


def logo():
    # white silhouette, then the fill in three strips across the gradient (solid magenta, the blend, solid green):
    # rotated quads whose corners pick their part of the fill mask, coloured by their vertex colours
    W, H = LOGO_SIZE
    n = Pane('N_Logo', 'pan1', flags=3, t=(LOGO_POS[0], LOGO_POS[1], 0.0), size=LOGO_SIZE)
    n.children.append(pic('P_LogoOut', 'M_Logo', LOGO_SIZE, uv=sub_uv(1, 1, LOGO_T[0], LOGO_T[1], 128, 128)))
    sx, sy = W / LOGO_EXACT[0], H / LOGO_EXACT[1]
    local = lambda p: ((p[0] - (LX0 + LX1) / 2) / PX * sx, ((LY0 + LY1) / 2 - p[1]) / PX * sy)   # render px -> logo pane, y up
    p1, p2 = local(G1), local(G2)
    g = (p2[0] - p1[0], p2[1] - p1[1]); gl = math.hypot(*g)
    dvec = (g[0] / gl, g[1] / gl)
    evec = (-dvec[1], dvec[0])
    corners = [(-W / 2, H / 2), (W / 2, H / 2), (-W / 2, -H / 2), (W / 2, -H / 2)]
    along = lambda p: (p[0] - p1[0]) * dvec[0] + (p[1] - p1[1]) * dvec[1]       # distance along the gradient from its start
    lo, hi = min(along(c) for c in corners) - 1, max(along(c) for c in corners) + 1
    centre_along = along((0.0, 0.0))
    half = max(abs(c[0] * evec[0] + c[1] * evec[1]) for c in corners) + 1
    angle = math.degrees(math.atan2(dvec[1], dvec[0]))
    for i, (a, b, ca, cb) in enumerate(((lo, 0.0, MAGENTA, MAGENTA), (0.0, gl, MAGENTA, GREEN), (gl, hi, GREEN, GREEN))):
        mid = (a + b) / 2 - centre_along
        c = (mid * dvec[0], mid * dvec[1])
        width = b - a
        uv = []
        for lx, ly in ((-width / 2, half), (width / 2, half), (-width / 2, -half), (width / 2, -half)):
            px, py = c[0] + lx * dvec[0] + ly * evec[0], c[1] + lx * dvec[1] + ly * evec[1]
            uv += [(1 + (px / W + 0.5) * LOGO_T[0]) / 128, (1 + (0.5 - py / H) * LOGO_T[1]) / 128]
        n.children.append(Pane(f'P_LogoF{i}', 'pic1', t=(c[0], c[1], 0.0), r=(0.0, 0.0, angle), size=(width, 2 * half),
                               vcol=[ca, cb, ca, cb], mat=MI['M_Fill'], uvs=[tuple(uv)]))
    return n


top_root = Pane('RootPane', 'pan1', size=(400.0, 240.0))
nw = Pane('N_W_00', 'pan1', flags=3, size=(32.0, 32.0))
top_root.children.append(nw)
nw.children += background(440.0, 264.0, -8.0)
fgp = Pane('N_Fg', 'pan1', flags=3, size=(32.0, 32.0))
fgp.children += [logo(), pic('P_Word', 'M_Word', WORD_SIZE, (WORD_POS[0], WORD_POS[1], 0.0), uv=sub_uv(0, 0, WORD_T[0], WORD_T[1], 256, 64))]
nw.children.append(fgp)


def fade(w, h, z=0.0):
    # black over everything, for fading in and out: fading the panes themselves would show the logo's white layer
    # through its colours (and fade to whatever the HOME Menu clears to). Starts black, like makerom's Homebrew logo.
    p = pic('P_Fade', 'M_Solid', (w, h), (0.0, 0.0, z), [(0, 0, 0, 255)] * 4)
    p.alpha = 255
    return p


nw.children.append(fade(440.0, 264.0, 1.0))

bot_root = Pane('RootPane', 'pan1', size=(320.0, 240.0))
nr = Pane('N_Root_00', 'pan1', flags=3, size=(40.0, 40.0))
bot_root.children.append(nr)
nr.children += background(320.0, 240.0, 0.0)
nr.children.append(fade(320.0, 240.0))

# ---------------------------------------------------------------- animations
# A: the intro (60 frames), fading in from black over the first 10; B: loops until the app has started (40 frames);
# C: the fade out to black (15 frames). The chevrons move up one cell per 40 frames throughout: A goes from half a cell
# below to one cell up, so B carries on seamlessly.


def key(f, v, s=0.0): return (float(f), float(v), float(s))


def linear(f0, v0, f1, v1):
    s = (v1 - v0) / (f1 - f0)
    return [key(f0, v0, s), key(f1, v1, s)]


def target(name, **c):
    tags = {'ty': (PANE_SRT, TY), 'tz': (PANE_SRT, TZ), 'alpha': (VTX_COLOR, ALPHA)}
    return AnimTarget(name, [Curve(tags[k][0], tags[k][1], v) for k, v in c.items()])


def scene(root, top):
    still = dict(tz=[key(0, 0)], alpha=[key(0, 255)]) if top else dict(alpha=[key(0, 255)])
    a = [target(root, **still), target('P_Chev', ty=linear(0, -PERIOD / 2, 60, PERIOD)), target('P_Fade', alpha=[key(0, 255), key(10, 0)])]
    b = [target(root, **still), target('P_Chev', ty=linear(0, 0, 40, PERIOD)), target('P_Fade', alpha=[key(0, 0)])]
    c = [target('P_Fade', alpha=linear(0, 0, 14, 255))]
    names = lambda ts: [x.name for x in ts]
    groups = Group('RootGroup', [], [Group('G_A_00', names(a)), Group('G_B_00', names(b)), Group('G_C_00', names(c))])
    return (Anim('SceneOutA', 0, ['G_A_00'], -60, -1, 60, False, a), Anim('SceneOutB', 1, ['G_B_00'], 0, 40, 40, True, b),
            Anim('SceneOutC', 2, ['G_C_00'], 101, 115, 15, False, c)), groups


(UA, UB, UC), ugroups = scene('N_W_00', True)
(DA, DB, DC), dgroups = scene('N_Root_00', False)
top = Layout(400.0, 240.0, TEX_NAMES, MATS, top_root, ugroups)
bot = Layout(320.0, 240.0, TEX_NAMES[:2], MATS[:2], bot_root, dgroups)

files = {
    'anim/NintendoLogo_D_00_SceneOutA.bclan': write_clan(DA), 'anim/NintendoLogo_D_00_SceneOutB.bclan': write_clan(DB),
    'anim/NintendoLogo_D_00_SceneOutC.bclan': write_clan(DC), 'anim/NintendoLogo_U_00_SceneOutA.bclan': write_clan(UA),
    'anim/NintendoLogo_U_00_SceneOutB.bclan': write_clan(UB), 'anim/NintendoLogo_U_00_SceneOutC.bclan': write_clan(UC),
    'blyt/NintendoLogo_D_00.bclyt': write_clyt(bot), 'blyt/NintendoLogo_U_00.bclyt': write_clyt(top),
}
for n, (data, _) in textures.items():
    files['timg/' + n] = data

# ---------------------------------------------------------------- DARC, HMAC, LZ11


def darc(files):
    # entries: root, '.', then each directory followed by its files; names UTF-16; every file on a 4-byte boundary (the
    # HOME Menu reads the layouts' words and floats in place), textures on 128-byte ones
    dirs = {}
    for path in files:
        dd, f = path.split('/')
        dirs.setdefault(dd, []).append(f)
    entries, names = [], b''

    def name(s):
        nonlocal names
        o = len(names); names += s.encode('utf-16-le') + b'\0\0'; return o
    total = 2 + sum(1 + len(v) for v in dirs.values())
    entries.append([name('') | 0x01000000, 0, total])
    entries.append([name('.') | 0x01000000, 0, total])
    order = []
    for dd in sorted(dirs):
        idx = len(entries)
        entries.append([name(dd) | 0x01000000, 1, idx + 1 + len(dirs[dd])])
        for f in sorted(dirs[dd]):
            order.append((len(entries), dd + '/' + f))
            entries.append([name(f), 0, len(files[dd + '/' + f])])
    table_len = 12 * len(entries) + len(names)
    data_off = 0x1C + table_len
    data_off += -data_off % 4
    blob = bytearray()
    for idx, path in order:
        pos = data_off + len(blob)
        pad = -pos % (128 if path.endswith('.bclim') else 4)
        blob += b'\0' * pad; pos += pad
        entries[idx][1] = pos
        blob += files[path]
    hdr = b'darc' + struct.pack('<HHIIIII', 0xFEFF, 0x1C, 0x01000000, data_off + len(blob), 0x1C, table_len, data_off)
    table = b''.join(struct.pack('<III', *e) for e in entries) + names
    return hdr + table + b'\0' * (data_off - 0x1C - len(table)) + bytes(blob)


archive = darc(files)
key_bytes = bytes.fromhex(open(KEY).read().strip()) if KEY else b'\0' * 32
full = archive + hmac.new(key_bytes, archive, hashlib.sha256).digest()
packed = lz11_compress(full)
assert lz11_decompress(packed) == full
open(os.path.join(OUT, 'logo.darc'), 'wb').write(full)
for path in sorted(files):
    print(f'{path:45} {len(files[path]):6} bytes')
print(f'logo: {len(full)} bytes, {len(packed)} compressed (limit 8192){"" if KEY else " -- no key given, HMAC is a placeholder"}')
print(f'logo {LOGO_SIZE[0]:.1f}x{LOGO_SIZE[1]:.1f} at {LOGO_POS[0]:.1f},{LOGO_POS[1]:.1f}; wordmark {WORD_SIZE[0]:.1f}x{WORD_SIZE[1]:.1f} at {WORD_POS[0]:.1f},{WORD_POS[1]:.1f}')
if len(packed) <= 0x2000:
    open(os.path.join(OUT, 'logo.bin'), 'wb').write(packed + b'\0' * (0x2000 - len(packed)))
else:
    print('TOO BIG by', len(packed) - 0x2000)
pickle.dump({'U': top, 'D': bot, 'anims': {'U': (UA, UB, UC), 'D': (DA, DB, DC)}, 'textures': {n: q for n, (_, q) in textures.items()}},
            open(os.path.join(OUT, 'scene.pkl'), 'wb'))
