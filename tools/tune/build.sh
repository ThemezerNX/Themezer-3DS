#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
#
# Rebuilds meta/audio.wav, the tune the HOME Menu plays with the banner: "rise-strings", synthesized from scratch.
# See meta/README.md.
#
#     tools/tune/build.sh
#
# For the other candidates, to listen to: tools/.venv/bin/python tools/tune/make_tunes.py out/assets/tune [--instruments]

. "$(dirname "$0")/../lib.sh"

out=$WORK/tune
rm -rf "$out"
mkdir -p "$out"
"$PYTHON" "$TOOLS/tune/make_tunes.py" "$out" --only rise-strings
# 24 kHz is plenty for the 3DS speakers and makes the banner smaller
ffmpeg -v error -y -i "$out/tune-rise-strings.wav" -af aresample=resampler=swr:filter_size=64:cutoff=0.95:filter_type=kaiser:kaiser_beta=9:dither_method=triangular \
    -ar 24000 -c:a pcm_s16le -map_metadata -1 -bitexact "$out/audio.wav"
install_asset "$out/audio.wav" meta/audio.wav
