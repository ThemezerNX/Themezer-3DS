# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# Builds Themezer's 3D HOME Menu banner as glTF, no background: the controller and brush as a slab with rounded edges
# above the "Themezer" wordmark on a flat screen-facing plane. convert_banner.py turns it into the banner CGFX.
# usage: make_banner.py <design dir> <out dir> <icon.svg rendered as png> [--spin] [--name-scale S] [--logo-turn D]
#        [--button-depth D] [--back-art PNG]
# The logo hangs in the "world" node, which the HOME Menu turns itself (microphone, motion). --spin adds an animation
# that also spins the logo on its own.
import math, sys, os, json
import numpy as np
from PIL import Image, ImageDraw
from svgelements import SVG, Path, Shape, Move, Close, Line
from shapely.geometry import Polygon, MultiPolygon, LineString, LinearRing, Point
from shapely.geometry.polygon import orient
from shapely.ops import unary_union, polygonize
import mapbox_earcut as earcut

D, OUT, ICON_PNG = sys.argv[1:4]
SPIN = '--spin' in sys.argv[4:]
# --logo-turn D: the logo starts turned D degrees anticlockwise seen from above (default 45). The HOME Menu turns the
# "world" node clockwise, so the logo comes round to face the viewer after D degrees instead of turning away at once.
LOGO_TURN = float(sys.argv[sys.argv.index('--logo-turn') + 1]) if '--logo-turn' in sys.argv else 45.0
# --button-depth D: how far the d-pad and buttons stand out of the logo's front face, in banner units (default 0.35; 0
# leaves them flat in the art). BUTTON_ROUND is the rounding of their edges.
BUTTON_DEPTH = float(sys.argv[sys.argv.index('--button-depth') + 1]) if '--button-depth' in sys.argv else 0.35
BUTTON_ROUND = 0.1
# --back-art PNG: the icon rendered without its d-pad and buttons, for the logo's back face (a controller's back has no
# buttons, and the front art seen from behind is mirrored); without it the back shows the front art
BACK_ART = sys.argv[sys.argv.index('--back-art') + 1] if '--back-art' in sys.argv else None
# --name-scale S: scale the wordmark node instead of using a smaller texture. The plane's vertices stay as they are; at
# S >= ~1.25 the 256-texel texture is drawn larger than it is and so filtered, the letters shrunk in the texture to
# keep their size on screen
NAME_SCALE = float(sys.argv[sys.argv.index('--name-scale') + 1]) if '--name-scale' in sys.argv else None
# --uv-zoom Z: keep the plane and its node as they are and have the wordmark's materials show only the middle Z of the
# texture (convert_banner.py --name-uv-zoom Z sets their texture matrix), so a 256-texel texture is drawn larger than it
# is and filtered, the letters drawn inside that window at their usual size
UV_ZOOM = float(sys.argv[sys.argv.index('--uv-zoom') + 1]) if '--uv-zoom' in sys.argv else None

# Layout, in HOME Menu banner units: the camera sits at (0, 1, 44.8) with a 30 degree vertical field of view, so the
# screen's 240 pixels span y = -11 to 13 at z = 0. The status bar covers the top ~20 pixels and the camera and L+R hints
# the bottom ~20, so everything stays between y = -8 and 9.5 (40 to 210 pixels down), leaving room for the logo to turn.
LOGO_HEIGHT, LOGO_Y = 10.0, 3.4
LOGO_BAND = 0.55  # the straight part of the edge between its two rounded halves; the rounding is the icon's outline width
# The wordmark plane, exactly as in the first build that showed it without freezing (float32 values from that file).
# Moving its corners froze the HOME Menu as the banner appeared (a larger plane, a smaller one, other texture
# coordinates: tested on hardware), for reasons not understood, so the plane stays put and everything about the
# wordmark is done in its texture, which can change freely.
NAME_X, NAME_Y0, NAME_Y1 = 10.825911521911621, -7.906477928161621, -2.4935219287872314
# The plane is ~205 pixels wide on screen. pycgfx's sampler settings point sample textures drawn smaller than they are
# (correcting them froze the HOME Menu too), which jags the letters of a 256-texel texture; a 128-texel one is drawn
# larger than it is, so the GPU filters it smoothly, like the standard 2D banner's 256x128 texture.
TEXT_TEX = (256, 64) if NAME_SCALE or UV_ZOOM else (128, 32)
# texels of white around the letters, ~2.4 pixels on screen
TEXT_OUTLINE = 3.0 / NAME_SCALE if NAME_SCALE else 2.3 if UV_ZOOM else 1.5
TEXT_WIDTH, TEXT_Y = 18.0, -6.0  # with --name-scale: the outlined text's width and centre on screen, in units
GRADIENT = ((0xff, 0x00, 0xce), (0x00, 0xd0, 0x79))  # the icon's gradient stops
# colour boost for the icon art and the wordmark: blended in RGB the gradient goes greyish in the middle
SATURATION = 1.25
SPIN_SECONDS, SPIN_KEYS = 6.0, 24  # with --spin: one turn, clockwise seen from above

