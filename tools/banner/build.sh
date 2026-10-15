#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
#
# Rebuilds meta/banner.cgfx, the 3D HOME Menu banner, from the Themezer design files (design/icon.svg and
# design/logotext.svg in the monorepo). See meta/README.md for what it is made of.
#
#     tools/banner/build.sh
#
# Needs Node.js, devkitPro's tex3ds and git (pycgfx is fetched into tools/.cache on first use). The glTF, its textures
# and a flat-shaded preview stay in out/assets/banner.

. "$(dirname "$0")/../lib.sh"
need_monorepo

PYCGFX_COMMIT=1f78850086f3a77c41e07162e842f97a5bf3c18a
pycgfx=$TOOLS/.cache/pycgfx
if [ ! -d "$pycgfx/.git" ]; then
    git clone -q https://github.com/skyfloogle/pycgfx "$pycgfx"
fi
git -C "$pycgfx" cat-file -e "$PYCGFX_COMMIT^{commit}" 2>/dev/null || git -C "$pycgfx" fetch -q origin
git -C "$pycgfx" checkout -q "$PYCGFX_COMMIT"

out=$WORK/banner
rm -rf "$out"
mkdir -p "$out/art"

# the icon's art for the logo's front, and without its d-pad and buttons for its back
render "$DESIGN/icon.svg" "$out/art/icon.png" 512 600
"$PYTHON" "$TOOLS/hide_svg.py" "$DESIGN/icon.svg" "$out/art/icon_back.svg" path1-8-8 path1-8-9-6
render "$out/art/icon_back.svg" "$out/art/icon_back.png" 512 600

here=$TOOLS/banner
"$PYTHON" "$here/make_banner.py" "$DESIGN" "$out" "$out/art/icon.png" --uv-zoom 0.75 --back-art "$out/art/icon_back.png"
"$PYTHON" "$here/convert_banner.py" "$pycgfx" "$out/banner.gltf" "$out/banner.cgfx" --name-uv-zoom 0.75 --name-la8 --name-linear
"$PYTHON" "$here/preview.py" "$out" > /dev/null || echo "(no preview)"

install_asset "$out/banner.cgfx" meta/banner.cgfx
