# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# LZ11 (Nintendo LZ77 variant 0x11) compression and decompression.
# The compressor finds matches through a table of 3-byte prefixes and parses optimally (shortest output by dynamic
# programming over the positions), which matters for the boot logo's 0x2000-byte limit.


def lz11_decompress(data):
    assert data[0] == 0x11
    size = data[1] | data[2] << 8 | data[3] << 16
    pos = 4
    if size == 0:
        size = int.from_bytes(data[4:8], 'little'); pos = 8
    out = bytearray()
    while len(out) < size:
        flags = data[pos]; pos += 1
        for bit in range(8):
            if len(out) >= size: break
            if flags & (0x80 >> bit):
                b = data[pos] >> 4
                if b == 0:
                    length = (((data[pos] & 0xF) << 4) | (data[pos + 1] >> 4)) + 0x11
                    disp = (((data[pos + 1] & 0xF) << 8) | data[pos + 2]) + 1; pos += 3
                elif b == 1:
                    length = (((data[pos] & 0xF) << 12) | (data[pos + 1] << 4) | (data[pos + 2] >> 4)) + 0x111
                    disp = (((data[pos + 2] & 0xF) << 8) | data[pos + 3]) + 1; pos += 4
                else:
                    length = b + 1
                    disp = (((data[pos] & 0xF) << 8) | data[pos + 1]) + 1; pos += 2
                for _ in range(length):
                    out.append(out[-disp])
            else:
                out.append(data[pos]); pos += 1
    return bytes(out)


def _token_bits(length):
    # size of a match token in bits, plus its flag bit
    return 1 + 8 * (2 if length <= 0x10 else 3 if length <= 0x110 else 4)


def _common(src, i, j, limit):
    # length of the common prefix of src[i:] and src[j:] (j < i; overlapping is fine), at most limit
    lo, hi = 0, limit
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if src[j:j + mid] == src[i:i + mid]: lo = mid
        else: hi = mid - 1
    return lo


def lz11_compress(src):
    src = bytes(src)
    n = len(src)
    # earlier positions with the same 3 bytes, nearest first
    heads = {}
    prev = [-1] * n
    for i in range(n - 2):
        k = src[i:i + 3]
        prev[i] = heads.get(k, -1)
        heads[k] = i
    # matches[i]: (length, displacement) pairs worth considering, longest first
    matches = [None] * n
    for i in range(n):
        best = {}
        limit = min(0x10110, n - i)
        j = prev[i] if i < n - 2 else -1
        steps = 0
        while j >= 0 and i - j <= 0x1000 and steps < 128:
            l = _common(src, i, j, limit)
            if l >= 3 and l not in best:
                best[l] = i - j
            if l == limit: break
            j = prev[j]; steps += 1
        matches[i] = sorted(best.items(), reverse=True)[:8]
    # dynamic programming from the end: cost[i] = bits to encode src[i:]
    INF = float('inf')
    cost = [INF] * (n + 1); cost[n] = 0
    choice = [None] * n
    for i in range(n - 1, -1, -1):
        c = 9 + cost[i + 1]; ch = None
        for l, d in matches[i]:
            # any length from 3 to l can be used with this displacement; try the band edges and l itself
            for L in {l, min(l, 0x10), min(l, 0x110), 3}:
                if L < 3: continue
                cc = _token_bits(L) + cost[i + L]
                if cc < c: c, ch = cc, (L, d)
        cost[i], choice[i] = c, ch
    out = bytearray([0x11, n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF])
    i = 0
    while i < n:
        flag_at = len(out); out.append(0); flags = 0
        for bit in range(8):
            if i >= n: break
            ch = choice[i]
            if ch is None:
                out.append(src[i]); i += 1
            else:
                L, d = ch
                flags |= 0x80 >> bit
                dd = d - 1
                if L <= 0x10:
                    out += bytes([((L - 1) << 4) | (dd >> 8), dd & 0xFF])
                elif L <= 0x110:
                    l = L - 0x11
                    out += bytes([l >> 4, ((l & 0xF) << 4) | (dd >> 8), dd & 0xFF])
                else:
                    l = L - 0x111
                    out += bytes([0x10 | (l >> 12), (l >> 4) & 0xFF, ((l & 0xF) << 4) | (dd >> 8), dd & 0xFF])
                i += L
        out[flag_at] = flags
    return bytes(out)
