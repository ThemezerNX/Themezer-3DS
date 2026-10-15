#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
#
# Rebuilds meta/logo.bin, the boot animation, after the Homebrew App Store icon (design/hbappstore/icon.svg in the
# monorepo). See meta/README.md for what it is made of.
#
#     tools/bootlogo/build.sh
#
# Needs Node.js and the HOME Menu's logo HMAC key: 32 bytes as hex in ~/.config/themezer-3ds/logo-hmac.key, or in the
# file LOGO_KEY names. The key is never committed. The DARC, the scene and an animated preview (preview.gif, both
# screens) stay in out/assets/bootlogo.

. "$(dirname "$0")/../lib.sh"
need_monorepo

key=${LOGO_KEY:-$HOME/.config/themezer-3ds/logo-hmac.key}
if [ ! -f "$key" ]; then
    echo "No logo HMAC key at $key (see meta/README.md); without it the HOME Menu panics at launch." >&2
    exit 1
fi

out=$WORK/bootlogo
rm -rf "$out"
mkdir -p "$out"

# the logo and the wordmark, without the background layer
"$PYTHON" "$TOOLS/hide_svg.py" "$DESIGN/hbappstore/icon.svg" "$out/fg.svg" layer2
render "$out/fg.svg" "$out/fg.png" 2560 2400

here=$TOOLS/bootlogo
"$PYTHON" "$here/build_logo.py" "$out/fg.png" "$out" --key "$key"
"$PYTHON" "$here/render_logo.py" "$out" > /dev/null

install_asset "$out/logo.bin" meta/logo.bin
