# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
#
# Sourced by the asset build scripts (tools/*/build.sh): paths, Python with the pinned packages, SVG rendering.
#
# Environment:
#   THEMEZER_MONOREPO  the Themezer monorepo, for the design files, sharp and the BCSTM encoder
#                      (default ~/dev/themezer/themezer-monorepo)
#   PYTHON             a Python that has the packages in tools/requirements.txt (default: tools/.venv, made on first use)

set -eu

TOOLS=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(dirname "$TOOLS")
MONOREPO=${THEMEZER_MONOREPO:-$HOME/dev/themezer/themezer-monorepo}
DESIGN=$MONOREPO/design
WORK=$ROOT/out/assets   # intermediate files and previews
PATH=$PATH:$HOME/.bun/bin

need_monorepo() {
    if [ ! -d "$DESIGN" ]; then
        echo "The Themezer monorepo isn't at $MONOREPO: set THEMEZER_MONOREPO to where it is." >&2
        exit 1
    fi
}

if [ -z "${PYTHON:-}" ]; then
    PYTHON=$TOOLS/.venv/bin/python
    if [ ! -x "$PYTHON" ]; then
        echo "setting up tools/.venv ..."
        python3 -m venv "$TOOLS/.venv"
        "$PYTHON" -m pip install -q --disable-pip-version-check -r "$TOOLS/requirements.txt"
    fi
fi

# render <in.svg> <out.png> <width> <density>
render() {
    node "$TOOLS/render_svg.mjs" "$1" "$2" "$3" "$4" "$MONOREPO"
}

# install_asset <built file> <path in the repository>: puts a result in place, saying whether it changed
install_asset() {
    if cmp -s "$1" "$ROOT/$2"; then
        echo "$2: unchanged"
    else
        cp "$1" "$ROOT/$2"
        echo "$2: updated"
    fi
}
