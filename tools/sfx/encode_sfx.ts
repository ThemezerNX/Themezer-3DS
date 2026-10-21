// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors
//
// Encodes raw 16-bit mono clips into mono DSP-ADPCM BCSTMs, a quarter of the size, with the Themezer webapp's encoder
// (as tools/music does).
// usage: bun encode_sfx.ts <dir with .pcm> <their rate> [path/to/themezer-monorepo]  -> <name>.bcstm next to each
import { readdirSync, readFileSync, writeFileSync } from "node:fs"
import { homedir } from "node:os"
import { join } from "node:path"

const dir = process.argv[2]!
const rate = Number(process.argv[3])
const monorepo = process.argv[4] ?? process.env.THEMEZER_MONOREPO ?? join(homedir(), "dev/themezer/themezer-monorepo")
const { encodePcmToBcstm, decodeBcstmToPcm } =
    await import(join(monorepo, "packages/3dstheme-builder/src/theme/bcstm/bcstm"))

for (const file of readdirSync(dir).filter((f) => f.endsWith(".pcm")).sort()) {
    const bytes = readFileSync(join(dir, file))
    const pcm = new Int16Array(bytes.buffer, bytes.byteOffset, bytes.byteLength / 2).slice()
    const bcstm = encodePcmToBcstm({ channels: [pcm], sampleRate: rate, encoding: "dspAdpcm", loopPoint: null })
    const out = join(dir, file.replace(/\.pcm$/, ".bcstm"))
    writeFileSync(out, bcstm)
    const decoded = decodeBcstmToPcm(bcstm).channels[0]!
    let err = 0, sig = 0
    for (let i = 0; i < pcm.length; i++) {
        err += (decoded[i]! - pcm[i]!) ** 2
        sig += pcm[i]! ** 2
    }
    console.log(`${file}: ${bytes.length} -> ${bcstm.length} bytes, SNR ${(10 * Math.log10(sig / err)).toFixed(1)} dB`)
}
