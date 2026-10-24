#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
"""Makes romfs/backdrop.png, the pattern under the pages' ThumbHash backdrop: the boot animation's chevrons (see
tools/bootlogo/build_logo.py), still, about the size it shows them, over the app's background at the opacity the
website gives its tile (bg-tile.png, 30%). The top screen's size; the bottom screen shows its middle, and as nine rows
of chevrons fill a screen's height exactly, the pattern carries on from the top screen into the bottom one.

    python3 tools/backdrop/make_backdrop.py <out.png>
"""

import sys

from PIL import Image, ImageDraw

if len(sys.argv) != 2:
    sys.exit(__doc__.strip().splitlines()[-1].strip())

W, H, SS = 400, 240, 8
CELL = [[(8, 9), (15, 5.5), (22, 9)], [(0, 9), (0, 4), (8, 0), (22, 0), (30, 4), (30, 9), (15, 1)]]  # 30 x 9 units
CELL_PX = (240 / 9 * 55.14 / 16.54, 240 / 9)  # the boot animation's 88.2 x 26.5 cell, a little larger: nine to a screen
BACKGROUND = (12, 10, 9)              # gfx::color::kBackground
TILE, CHEVRON, OPACITY = (41, 41, 41), (26, 26, 26), 0.3

mask = Image.new('L', (W * SS, H * SS), 0)
draw = ImageDraw.Draw(mask)
# a cell centred on the screen, as the website's tile is ("repeat center")
ox0, oy0 = W / 2 - CELL_PX[0] / 2, H / 2 - CELL_PX[1] / 2
for cy in range(-6, 7):
    for cx in range(-4, 5):
        for poly in CELL:
            draw.polygon([((ox0 + (cx + x / 30) * CELL_PX[0]) * SS, (oy0 + (cy + y / 9) * CELL_PX[1]) * SS)
                          for x, y in poly], fill=255)
mask = mask.resize((W, H), Image.Resampling.BOX)

mix = lambda over: tuple(round(o * OPACITY + b * (1 - OPACITY)) for o, b in zip(over, BACKGROUND))
Image.composite(Image.new('RGB', (W, H), mix(CHEVRON)), Image.new('RGB', (W, H), mix(TILE)), mask) \
    .save(sys.argv[1], optimize=True)
