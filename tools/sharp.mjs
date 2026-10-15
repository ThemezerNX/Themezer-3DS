// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors
//
// sharp, taken from the Themezer monorepo so the tools need no node_modules of their own.

import path from 'node:path';
import os from 'node:os';
import fs from 'node:fs';
import { createRequire } from 'node:module';

export const defaultMonorepo = process.env.THEMEZER_MONOREPO ?? path.join(os.homedir(), 'dev/themezer/themezer-monorepo');

// sharp from the monorepo: a normal install, or bun's package store
export function loadSharp(monorepo = defaultMonorepo) {
    const require = createRequire(path.join(monorepo, 'package.json'));
    try {
        return require('sharp');
    } catch {
        const store = path.join(monorepo, 'node_modules', '.bun');
        const found = fs.readdirSync(store).filter((name) => name.startsWith('sharp@')).sort().pop();
        if (!found)
            throw new Error(`sharp isn't installed in ${monorepo}`);
        return require(path.join(store, found, 'node_modules', 'sharp'));
    }
}
