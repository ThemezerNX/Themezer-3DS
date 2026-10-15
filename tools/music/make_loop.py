# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# Cuts a seamlessly looping version of a song at loop points A < B (seconds, from find_loop.py).
# usage: make_loop.py <song.wav> <A> <B> <out prefix> [crossfade beats] [bpm] [max shift of B, ms]
# - B is moved by up to +-30 ms (or the given maximum) to where the waveform around it lines up best with A's
# - the last stretch before B is crossfaded into the stretch before A, so the jump from B back to A is part of the
#   file: linear when the two are alike (no loudness bump), equal power otherwise
# Writes <prefix>.wav (start to B, a 'smpl' chunk marking the loop A..B, for converters that read loop points),
# <prefix>-seam.wav (10 s before the jump and 10 s after it, to listen to the join) and <prefix>-3x.wav (the intro and
# the loop three times, to hear it running).
import struct, sys, wave
import numpy as np

src, A_s, B_s, prefix = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
xf_beats = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0
bpm = float(sys.argv[6]) if len(sys.argv) > 6 else 133.04
max_shift = float(sys.argv[7]) / 1000 if len(sys.argv) > 7 else 0.03  # how far B may move to line up, seconds
w = wave.open(src)
SR, CH = w.getframerate(), w.getnchannels()
x = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, CH).astype(np.float64) / 32768
mono = x.mean(1)
A, B = int(round(A_s * SR)), int(round(B_s * SR))
X = int(round(xf_beats * 60 / bpm * SR))

# align B: the lag (within 30 ms) where the two seconds around B correlate best with the two seconds around A
H = SR
a = mono[A - H:A + H]
best = None
for d in range(-int(max_shift * SR), int(max_shift * SR) + 1):
    b = mono[B + d - H:B + d + H]
    c = np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-12)
    if best is None or c > best[0]: best = (c, d)
corr, d = best
B += d

# crossfade the X samples before B into the X samples before A
pre_a, pre_b = x[A - X:A], x[B - X:B]
c_ab = float(np.dot(pre_a.ravel(), pre_b.ravel()) / (np.linalg.norm(pre_a) * np.linalg.norm(pre_b) + 1e-12))
t = (np.arange(X) + 0.5) / X
if c_ab > 0.5:
    fin, fout = t, 1 - t
else:
    fin, fout = np.sin(t * np.pi / 2), np.cos(t * np.pi / 2)
loop_file = x[:B].copy()
loop_file[B - X:B] = pre_b * fout[:, None] + pre_a * fin[:, None]


def write(path, data, loop=None):
    pcm = np.round(np.clip(data, -1, 1) * 32767).astype('<i2').tobytes()
    chunks = b'fmt ' + struct.pack('<IHHIIHH', 16, 1, CH, SR, SR * CH * 2, CH * 2, 16)
    if loop:
        s, e = loop
        smpl = struct.pack('<9I', 0, 0, int(1e9 / SR), 60, 0, 0, 0, 1, 0) + struct.pack('<6I', 0, 0, s, e - 1, 0, 0)
        chunks += b'smpl' + struct.pack('<I', len(smpl)) + smpl
    chunks += b'data' + struct.pack('<I', len(pcm)) + pcm
    open(path, 'wb').write(b'RIFF' + struct.pack('<I', 4 + len(chunks)) + b'WAVE' + chunks)


write(prefix + '.wav', loop_file, (A, B))
body = loop_file[A:B]
seam = np.concatenate([loop_file[B - 10 * SR:B], body[:10 * SR]])
write(prefix + '-seam.wav', seam)
write(prefix + '-3x.wav', np.concatenate([loop_file[:A], body, body, body]))
print(f'{prefix}: A {A / SR:.3f} s (sample {A}), B {B / SR:.3f} s (sample {B}, moved {d / SR * 1000:+.1f} ms, '
      f'match {corr:.2f}), loop {(B - A) / SR:.2f} s, crossfade {X / SR * 1000:.0f} ms '
      f'{"linear" if c_ab > 0.5 else "equal power"} (alike {c_ab:.2f})')
