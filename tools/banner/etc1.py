# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# ETC1 / ETC1A4 for 3DS textures: encoding through devkitPro's tex3ds, and an independent decoder to check the result.
# GPU layout: 8x8 tiles row by row, four 4x4 blocks per tile (top left, top right, bottom left, bottom right), each
# block a little-endian 64-bit ETC1 word, preceded in ETC1A4 by a little-endian 64-bit word of 4-bit alphas.
# Rows run bottom to top, so the image is flipped first, as pycgfx does for its own formats.
import os, struct, subprocess, tempfile
import numpy as np
from PIL import Image

TEX3DS = os.path.join(os.environ.get('DEVKITPRO', '/opt/devkitpro'), 'tools', 'bin', 'tex3ds')
MODIFIERS = [(2, 8), (5, 17), (9, 29), (13, 42), (18, 60), (24, 80), (33, 106), (47, 183)]

def encode(img, alpha):
    with tempfile.TemporaryDirectory() as d:
        src, dst = os.path.join(d, 'in.png'), os.path.join(d, 'out.raw')
        img.convert('RGBA').transpose(Image.Transpose.FLIP_TOP_BOTTOM).save(src)
        subprocess.run([TEX3DS, '-f', 'etc1a4' if alpha else 'etc1', '-q', 'high', '-z', 'none', '-r', '-o', dst, src], check=True)
        data = open(dst, 'rb').read()[4:]  # tex3ds puts a 4-byte "uncompressed" header in front
    assert len(data) == img.width * img.height * (1 if alpha else 0.5), len(data)
    return data

def encode_la8(img):
    # white-on-alpha textures: luminance and alpha, a byte each, alpha first in memory
    with tempfile.TemporaryDirectory() as d:
        src, dst = os.path.join(d, 'in.png'), os.path.join(d, 'out.raw')
        img.convert('RGBA').transpose(Image.Transpose.FLIP_TOP_BOTTOM).save(src)
        subprocess.run([TEX3DS, '-f', 'la8', '-z', 'none', '-r', '-o', dst, src], check=True)
        data = open(dst, 'rb').read()[4:]
    assert len(data) == img.width * img.height * 2, len(data)
    return data

def _block(word):
    out = np.zeros((4, 4, 3), int)  # [y][x]
    flip, diff = (word >> 32) & 1, (word >> 33) & 1
    if diff:
        base = [(word >> s) & 31 for s in (59, 51, 43)]
        delta = [((word >> s) & 7) - (8 if (word >> s) & 4 else 0) for s in (56, 48, 40)]
        c1 = [(b << 3) | (b >> 2) for b in base]
        c2 = [((b + d) << 3) | ((b + d) >> 2) for b, d in zip(base, delta)]
    else:
        c1 = [((word >> s) & 15) * 17 for s in (60, 52, 44)]
        c2 = [((word >> s) & 15) * 17 for s in (56, 48, 40)]
    tables = (MODIFIERS[(word >> 37) & 7], MODIFIERS[(word >> 34) & 7])
    for x in range(4):
        for y in range(4):
            i = x * 4 + y
            second = (y >= 2) if flip else (x >= 2)
            msb, lsb = (word >> (16 + i)) & 1, (word >> i) & 1
            small, large = tables[second]
            mod = (small, large, -small, -large)[msb * 2 + lsb]
            out[y][x] = [min(255, max(0, c + mod)) for c in (c2 if second else c1)]
    return out

def decode(data, w, h, alpha):
    rgb = np.zeros((h, w, 3), np.uint8); a = np.full((h, w), 255, np.uint8)
    pos = 0
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for bx, by in ((0, 0), (4, 0), (0, 4), (4, 4)):
                if alpha:
                    aw = struct.unpack_from('<Q', data, pos)[0]; pos += 8
                    for x in range(4):
                        for y in range(4):
                            a[ty + by + y, tx + bx + x] = ((aw >> (4 * (x * 4 + y))) & 15) * 17
                word = struct.unpack_from('<Q', data, pos)[0]; pos += 8
                rgb[ty + by:ty + by + 4, tx + bx:tx + bx + 4] = _block(word)
    img = Image.fromarray(np.dstack([rgb, a]), 'RGBA')
    return img.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
