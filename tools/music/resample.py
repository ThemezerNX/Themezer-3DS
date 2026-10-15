#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
"""Resamples a loop file (make_loop.py's output) to 32,728 Hz, the DSP's own rate, for encode_bcstm.ts.

The music after the loop start is appended past the loop end first, so the resampler sees what really follows the end;
the result is cut back at the loop end. Writes pcm32728-ch<N>.s16le per channel and pcm32728.json (rate, channels,
samples, loop start) into the out dir. Needs ffmpeg.

    python3 tools/music/resample.py <loop.wav> <out dir>
"""

import json
import os
import struct
import subprocess
import sys
import tempfile
import wave

import numpy as np

if len(sys.argv) != 3:
    sys.exit(__doc__.strip().splitlines()[-1].strip())
src, out, RATE = sys.argv[1], sys.argv[2], 32728
raw = open(src, 'rb').read()
i = raw.index(b'smpl')
loop_a, loop_b = struct.unpack_from('<II', raw, i + 8 + 36 + 8)
loop_b += 1  # smpl's end is inclusive
w = wave.open(src)
SR, CH = w.getframerate(), w.getnchannels()
x = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, CH)
assert loop_b == len(x), (loop_b, len(x))
ext = np.concatenate([x, x[loop_a:loop_a + SR // 2]])
with tempfile.TemporaryDirectory() as d:
    a, b = os.path.join(d, 'in.wav'), os.path.join(d, 'out.raw')
    with wave.open(a, 'wb') as o:
        o.setnchannels(CH); o.setsampwidth(2); o.setframerate(SR); o.writeframes(ext.tobytes())
    resampler = ('aresample=resampler=swr:filter_size=64:phase_shift=10:exact_rational=1:cutoff=0.95:filter_type=kaiser:'
                 'kaiser_beta=9:dither_method=triangular')
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', a, '-af', resampler, '-ar', str(RATE), '-f', 's16le',
                    '-acodec', 'pcm_s16le', b], check=True)
    y = np.frombuffer(open(b, 'rb').read(), np.int16).reshape(-1, CH)
A, B = int(round(loop_a * RATE / SR)), int(round(loop_b * RATE / SR))
y = np.ascontiguousarray(y[:B])
os.makedirs(out, exist_ok=True)
for c in range(CH):
    open(os.path.join(out, f'pcm{RATE}-ch{c}.s16le'), 'wb').write(y[:, c].astype('<i2').tobytes())
json.dump({'rate': RATE, 'channels': CH, 'samples': B, 'loopStart': A}, open(os.path.join(out, f'pcm{RATE}.json'), 'w'))
print(f'{B} samples at {RATE} Hz ({B / RATE:.3f} s), loop start {A} ({A / RATE:.3f} s), loop {(B - A) / RATE:.3f} s')
