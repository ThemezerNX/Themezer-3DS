// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors
//
// Renders an SVG to a PNG of the given width with sharp, for the asset build scripts. The density is the SVG's
// rasterising resolution in dpi before the resize; the scripts keep the one each asset was made with, as it changes the
// edges slightly.
//
//     node tools/render_svg.mjs <in.svg> <out.png> <width> <density> [path/to/themezer-monorepo]

import { defaultMonorepo, loadSharp } from './sharp.mjs';

const [, , src, out, width, density, monorepo = defaultMonorepo] = process.argv;
if (!density) {
    console.error('usage: render_svg.mjs <in.svg> <out.png> <width> <density> [path/to/themezer-monorepo]');
    process.exit(2);
}
const sharp = loadSharp(monorepo);
await sharp(src, { density: Number(density) }).resize({ width: Number(width) }).png().toFile(out);