def subpath_points(path, steps):
    subs = []
    for sub in path.as_subpaths():
        pts = []
        for seg in sub:
            if isinstance(seg, Move):
                pts.append((seg.end.x, seg.end.y)); continue
            if isinstance(seg, (Line, Close)):
                if seg.end is not None: pts.append((seg.end.x, seg.end.y))
                continue
            for t in np.linspace(0, 1, steps + 1)[1:]:
                p = seg.point(t); pts.append((p.x, p.y))
        subs.append(pts)
    return subs

def winding(rings, x, y):
    w = 0
    for r in rings:
        a, b = r, np.roll(r, -1, axis=0)
        cross = (b[:, 0] - a[:, 0]) * (y - a[:, 1]) - (x - a[:, 0]) * (b[:, 1] - a[:, 1])
        w += int(((a[:, 1] <= y) & (b[:, 1] > y) & (cross > 0)).sum()) - int(((a[:, 1] > y) & (b[:, 1] <= y) & (cross < 0)).sum())
    return w

def filled(path, steps=10):
    # SVG's default (nonzero) fill rule: split the plane along every contour and keep the pieces with a nonzero winding number
    rings = [np.array(p) for p in subpath_points(path, steps) if len(set(p)) >= 3]
    pieces = [q for q in polygonize(unary_union([LinearRing(r) for r in rings]))
              if winding(rings, q.representative_point().x, q.representative_point().y) != 0]
    return unary_union(pieces).buffer(0)

def stroked(path, steps=10):
    # round caps and joins, as in the icon
    R = path.stroke_width / 2
    shapes = []
    for pts in subpath_points(path, steps):
        if len(set(pts)) == 1: shapes.append(Point(pts[0]).buffer(R, 8))
        elif len(pts) >= 2: shapes.append(LineString(pts).buffer(R, 8))
    return unary_union(shapes)

def saturate(rgb, factor=SATURATION):
    # scale chroma in OKLab, keeping lightness and hue; rgb is 0..255 sRGB, shape (..., 3)
    c = np.asarray(rgb, np.float64) / 255
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    M1 = np.array([[0.4122214708, 0.5363325363, 0.0514459929], [0.2119034982, 0.6806995451, 0.1073969566], [0.0883024619, 0.2817188376, 0.6299787005]])
    M2 = np.array([[0.2104542553, 0.7936177850, -0.0040720468], [1.9779984951, -2.4285922050, 0.4505937099], [0.0259040371, 0.7827717662, -0.8086757660]])
    lab = np.cbrt(lin @ M1.T) @ M2.T
    lab[..., 1:] *= factor
    lin = (lab @ np.linalg.inv(M2).T) ** 3 @ np.linalg.inv(M1).T
    lin = np.clip(lin, 0, 1)
    return np.where(lin <= 0.0031308, lin * 12.92, 1.055 * lin ** (1 / 2.4) - 0.055) * 255

def polygons_of(geom):
    if geom.is_empty: return []
    return list(geom.geoms) if isinstance(geom, MultiPolygon) else [geom]

# --- geometry
class Mesh:
    def __init__(self):
        self.prims = {}  # material -> (positions, normals, uvs, indices, colours or None)
    def add(self, material, pos, nrm, uv, idx, col=None):
        P = self.prims.setdefault(material, ([], [], [], [], [] if col is not None else None))
        base = len(P[0])
        P[0].extend(pos); P[1].extend(nrm); P[2].extend(uv); P[3].extend(i + base for i in idx)
        if col is not None: P[4].extend(col)

