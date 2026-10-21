#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
#
# Rebuilds the UI sounds in romfs/sfx, synthesized from scratch by make_sfx.py to go with the music and encoded to
# DSP-ADPCM by encode_sfx.ts. A .wav of each, to listen to, stays in out/assets/sfx.
#
# Needs Bun and the Themezer monorepo (its BCSTM encoder).
#
#     tools/sfx/build.sh

. "$(dirname "$0")/../lib.sh"
need_monorepo

out=$WORK/sfx
rm -rf "$out"
mkdir -p "$out" "$ROOT/romfs/sfx"
"$PYTHON" "$TOOLS/sfx/make_sfx.py" "$out"
# all their sound is below 2.5 kHz, so 8 kHz loses nothing (the app plays them at their own rate)
for wav in "$out"/*.wav; do
    ffmpeg -v error -y -i "$wav" -af aresample=resampler=swr:filter_size=64:cutoff=0.95:filter_type=kaiser:kaiser_beta=9 \
        -ar 8000 -f s16le -c:a pcm_s16le "${wav%.wav}.pcm"
done
bun "$TOOLS/sfx/encode_sfx.ts" "$out" 8000 "$MONOREPO"
for clip in "$out"/*.bcstm; do
    install_asset "$clip" "romfs/sfx/$(basename "$clip")"
done
