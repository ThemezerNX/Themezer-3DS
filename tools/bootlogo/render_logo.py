# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# Renders the boot logo from build_logo.py's scene.pkl the way the HOME Menu's layout code draws it: pane transforms,
# Hermite curves, A4 textures with linear filtering, vertex colours, the black/white TEV, alpha blending.
# usage: render_logo.py <out dir> [frame step]  -> preview.gif (both screens, the intro, two loops, the fade out),
#        still.png (the end of the intro)
import math, os, pickle, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lyt  # noqa: F401  (for unpickling)

OUT = sys.argv[1]
STEP = int(sys.argv[2]) if len(sys.argv) > 2 else 2
scene = pickle.load(open(os.path.join(OUT, 'scene.pkl'), 'rb'))
TEX = scene['textures']


def hermite(keys, f):
    if f <= keys[0][0]: return keys[0][1]
    if f >= keys[-1][0]: return keys[-1][1]
    for (f0, v0, s0), (f1, v1, s1) in zip(keys, keys[1:]):
        if f0 <= f <= f1 and f1 > f0:
            t = (f - f0) / (f1 - f0); d = f1 - f0
            t2, t3 = t * t, t * t * t
            return (2 * t3 - 3 * t2 + 1) * v0 + (t3 - 2 * t2 + t) * s0 * d + (-2 * t3 + 3 * t2) * v1 + (t3 - t2) * s1 * d
    return keys[-1][1]


def sample(tex, u, v, wrap):
    h, w = tex.shape
    x, y = u * w - 0.5, v * h - 0.5
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    def at(xi, yi):
        if wrap == 5:
            xi, yi = xi % w, yi % h
        else:
            xi, yi = np.clip(xi, 0, w - 1), np.clip(yi, 0, h - 1)
        return tex[yi, xi]
    return (at(x0, y0) * (1 - fx) * (1 - fy) + at(x0 + 1, y0) * fx * (1 - fy) + at(x0, y0 + 1) * (1 - fx) * fy + at(x0 + 1, y0 + 1) * fx * fy)


def draw(layout, overrides, W, H):
    img = np.zeros((H, W, 3))
    def walk(p, mtx, parent_alpha):
        o = overrides.get(p.name, {})
        t = (p.t[0], o.get('ty', p.t[1]))
        a = o.get('alpha', p.alpha) / 255 * parent_alpha
        r = math.radians(p.r[2])
        c, s = math.cos(r), math.sin(r)
        local = np.array([[c * p.s[0], -s * p.s[1], t[0]], [s * p.s[0], c * p.s[1], t[1]], [0, 0, 1]])
        m = mtx @ local
        if p.kind == 'pic1':
            mat = layout.materials[p.mat]
            tex = TEX[layout.textures[mat.texmaps[0][0]]]
            wrap = mat.texmaps[0][1]
            w, h = p.size
            # pixels covered: invert the pane's affine map from (fx, fy) in 0..1 (fy down) to screen pixels (y down)
            def to_screen(fx, fy):
                x, y, _ = m @ np.array([(fx - 0.5) * w, (0.5 - fy) * h, 1.0])
                return W / 2 + x, H / 2 - y
            o_, ax, ay = np.array(to_screen(0, 0)), np.array(to_screen(1, 0)), np.array(to_screen(0, 1))
            A = np.stack([ax - o_, ay - o_], 1)
            Ai = np.linalg.inv(A)
            pts = [o_, ax, ay, ax + ay - o_]
            x0, x1 = max(0, int(min(q[0] for q in pts))), min(W, int(math.ceil(max(q[0] for q in pts))))
            y0, y1 = max(0, int(min(q[1] for q in pts))), min(H, int(math.ceil(max(q[1] for q in pts))))
            if x1 <= x0 or y1 <= y0: return
            ys, xs = np.mgrid[y0:y1, x0:x1] + 0.5
            rel = np.stack([xs - o_[0], ys - o_[1]], -1) @ Ai.T
            fx, fy = rel[..., 0], rel[..., 1]
            inside = (fx >= 0) & (fx < 1) & (fy >= 0) & (fy < 1)
            uv = p.uvs[0]
            bil = lambda tl, tr, bl, br: tl * (1 - fx) * (1 - fy) + tr * fx * (1 - fy) + bl * (1 - fx) * fy + br * fx * fy
            u, v = bil(uv[0], uv[2], uv[4], uv[6]), bil(uv[1], uv[3], uv[5], uv[7])
            ta = sample(tex, u, v, wrap)
            col = np.stack([bil(*[p.vcol[k][ch] for k in range(4)]) for ch in range(4)], -1) / 255
            black, white = np.array(mat.colors[0]) / 255, np.array(mat.colors[1]) / 255
            rgb = black[:3] * col[..., :3]          # an A4 texture's colour is black: the black colour
            alpha = (black[3] + (white[3] - black[3]) * ta) * col[..., 3] * a * inside
            region = img[y0:y1, x0:x1]
            region[:] = rgb * alpha[..., None] + region * (1 - alpha[..., None])
        for ch in p.children:
            walk(ch, m, a if p.flags & 2 else 1.0)
    walk(layout.root, np.eye(3), 1.0)
    return img


def frame(which, anim, f):
    ov = {}
    for tgt in anim.targets:
        for c in tgt.curves:
            k = {(lyt.PANE_SRT, lyt.TY): 'ty', (lyt.PANE_SRT, lyt.TZ): 'tz', (lyt.VTX_COLOR, lyt.ALPHA): 'alpha'}[(c.tag, c.target)]
            ov.setdefault(tgt.name, {})[k] = hermite(c.keys, f)
    return ov


def both(state):
    top = draw(scene['U'], state['U'], 400, 240)
    bot = draw(scene['D'], state['D'], 320, 240)
    out = np.zeros((480, 400, 3)); out[:240] = top; out[240:, 40:360] = bot
    return Image.fromarray(np.round(np.clip(out, 0, 1) * 255).astype(np.uint8))


# timeline: A, two loops of B, C (which leaves everything else as B left it)
seq = [(0, f) for f in range(60)] + [(1, f) for f in range(40)] * 2 + [(2, f) for f in range(15)]
frames, last = [], {'U': {}, 'D': {}}
for i, (n, f) in enumerate(seq):
    state = {}
    for which in ('U', 'D'):
        ov = {k: dict(v) for k, v in last[which].items()}
        for name, vals in frame(which, scene['anims'][which][n], f).items():
            ov.setdefault(name, {}).update(vals)
        state[which] = ov
    last = state
    if n == 0 and f == 59:
        both(state).save(os.path.join(OUT, 'still.png'))
    if i % STEP == 0:
        frames.append(both(state))
frames[0].save(os.path.join(OUT, 'preview.gif'), save_all=True, append_images=frames[1:], duration=int(1000 * STEP / 60), loop=0, optimize=False)
print('preview.gif', len(frames), 'frames; still.png')
