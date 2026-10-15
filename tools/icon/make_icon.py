#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
"""Makes the app's small images from renders of the Themezer design files (see build.sh):

- icon.png: the HOME Menu icon, 48x48, the controller on Themezer dark; also the icon of the badge set the app installs;
- banner.png: the README's header, the Homebrew App Store design scaled to 256x150 and cut to 256x128 around its content.

    python3 tools/icon/make_icon.py <icon.svg render> <hbappstore/icon.svg render> <out dir>
"""

import os
import sys

from PIL import Image

if len(sys.argv) != 4:
    sys.exit(__doc__.strip().splitlines()[-1].strip())
icon_render, hbapp_render, out = sys.argv[1:]
DARK = (41, 41, 41, 255)


def fit(img, box):
    w, h = img.size
    s = min(box[0] / w, box[1] / h)
    return img.resize((max(1, round(w * s)), max(1, round(h * s))), Image.LANCZOS)


icon = Image.open(icon_render).convert('RGBA')
icon = icon.crop(icon.getbbox())
tile = Image.new('RGBA', (48, 48), DARK)
small = fit(icon, (40, 40))
tile.alpha_composite(small, ((48 - small.width) // 2, (48 - small.height) // 2))
tile.convert('RGB').save(os.path.join(out, 'icon.png'))

hb = Image.open(hbapp_render).convert('RGBA').resize((256, 150), Image.LANCZOS)
# the content is anything brighter than the chevron background
px = hb.load()
rows = [y for y in range(150) if any(max(px[x, y][:3]) > 120 for x in range(256))]
mid = (rows[0] + rows[-1]) // 2
y0 = max(0, min(150 - 128, mid - 64))
hb.crop((0, y0, 256, y0 + 128)).save(os.path.join(out, 'banner.png'))
