#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
"""Copies an SVG with some of its elements hidden (display:none), to render parts of a design apart.

    python3 tools/hide_svg.py <in.svg> <out.svg> <element id>...
"""

import re
import sys


def hide(svg, element_id):
    at = svg.find(f'id="{element_id}"')
    if at < 0:
        sys.exit(f'no element with id "{element_id}"')
    start, end = svg.rindex('<', 0, at), svg.index('>', at)
    tag = svg[start:end]
    style = re.search(r'\bstyle="([^"]*)"', tag)
    if not style:
        tag = tag.rstrip('/').rstrip() + ' style="display:none"' + (' /' if tag.endswith('/') else '')
    elif re.search(r'(^|;)\s*display:', style.group(1)):
        tag = tag[:style.start(1)] + re.sub(r'((?:^|;)\s*display:)[^;]*', r'\1none', style.group(1)) + tag[style.end(1):]
    else:
        tag = tag[:style.start(1)] + 'display:none;' + style.group(1) + tag[style.end(1):]
    return svg[:start] + tag + svg[end:]


if __name__ == '__main__':
    if len(sys.argv) < 4:
        sys.exit(__doc__.strip().splitlines()[-1].strip())
    svg = open(sys.argv[1], encoding='utf-8').read()
    for element_id in sys.argv[3:]:
        svg = hide(svg, element_id)
    open(sys.argv[2], 'w', encoding='utf-8').write(svg)