def rounded_slab(mesh, faces, to3d, uvf, r, band, face_mat, side_mat, steps=5, corner_deg=20, z0=0.0, back_mat=None):
    # A slab whose edges are rounded like a pill: flat faces, a quarter round out to a short straight band, and a
    # quarter round back. `faces` is the flat face outline (2D, svg pixels), r the rounding in 3D units; the slab
    # reaches r beyond the faces all around and is band + 2r deep, centred on z0. Front and back get planar UVs from the 2D
    # coordinates.
    zf = band / 2 + r
    # the profile from back to front: (outward reach, z, normal's outward part, normal's z part)
    prof = [(math.cos(p), -band / 2 + r * math.sin(p), math.cos(p), math.sin(p)) for p in np.linspace(-math.pi / 2, 0, steps + 1)]
    prof += [(math.cos(p), band / 2 + r * math.sin(p), math.cos(p), math.sin(p)) for p in np.linspace(0, math.pi / 2, steps + 1)]
    for poly in polygons_of(faces):
        poly = orient(poly, 1.0)
        rings = [list(poly.exterior.coords)[:-1]] + [list(q.coords)[:-1] for q in poly.interiors]
        flat = np.array([p for q in rings for p in q], dtype=np.float64)
        ends = np.cumsum([len(q) for q in rings]).astype(np.uint32)
        tris = earcut.triangulate_float64(flat, ends).reshape(-1, 3)
        # svg y points down, so the 2D winding flips in 3D: (a, c, b) faces the front
        pos = [to3d(x, y, z0 + zf) for x, y in flat]; uv = [uvf(x, y) for x, y in flat]
        mesh.add(face_mat, pos, [(0, 0, 1)] * len(pos), uv, [i for t in tris for i in (t[0], t[2], t[1])])
        pos = [to3d(x, y, z0 - zf) for x, y in flat]
        mesh.add(back_mat or face_mat, pos, [(0, 0, -1)] * len(pos), uv, [i for t in tris for i in (t[0], t[1], t[2])])
        for ring in rings:
            Q = np.array([to3d(x, y, 0)[:2] for x, y in ring])
            n = len(Q)
            # orient() runs the outline counterclockwise and the holes clockwise in svg space; the y flip reverses both
            # in 3D, which puts the solid on the right of every edge, so the outward normal is the edge turned left
            e = np.roll(Q, -1, axis=0) - Q
            en = np.stack([-e[:, 1], e[:, 0]], axis=1) / np.linalg.norm(e, axis=1, keepdims=True)
            # every point of the ring with the directions it reaches out in: a fan of directions rounds off outward
            # corners, inward corners reach out along the mitre
            entries = []  # (point, reach vector, normal)
            for k in range(n):
                a, b = en[k - 1], en[k]
                turn = math.atan2(a[0] * b[1] - a[1] * b[0], float(a @ b))
                if turn < -math.radians(3):  # outward corner: the normals turn clockwise
                    cnt = max(1, math.ceil(-turn / math.radians(corner_deg)))
                    for t in np.linspace(0, 1, cnt + 1):
                        ang = turn * t
                        d = np.array([a[0] * math.cos(ang) - a[1] * math.sin(ang), a[0] * math.sin(ang) + a[1] * math.cos(ang)])
                        entries.append((Q[k], d, d))
                else:
                    m = (a + b) / (1 + float(a @ b))
                    if np.linalg.norm(m) > 3: m *= 3 / np.linalg.norm(m)
                    entries.append((Q[k], m, (a + b) / np.linalg.norm(a + b)))
            pos = np.array([(*(p + reach * r * c), z0 + z) for p, reach, nrm in entries for c, z, nc, nz in prof])
            nrms = np.array([(*(nrm * nc), nz) for p, reach, nrm in entries for c, z, nc, nz in prof])
            L, cnt = len(prof), len(entries)
            idx = []
            for s in range(cnt):
                s1 = (s + 1) % cnt
                for j in range(L - 1):
                    A, B, C, Dv = s * L + j + 1, s1 * L + j + 1, s1 * L + j, s * L + j
                    for tri in ((A, B, C), (A, C, Dv)):
                        g = np.cross(pos[tri[1]] - pos[tri[0]], pos[tri[2]] - pos[tri[0]])
                        # skip slivers: degenerate ones where the rounding meets itself at a point, and the few that
                        # fold over there (inward corners pull the edge together); their neighbours cover the spot
                        if np.linalg.norm(g) < 1e-9 or g @ nrms[list(tri)].sum(axis=0) <= 0: continue
                        idx.extend(tri)
            mesh.add(side_mat, [tuple(v) for v in pos], [tuple(v) for v in nrms], [(0.5, 0.5)] * len(pos), idx)

