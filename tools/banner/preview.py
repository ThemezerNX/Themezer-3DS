# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# Flat-shaded software preview of banner.gltf through the HOME Menu camera at a few spin angles (painter's algorithm).
# usage: preview.py <out dir>  -> preview.png (reads banner.gltf from the same dir)
import json, math, sys, os
import numpy as np
from PIL import Image, ImageDraw
OUT = sys.argv[1]
g = json.load(open(os.path.join(OUT, 'banner.gltf'))); blob = open(os.path.join(OUT, 'banner.bin'), 'rb').read()
tex = {i: Image.open(os.path.join(OUT, im['uri'])).convert('RGBA') for i, im in enumerate(g['images'])}
def acc(i):
    a = g['accessors'][i]; v = g['bufferViews'][a['bufferView']]
    dt = {5126: np.float32, 5123: np.uint16, 5125: np.uint32}[a['componentType']]
    n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
    return np.frombuffer(blob, dt, a['count'] * n, v['byteOffset']).reshape(a['count'], n)
def qmat(q):
    x, y, z, w = q
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)], [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
def world(node_i, spin):
    n = g['nodes'][node_i]
    M = np.eye(4)
    if 'translation' in n: M[:3, 3] = n['translation']
    R = qmat(n['rotation']) if 'rotation' in n else np.eye(3)
    if n['name'] == 'world': R = qmat([0, math.sin(spin/2), 0, math.cos(spin/2)])  # as the HOME Menu turns it
    M[:3, :3] = R @ np.diag(n.get('scale', [1, 1, 1]))
    return M
parent = {c: i for i, n in enumerate(g['nodes']) for c in n.get('children', [])}
def full(i, spin):
    M = world(i, spin)
    while i in parent:
        i = parent[i]; M = world(i, spin) @ M
    return M
cam = np.array([0, 1, 44.786]); f = 1 / math.tan(0.523599 / 2)
SS = 3  # supersampling, like the smoothed output on the console
W, H = 400 * SS, 240 * SS
light = np.array([0.3, 0.5, 1.0]); light /= np.linalg.norm(light)
frames = []
texarr = {k: np.asarray(v, np.float32) for k, v in tex.items()}
# a dark and the default light theme
for deg, bg in ((0, (70, 74, 82)), (-35, (70, 74, 82)), (-70, (236, 236, 236)), (-160, (236, 236, 236))):
    color = np.zeros((H, W, 3), np.float32); color[:] = bg
    zbuf = np.full((H, W), np.inf, np.float32)
    for ni, n in enumerate(g['nodes']):
        if 'mesh' not in n: continue
        M = full(ni, math.radians(deg))
        for p in g['meshes'][n['mesh']]['primitives']:
            P = acc(p['attributes']['POSITION']); N = acc(p['attributes']['NORMAL']); T = acc(p['attributes']['TEXCOORD_0']); I = acc(p['indices']).reshape(-1, 3)
            VC = acc(p['attributes']['COLOR_0']) if 'COLOR_0' in p['attributes'] else None
            material = g['materials'][p['material']]; mat = material['pbrMetallicRoughness']
            blend = material.get('alphaMode') == 'BLEND'; unlit = material['name'].startswith('name')
            Pw = (M[:3, :3] @ P.T).T + M[:3, 3]; Nw = (M[:3, :3] @ N.T).T
            rel = Pw - cam
            sx = W / 2 + f * rel[:, 0] / -rel[:, 2] * H / 2
            sy = H / 2 - f * rel[:, 1] / -rel[:, 2] * H / 2
            dz = -rel[:, 2]
            for t in I:
                nrm = Nw[t[0]]
                if np.dot(nrm, cam - Pw[t[0]]) <= 0: continue
                shade = 1.0 if unlit else 0.45 + 0.55 * max(0.0, float(np.dot(nrm, light)))
                x0, x1 = int(max(0, np.floor(sx[t].min()))), int(min(W - 1, np.ceil(sx[t].max())))
                y0, y1 = int(max(0, np.floor(sy[t].min()))), int(min(H - 1, np.ceil(sy[t].max())))
                if x1 < x0 or y1 < y0: continue
                xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
                (ax, bx, cx), (ay, by, cy) = sx[t], sy[t]
                den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
                if abs(den) < 1e-9: continue
                w0 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / den
                w1 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / den
                w2 = 1 - w0 - w1
                inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
                if not inside.any(): continue
                z = w0 * dz[t[0]] + w1 * dz[t[1]] + w2 * dz[t[2]]
                zb = zbuf[y0:y1 + 1, x0:x1 + 1]
                draw = inside & (z < zb)
                if not draw.any(): continue
                if 'baseColorTexture' in mat:
                    tx = texarr[mat['baseColorTexture']['index']]
                    u = w0 * T[t[0], 0] + w1 * T[t[1], 0] + w2 * T[t[2], 0]
                    v = w0 * T[t[0], 1] + w1 * T[t[1], 1] + w2 * T[t[2], 1]
                    # bilinear, as the samplers are set to linear
                    fx = np.clip(u * tx.shape[1] - 0.5, 0, tx.shape[1] - 1); fy = np.clip(v * tx.shape[0] - 0.5, 0, tx.shape[0] - 1)
                    ix, iy = np.floor(fx).astype(int), np.floor(fy).astype(int)
                    jx, jy = np.minimum(ix + 1, tx.shape[1] - 1), np.minimum(iy + 1, tx.shape[0] - 1)
                    wx, wy = (fx - ix)[..., None], (fy - iy)[..., None]
                    col = (tx[iy, ix] * (1 - wx) + tx[iy, jx] * wx) * (1 - wy) + (tx[jy, ix] * (1 - wx) + tx[jy, jx] * wx) * wy
                else:
                    col = np.full(xs.shape + (4,), 255, np.float32)
                if VC is not None:
                    vc = (w0[..., None] * VC[t[0]] + w1[..., None] * VC[t[1]] + w2[..., None] * VC[t[2]]) * 255
                    col = col * vc / 255
                region = color[y0:y1 + 1, x0:x1 + 1]
                if blend:
                    a = col[..., 3:4] / 255
                    region[draw] = (col[..., :3] * shade * a + region * (1 - a))[draw]
                else:
                    region[draw] = col[draw][:, :3] * shade
                    zb[draw] = z[draw]
    # the HOME Menu's status bar and camera / L+R hint bar
    color[:20 * SS] = color[:20 * SS] * 0.4 + 160 * 0.6; color[-20 * SS:] = color[-20 * SS:] * 0.4 + 160 * 0.6
    frames.append(Image.fromarray(np.clip(color, 0, 255).astype(np.uint8)).resize((W // SS, H // SS), Image.BOX))
fw, fh = W // SS, H // SS
sheet = Image.new('RGB', (fw * 2 + 4, fh * 2 + 4), (0, 0, 0))
for i, fr in enumerate(frames): sheet.paste(fr, ((i % 2) * (fw + 4), (i // 2) * (fh + 4)))
sheet.save(os.path.join(OUT, 'preview.png'))
print('preview written')
