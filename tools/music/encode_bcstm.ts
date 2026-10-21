// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors
//
// Encodes resample.py's PCM into a looping DSP-ADPCM BCSTM with the Themezer webapp's encoder (encodePcmToBcstm in the
// monorepo's packages/3dstheme-builder), and checks the result with its decoder.
// usage: bun encode_bcstm.ts <pcm dir> <out.bcstm> [path/to/themezer-monorepo]
import { readFileSync, writeFileSync } from "node:fs"
import { homedir } from "node:os"
import { join } from "node:path"

const [dir, out] = process.argv.slice(2, 4)
const monorepo = process.argv[4] ?? process.env.THEMEZER_MONOREPO ?? join(homedir(), "dev/themezer/themezer-monorepo")
const { encodePcmToBcstm, decodeBcstmToPcm, parseBcstm } =
    await import(join(monorepo, "packages/3dstheme-builder/src/theme/bcstm/bcstm"))

const meta = JSON.parse(readFileSync(join(dir, "pcm.json"), "utf8"))
const channels: Int16Array[] = []
for (let c = 0; c < meta.channels; c++) {
    const bytes = readFileSync(join(dir, `pcm-ch${c}.s16le`))
    channels.push(new Int16Array(bytes.buffer, bytes.byteOffset, bytes.byteLength / 2).slice())
}
const bcstm = encodePcmToBcstm({ channels, sampleRate: meta.rate, encoding: "dspAdpcm", loopPoint: { startFrame: meta.loopStart } })
writeFileSync(out, bcstm)

const parsed = parseBcstm(bcstm)
const s = parsed.streamInfo
const decoded = decodeBcstmToPcm(bcstm)
const shift = s.loopStartFrame - meta.loopStart
// the loop must hold the same music: after the shifted loop start, the original loop's music from that offset
let worst = 0
for (let c = 0; c < meta.channels; c++) {
    let err = 0, sig = 0
    const d = decoded.channels[c]!
    for (let i = 0; i < meta.samples; i++) {
        const e = d[i]! - channels[c]![i]!
        err += e * e
        sig += channels[c]![i]! * channels[c]![i]!
    }
    console.log(`channel ${c}: SNR ${(10 * Math.log10(sig / err)).toFixed(1)} dB`)
    for (let i = 0; i < shift; i++) worst = Math.max(worst, Math.abs(d[meta.samples + i]! - channels[c]![meta.loopStart + i]!))
}
writeFileSync(out + ".decoded.s16le", Buffer.from(new Int16Array(decoded.channels[0]!.length * meta.channels).map((_, i) => decoded.channels[i % meta.channels]![Math.floor(i / meta.channels)]!).buffer))
console.log(`${out}: ${bcstm.length} bytes, ${s.sampleRate} Hz, ${s.channelCount} ch, ${s.sampleCount} samples, loop ${s.loopStartFrame}..${s.sampleCount} ` +
    `(start moved ${shift} samples to a block boundary: ${s.loopStartFrame % s.sampleBlockSampleCount === 0}), ${s.sampleBlockCount} blocks; ` +
    `appended loop music differs from the source by at most ${worst}`)