# logo: the icon's outer outline (controller, brush and their white outlines). The white outline's width becomes the
# rounded edge, so the flat faces show the coloured icon art and the rounded edge is the white outline.
icon_svg = SVG.parse(os.path.join(D, 'icon.svg'), reify=True)
W, H = float(icon_svg.width), float(icon_svg.height)
els = {e.id: Path(e) for e in icon_svg.elements() if isinstance(e, Shape)}
logo_shape = unary_union([filled(els['path1-8']), filled(els['path1-07']), stroked(els['path1-8-9']), stroked(els['path1-07-7'])])
logo_shape = unary_union([Polygon(p.exterior) for p in polygons_of(logo_shape)])  # no gaps inside the outline
S = LOGO_HEIGHT / H
R_PX = els['path1-8-9'].stroke_width
logo_faces = logo_shape.buffer(-R_PX, join_style=1).simplify(0.02 / S)
logo_to3d = lambda x, y, z: ((x - W / 2) * S, (H / 2 - y) * S, z)
logo = Mesh()
rounded_slab(logo, logo_faces, logo_to3d, lambda x, y: (x / W, y / H), R_PX * S, LOGO_BAND, 'logo_face', 'logo_side',
             back_mat='logo_back' if BACK_ART else None)
# the d-pad and the four buttons stand out of the front face by BUTTON_DEPTH: white like the logo's edge, their edges
# rounded the same way, their footprint the art's shapes and their backs inside the slab
if BUTTON_DEPTH > 0:
    zf_logo = LOGO_BAND / 2 + R_PX * S
    band_b = BUTTON_DEPTH + 0.1          # deep enough that the back sits behind the logo's front face
    z_b = zf_logo + BUTTON_DEPTH - (band_b / 2 + BUTTON_ROUND)
    for el in ('path1-8-8', 'path1-8-9-6'):  # d-pad, buttons (white strokes in the icon)
        faces_b = stroked(els[el]).buffer(-BUTTON_ROUND / S, join_style=1).simplify(0.02 / S)
        rounded_slab(logo, faces_b, logo_to3d, lambda x, y: (0.5, 0.5), BUTTON_ROUND, band_b, 'logo_side', 'logo_side', steps=3, z0=z_b)
# for check_banner.py
json.dump({'faces': [[logo_to3d(x, y, 0)[:2] for x, y in p.exterior.coords] for p in polygons_of(logo_faces)],
           'r': R_PX * S, 'band': LOGO_BAND}, open(os.path.join(OUT, 'logo_shape.json'), 'w'))

# the faces are opaque, so filtering must not pull in the black of transparent texels at the shape edges:
# grow the edge colours outward and drop the alpha
def bleed(img):
    a = np.asarray(img, dtype=np.float64)
    rgb, known = a[..., :3].copy(), a[..., 3] >= 250
    while not known.all():
        acc, cnt = np.zeros_like(rgb), np.zeros(known.shape)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                k = np.roll(np.roll(known, dy, 0), dx, 1)
                acc += np.roll(np.roll(rgb, dy, 0), dx, 1) * k[..., None]; cnt += k
        grow = ~known & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]; known |= grow
    return Image.fromarray(np.dstack([rgb, np.full(known.shape, 255)]).round().astype(np.uint8), 'RGBA')
