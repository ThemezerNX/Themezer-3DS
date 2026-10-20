#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
"""Makes icon.png from a render of the Themezer design file (see build.sh): the HOME Menu icon, 48x48, the controller
on Themezer dark; also the icon of the badge set the app installs.

    python3 tools/icon/make_icon.py <icon.svg render> <out dir>
"""

import os
import sys

from PIL import Image

if len(sys.argv) != 3:
    sys.exit(__doc__.strip().splitlines()[-1].strip())
icon_render, out = sys.argv[1:]
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
