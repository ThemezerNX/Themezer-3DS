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
install_asset "$out/tune-rise-strings.wav" meta/audio.wav