# (full colour here: convert_banner.py stores the icon art and the wordmark as ETC1A4, which keeps the gradient smooth)
art = np.asarray(bleed(Image.open(ICON_PNG).convert('RGBA').resize((128, 128), Image.LANCZOS)), np.float64)
art[..., :3] = saturate(art[..., :3])
Image.fromarray(art.round().clip(0, 255).astype(np.uint8), 'RGBA').save(os.path.join(OUT, 'logo.png'))
if BACK_ART:
    back = np.asarray(bleed(Image.open(BACK_ART).convert('RGBA').resize((128, 128), Image.LANCZOS)), np.float64)
    back[..., :3] = saturate(back[..., :3])
    Image.fromarray(back.round().clip(0, 255).astype(np.uint8), 'RGBA').save(os.path.join(OUT, 'logo_back.png'))
Image.new('RGBA', (8, 8), (255, 255, 255, 255)).save(os.path.join(OUT, 'logo_side.png'))

# wordmark: Themezer in the icon's gradient with a thin white outline, like the icon, on a transparent texture, drawn
# 16x larger and scaled down for smooth edges
text_svg = SVG.parse(os.path.join(D, 'logotext.svg'))
text_shape = unary_union([filled(Path(e), 8) for e in text_svg.elements() if isinstance(e, Shape) and e.fill is not None and e.fill.value is not None])
(TW, TH), PAD, SS = TEXT_TEX, 1, 16
minx, miny, maxx, maxy = text_shape.bounds
if UV_ZOOM:
    upt = 2 * NAME_X / (TW * UV_ZOOM)  # units per texel inside the window
    k = (TEXT_WIDTH / upt - 2 * TEXT_OUTLINE) / (maxx - minx)
    outlined = text_shape.buffer(TEXT_OUTLINE / k, join_style=1)
    minx, miny, maxx, maxy = outlined.bounds
    ox, oy = (TW - (maxx - minx) * k) / 2, (TH - (maxy - miny) * k) / 2  # centred, like the window
    wy0 = TH * (1 - UV_ZOOM) / 2
    assert oy >= wy0 + 1 and ox >= TW * (1 - UV_ZOOM) / 2 + 1, ('the text does not fit the window', ox, oy, wy0)
elif NAME_SCALE is None:
    k = min((TW - 2 * PAD - 2 * TEXT_OUTLINE) / (maxx - minx), (TH - 2 * PAD - 2 * TEXT_OUTLINE) / (maxy - miny))  # texels per svg unit
    outlined = text_shape.buffer(TEXT_OUTLINE / k, join_style=1)
    minx, miny, maxx, maxy = outlined.bounds
    ox, oy = (TW - (maxx - minx) * k) / 2, (TH - (maxy - miny) * k) / 2
else:
    upt = 2 * NAME_X * NAME_SCALE / TW  # units per texel once scaled
    k = (TEXT_WIDTH / upt - 2 * TEXT_OUTLINE) / (maxx - minx)
    outlined = text_shape.buffer(TEXT_OUTLINE / k, join_style=1)
    minx, miny, maxx, maxy = outlined.bounds
    ox = (TW - (maxx - minx) * k) / 2
    oy = (NAME_Y1 * NAME_SCALE - TEXT_Y) / upt - (maxy - miny) * k / 2  # texel row of the text's top
    assert PAD <= oy and oy + (maxy - miny) * k <= TH - PAD, (oy, (maxy - miny) * k)
def coverage(geom):
    mask = Image.new('L', (TW * SS, TH * SS), 0); draw = ImageDraw.Draw(mask)
    px = lambda c: [((x - minx) * k * SS + ox * SS, (y - miny) * k * SS + oy * SS) for x, y in c]
    for poly in polygons_of(geom):
        draw.polygon(px(poly.exterior.coords), fill=255)
        for q in poly.interiors: draw.polygon(px(q.coords), fill=0)
    return np.asarray(mask, np.float64) / 255
fill, edge = coverage(text_shape), coverage(outlined)
# Two layers, ETC1A4 in the banner (LA8 with --uv-zoom, see below). ETC1 keeps colour per 4x4 block, so a single texture mixing the white outline
# with the gradient breaks up where they meet. Here the outline layer is white throughout and the fill layer the bare
# gradient throughout, and only their alpha draws the letters. Alpha is kept at least 1 so the encoder keeps the colour
# of the texels around the letters (it still rounds to transparent).
small = lambda a: np.asarray(Image.fromarray(a.astype(np.float32), 'F').resize((TW, TH), Image.BOX), np.float64)
def layer(rgb, cover, file):
    a = np.maximum(small(cover * 255), 1)
    Image.fromarray(np.dstack([np.broadcast_to(rgb, (TH, TW, 3)), a]).clip(0, 255).round().astype(np.uint8), 'RGBA').save(os.path.join(OUT, file))
