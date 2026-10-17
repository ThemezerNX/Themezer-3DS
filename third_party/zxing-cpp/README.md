# ZXing-cpp

QR code reading, used by the QR code scanner (`source/services/qr_zxing.cpp`). From
https://github.com/zxing-cpp/zxing-cpp, under the Apache License 2.0 (`LICENSE`).

## Version

Upstream's master at commit `2c3dcfe` (6 October 2026), newer than release 3.1.1 for the fix of a hang in the QR
code detector (`a17fd9d`). Only the files of upstream's `core/src` that reading QR codes needs, unmodified: the
sources of its reader, and the headers they include. `Version.h`, which upstream's CMake generates, is written by hand
here: readers only, QR codes only, no Unicode tables. So of libzueci (BSD-3-Clause, `libzueci/LICENSE`) only its header
is here, for the declarations ZXing-cpp includes; none of its code is built.

The Makefile builds these files with exceptions on, which ZXing-cpp throws and catches as it decodes, and warnings off.
