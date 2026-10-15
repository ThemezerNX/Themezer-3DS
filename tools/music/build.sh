#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
#
# Rebuilds romfs/bgm.bcstm, the app's background music, from the song "Aether" (made with Suno), Aether.m4a next to
# this script. See meta/README.md for how the loop was chosen.
#
#     tools/music/build.sh [song]
#
# The loop points below are Aether's: another song needs its own.
#
# Needs ffmpeg, Bun and the Themezer monorepo (its BCSTM encoder). The loop (with -seam.wav and -3x.wav to listen to the
# join) and the decoded BCSTM stay in out/assets/music.
#
# For another song: find_loop.py <song.wav> <out dir> suggests loop points, make_loop.py cuts the loop, then resample.py
# and encode_bcstm.ts as below.

. "$(dirname "$0")/../lib.sh"
need_monorepo

here=$TOOLS/music
song=${1:-$here/Aether.m4a}

out=$WORK/music
rm -rf "$out"
mkdir -p "$out"

ffmpeg -v error -y -i "$song" -c:a pcm_s16le "$out/song.wav"
# 16 bars from 16.20 s to 45.06 s, a one-beat crossfade at 133.04 bpm, the end moved by at most 5 ms to line up
"$PYTHON" "$here/make_loop.py" "$out/song.wav" 16.20 45.06 "$out/loop" 1.0 133.04 5
"$PYTHON" "$here/resample.py" "$out/loop.wav" "$out"
bun "$here/encode_bcstm.ts" "$out" "$out/bgm.bcstm" "$MONOREPO"

install_asset "$out/bgm.bcstm" romfs/bgm.bcstm