tt = np.clip((np.arange(TW) + 0.5 - ox) / ((maxx - minx) * k), 0, 1)
layer(np.full(3, 255.0), edge, 'name_outline.png')
if UV_ZOOM:
    # The HOME Menu hangs when it filters an ETC1A4 texture (every build that drew one larger than it is, or set the
    # filters to linear, froze), so with --uv-zoom both layers are white LA8, filtered fine, and the fill's gradient
    # comes from vertex colours instead
    layer(np.full(3, 255.0), fill, 'name_fill.png')
else:
    layer(saturate(np.array(GRADIENT[0])[None, :] * (1 - tt[:, None]) + np.array(GRADIENT[1])[None, :] * tt[:, None]), fill, 'name_fill.png')
name = Mesh()
x0, x1, y0, y1 = -NAME_X, NAME_X, NAME_Y0, NAME_Y1
# the outline first, the fill drawn over it a hair in front
for mat, z in (('name_outline', 0.0), ('name_fill', 0.02)):
    if UV_ZOOM and mat == 'name_fill':
        # the fill in columns, each edge carrying the gradient's colour where it crosses the text
        COLS = 8
        w0 = (1 - UV_ZOOM) / 2  # the window's left edge in texture coordinates
        text_x = lambda texel: x0 + (texel / TW - w0) / UV_ZOOM * (x1 - x0)  # texel column -> plane x
        tl, tr = text_x(ox), text_x(ox + (maxx - minx) * k)
        pos, uv, col, idx = [], [], [], []
        for c in range(COLS + 1):
            x = x0 + (x1 - x0) * c / COLS
            g = np.clip((x - tl) / (tr - tl), 0, 1)
            rgb = saturate(np.array(GRADIENT[0]) * (1 - g) + np.array(GRADIENT[1]) * g) / 255
            pos += [(x, y0, z), (x, y1, z)]; uv += [(c / COLS, 1), (c / COLS, 0)]; col += [(*rgb, 1.0)] * 2
        for c in range(COLS):
            a, b, cc, d = 2 * c, 2 * c + 2, 2 * c + 3, 2 * c + 1  # bottom left, bottom right, top right, top left
            idx += [a, b, cc, a, cc, d]
        name.add(mat, pos, [(0, 0, 1)] * len(pos), uv, idx, col)
    else:
        name.add(mat, [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)], [(0, 0, 1)] * 4, [(0, 1), (1, 1), (1, 0), (0, 0)], [0, 1, 2, 0, 2, 3])
print(f'wordmark plane {x1 - x0:.2f} x {y1 - y0:.2f}, text {(maxx - minx) * k:.0f} of {TW} texels wide')

# --- glTF
blob = bytearray(); views = []; accessors = []
def add_acc(arr, ctype, typ, target=None, minmax=False):
    while len(blob) % 4: blob.append(0)
    views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': arr.nbytes, **({'target': target} if target else {})})
    blob.extend(arr.tobytes())
    a = {'bufferView': len(views) - 1, 'componentType': ctype, 'count': len(arr), 'type': typ}
    if minmax:
        a['min'] = np.atleast_1d(arr.min(axis=0)).tolist(); a['max'] = np.atleast_1d(arr.max(axis=0)).tolist()
    accessors.append(a); return len(accessors) - 1

# every material is textured, as in hardware-tested banners; convert_banner.py makes them matte
matte = {'metallicFactor': 0, 'roughnessFactor': 1}
materials = {'logo_face': {'name': 'logo_face', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, **matte}},
             'logo_side': {'name': 'logo_side', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 1}, **matte}},
             'name_outline': {'name': 'name_outline', 'alphaMode': 'BLEND', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 2}, **matte}},
             'name_fill': {'name': 'name_fill', 'alphaMode': 'BLEND', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 3}, **matte}}}
if BACK_ART:
    materials['logo_back'] = {'name': 'logo_back', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 4}, **matte}}
