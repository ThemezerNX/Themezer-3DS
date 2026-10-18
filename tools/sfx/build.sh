#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
#
# Rebuilds the UI sounds in romfs/sfx, synthesized from scratch by make_sfx.py to go with the music. A .wav of each, to
# listen to, stays in out/assets/sfx.
#
#     tools/sfx/build.sh

. "$(dirname "$0")/../lib.sh"

out=$WORK/sfx
rm -rf "$out"
mkdir -p "$out" "$ROOT/romfs/sfx"
"$PYTHON" "$TOOLS/sfx/make_sfx.py" "$out"
for pcm in "$out"/*.pcm; do
    install_asset "$pcm" "romfs/sfx/$(basename "$pcm")"
done
