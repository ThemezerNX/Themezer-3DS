#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
#
# Rebuilds meta/icon.png (the HOME Menu icon) and romfs/badge-set.png (the same image, for the badge set the app
# installs) from the Themezer design files.
#
#     tools/icon/build.sh
#
# Needs Node.js.

. "$(dirname "$0")/../lib.sh"
need_monorepo

out=$WORK/icon
rm -rf "$out"
mkdir -p "$out"
render "$DESIGN/icon.svg" "$out/icon_1024.png" 1024 600
"$PYTHON" "$TOOLS/icon/make_icon.py" "$out/icon_1024.png" "$out"

install_asset "$out/icon.png" meta/icon.png
install_asset "$out/icon.png" romfs/badge-set.png
