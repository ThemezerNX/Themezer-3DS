// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors
//
// Renders Themezer's logo from its design files into gfx/brand/ at the sizes the app draws it, so the GPU never scales
// it. Needs sharp, which the Themezer monorepo has:
//
//     node tools/make_brand.mjs [path/to/themezer-monorepo]

import path from 'node:path';
import fs from 'node:fs';
import { defaultMonorepo, loadSharp } from './sharp.mjs';

const monorepo = process.argv[2] ?? defaultMonorepo;
const sharp = loadSharp(monorepo);
const out = path.join(path.dirname(new URL(import.meta.url).pathname), '..', 'gfx', 'brand');
fs.mkdirSync(out, { recursive: true });

// [design file, output name, height in pixels]
const renders = [
    ['logo.svg', 'logo', 22],     // the top screen's bar
    ['icon.svg', 'icon_64', 64],  // the About screen and empty pages
];

for (const [file, name, height] of renders) {
    const source = path.join(monorepo, 'design', file);
    await sharp(source, { density: 1200, limitInputPixels: false })
        .resize({ height })
        .png()
        .toFile(path.join(out, `${name}.png`));
    console.log(name, height);
}

fs.writeFileSync(path.join(out, '..', 'brand.t3s'),
    ['--atlas -f rgba8 -z auto', ...renders.map(([, name]) => `brand/${name}.png`)].join('\n') + '\n');
