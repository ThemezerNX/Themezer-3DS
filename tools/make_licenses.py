#!/usr/bin/env python3
"""Writes THIRD_PARTY_NOTICES.md, the notices of what the app is built with, from the one list ENTRIES below, so it
cannot drift from the dependencies. The Makefile copies it into the app (romfs/licenses.txt), where the About screen
shows it. The license texts are devkitPro's portlib license files, the vendored sources, or written out below.

    python3 tools/make_licenses.py

Needs devkitPro (DEVKITPRO, default /opt/devkitpro) with the portlibs the app links.
"""
import os, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEVKITPRO = pathlib.Path(os.environ.get("DEVKITPRO", "/opt/devkitpro"))
PORTLIBS = DEVKITPRO / "portlibs" / "3ds" / "licenses"
OUT_NOTICES = ROOT / "THIRD_PARTY_NOTICES.md"

ISC = """Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE."""

MIT = """Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE."""

# The same terms, wrapped as in Bjoern Hoehrmann's file.
MIT_HOEHRMANN = """Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE."""

ZLIB = """This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution."""

THUMBHASH = ("Copyright (c) 2023 Stefano Verna\nCopyright (c) 2023 Evan Wallace\n\n" + MIT)

LUCIDE = ("ISC License\n\nCopyright (c) for portions of Lucide are held by Cole Bemis 2013-2022 as part of Feather "
          "(MIT). All other\ncopyright (c) for Lucide are held by Lucide Contributors 2022.\n\n" + ISC)
FEATHER = "The MIT License (MIT)\n\nCopyright (c) 2013-2023 Cole Bemis\n\n" + MIT

INTRO = ("Themezer-3DS is free software under the GNU General Public License, version 3 or later (see `LICENSE`).\n"
         "It is built with the libraries below, which keep their own licenses. Their notices are reproduced here\n"
         "and shipped inside the app, where the About screen shows them.\n\n"
         "This file is generated by `tools/make_licenses.py`; do not edit it by hand. To regenerate it:\n\n"
         "```sh\npython3 tools/make_licenses.py\n```")


def portlib(name, *files):
    return "\n\n".join((PORTLIBS / name / f).read_text(errors="replace").strip() for f in files)


def vendored(path):
    return (ROOT / path).read_text(errors="replace").strip()


def entry(name, use, text="", prose="", files=(), lic=""):
    """use and lic go into the table at the top; the rest into the section. No text: prose only."""
    return name, use, text, prose, files, lic


ENTRIES = [
    entry("Mozilla CA certificate list", "verifying HTTPS servers",
          prose="`romfs/cacert.pem` is the list of CA certificates from Mozilla, as extracted by the curl project\n"
          "(https://curl.se/docs/caextract.html). It is subject to the Mozilla Public License, version 2.0\n"
          "(https://mozilla.org/MPL/2.0/). The file is distributed unmodified; its source is linked in its header.",
          lic="MPL-2.0"),
    entry("C and C++ runtime", "newlib and libstdc++, from devkitARM",
          prose="The app is built with devkitARM. Its C library is newlib, whose parts are under BSD-style and similar\n"
          "permissive licenses (https://sourceware.org/newlib/COPYING.NEWLIB). Its C++ library, libstdc++, is under\n"
          "the GPL version 3 with the GCC Runtime Library Exception (https://www.gnu.org/licenses/gcc-exception-3.1.html).",
          lic="see below"),
    entry("libctru", "3DS system services", ZLIB,
          "Copyright (C) the libctru contributors (smea, fincs, WinterMute and others).", lic="zlib"),
    entry("citro3d and citro2d", "graphics", ZLIB,
          "Copyright (C) the citro3d and citro2d contributors (fincs, mtheall and others).", lic="zlib"),
    entry("curl", "downloads", portlib("3ds-curl", "COPYING"), lic="curl (MIT-style)"),
    entry("Mbed TLS", "HTTPS for curl", portlib("3ds-mbedtls", "LICENSE"),
          "Mbed TLS is used under the Apache License 2.0 (it is dual licensed under Apache-2.0 OR GPL-2.0-or-later).",
          lic="Apache-2.0"),
    entry("zlib", "zip archives, PNG", portlib("3ds-zlib", "LICENSE"), lic="zlib"),
    entry("libpng", "PNG images", portlib("3ds-libpng", "LICENSE"), lic="libpng-2.0"),
    entry("Jansson", "JSON", portlib("3ds-jansson", "LICENSE"), lic="MIT"),
    entry("ZXing-cpp", "QR code scanning",
          "Copyright Nu-book Inc.\nCopyright ZXing authors\nCopyright Axel Waggershauser\nCopyright gitlost\n"
          "Copyright Huy Cuong Nguyen\n\n" + vendored("third_party/zxing-cpp/LICENSE")
          + "\n\nIts UTF-8 decoder is Bjoern Hoehrmann's (http://bjoern.hoehrmann.de/utf-8/decoder/dfa/), "
          "under the MIT license:\n\nCopyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>\n\n" + MIT_HOEHRMANN,
          "ZXing-cpp (https://github.com/zxing-cpp/zxing-cpp) is used under the Apache License 2.0, whose text is below.\n"
          "Its files are unmodified (see `third_party/zxing-cpp`).", lic="Apache-2.0"),
    entry("ThumbHash", "the blurred backdrop", THUMBHASH,
          "The backdrop's ThumbHashes are decoded with a C adaptation of fast_thumbhash "
          "(https://github.com/datocms/fast_thumbhash)\nand encoded with a port of Evan Wallace's reference "
          "implementation (https://github.com/evanw/thumbhash), both under the\nMIT license "
          "(see `third_party/thumbhash`):", lic="MIT"),
    entry("Lucide", "the app's icons (gfx/icons)", LUCIDE + "\n\nThe parts that come from Feather:\n\n" + FEATHER,
          "The app's icons are drawn from Lucide (https://lucide.dev), the icon set the Themezer website uses.",
          lic="ISC; parts from Feather, MIT"),
]


def main():
    md = ["# Third-party notices", INTRO]
    table = ["| Component | Use | License |", "|---|---|---|"]
    table += [f"| {name} | {use} | {lic} |" for name, use, _, _, _, lic in ENTRIES]
    md.append("\n".join(table))
    for name, use, text, prose, files, _ in ENTRIES:
        md.append(f"## {name}")
        md.append(f"Used for: {use}.")
        if files:
            md.append("Files:\n\n" + "\n".join(f"- `{f}`" for f in files))
        if prose:
            md.append(prose)
        if text:
            md.append(f"```text\n{text}\n```")
    OUT_NOTICES.write_text("\n\n".join(md) + "\n")
    print(f"{OUT_NOTICES}: {len(md)} blocks")


if __name__ == "__main__":
    main()