mat_index = {k: i for i, k in enumerate(materials)}
def gl_mesh(mesh, name):
    prims = []
    for mat, (pos, nrm, uv, idx, col) in mesh.prims.items():
        P = np.array(pos, np.float32); N = np.array(nrm, np.float32); T = np.array(uv, np.float32); I = np.array(idx, np.uint16)
        assert len(pos) < 65536
        attrs = {'POSITION': add_acc(P, 5126, 'VEC3', 34962, True), 'NORMAL': add_acc(N, 5126, 'VEC3', 34962), 'TEXCOORD_0': add_acc(T, 5126, 'VEC2', 34962)}
        if col is not None: attrs['COLOR_0'] = add_acc(np.array(col, np.float32), 5126, 'VEC4', 34962)
        prims.append({'attributes': attrs, 'indices': add_acc(I, 5123, 'SCALAR', 34963), 'material': mat_index[mat]})
    print(f'{name}: {sum(len(p[3]) // 3 for p in mesh.prims.values())} triangles, {sum(len(p[0]) for p in mesh.prims.values())} vertices')
    return {'name': name, 'primitives': prims}

# Rigid mesh nodes only, as in hardware-tested banners. "world" is the node the HOME Menu turns for the microphone and
# motion, and it places the logo. The wordmark node "name" sits beside "world", keeps an identity transform with its
# placement baked into the vertices, and becomes a Y-axis billboard. With --spin the logo node inside "world" also
# spins about Y, every transform channel keyed (convert_banner.py writes the rotation as a plain Y angle).
nodes = [
    {'name': 'COMMON', 'children': [1, 3]},
    {'name': 'world', 'translation': [0, LOGO_Y, 0], 'children': [2]},
    {'name': 'logo', 'mesh': 0, 'rotation': [0, math.sin(math.radians(LOGO_TURN) / 2), 0, math.cos(math.radians(LOGO_TURN) / 2)]},
    {'name': 'name', 'mesh': 1, **({'scale': [NAME_SCALE] * 3} if NAME_SCALE else {})},
]
times = np.array([SPIN_SECONDS * k / SPIN_KEYS for k in range(SPIN_KEYS + 1)], np.float32)
rots = np.array([[0, -math.sin(math.pi * k / SPIN_KEYS), 0, math.cos(math.pi * k / SPIN_KEYS)] for k in range(SPIN_KEYS + 1)], np.float32)
anim = None
if SPIN:
    t_acc = add_acc(times, 5126, 'SCALAR', minmax=True)
    samplers = [{'input': t_acc, 'output': add_acc(np.zeros((len(times), 3), np.float32), 5126, 'VEC3'), 'interpolation': 'LINEAR'},
                {'input': t_acc, 'output': add_acc(rots, 5126, 'VEC4'), 'interpolation': 'LINEAR'},
                {'input': t_acc, 'output': add_acc(np.ones((len(times), 3), np.float32), 5126, 'VEC3'), 'interpolation': 'LINEAR'}]
    anim = {'name': 'spin', 'samplers': samplers,
            'channels': [{'sampler': i, 'target': {'node': 2, 'path': p}} for i, p in enumerate(('translation', 'rotation', 'scale'))]}
gltf = {'asset': {'version': '2.0', 'generator': 'themezer-3ds banner'}, 'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': nodes,
        'meshes': [gl_mesh(logo, 'logo'), gl_mesh(name, 'name')], 'materials': list(materials.values()),
        'textures': [{'source': i, 'sampler': 0} for i in range(5 if BACK_ART else 4)], 'samplers': [{'magFilter': 9729, 'minFilter': 9729, 'wrapS': 33071, 'wrapT': 33071}],
        'images': [{'uri': 'logo.png'}, {'uri': 'logo_side.png'}, {'uri': 'name_outline.png'}, {'uri': 'name_fill.png'}] + ([{'uri': 'logo_back.png'}] if BACK_ART else []), **({'animations': [anim]} if anim else {}),
        'buffers': [{'uri': 'banner.bin', 'byteLength': len(blob)}], 'bufferViews': views, 'accessors': accessors}
open(os.path.join(OUT, 'banner.bin'), 'wb').write(blob)
json.dump(gltf, open(os.path.join(OUT, 'banner.gltf'), 'w'))
print('written', os.path.join(OUT, 'banner.gltf'))
