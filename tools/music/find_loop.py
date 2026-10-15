# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# Finds a seamless loop in a song: two beat-aligned points A < B where the music around them is the same, so playback
# can jump from B back to A unnoticed.
# usage: find_loop.py <song.wav> <out dir>
# Steps: spectral flux onsets -> tempo by autocorrelation -> beats by dynamic programming (Ellis 2007) -> per-beat
# chroma (harmony) and log-mel (timbre, loudness) -> for every pair of beats a whole number of bars apart, how alike
# the 8 beats before and the 8 beats after each point are -> the best pair with a long loop, after the intro and before
# any fade at the end.
import sys, os, wave, json
import numpy as np
from PIL import Image, ImageDraw

src, out = sys.argv[1], sys.argv[2]
w = wave.open(src)
SR = w.getframerate()
audio = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, w.getnchannels()).astype(np.float64) / 32768
mono = audio.mean(1)
N_FFT, HOP = 4096, 512
frames = 1 + (len(mono) - N_FFT) // HOP
win = np.hanning(N_FFT)
idx = np.arange(N_FFT)[None, :] + HOP * np.arange(frames)[:, None]
spec = np.abs(np.fft.rfft(mono[idx] * win, axis=1))          # (frames, bins)
freqs = np.fft.rfftfreq(N_FFT, 1 / SR)
fps = SR / HOP

# onset strength: positive log-spectral flux
logspec = np.log1p(100 * spec)
flux = np.maximum(logspec[1:] - logspec[:-1], 0).sum(1)
flux = np.concatenate([[0], flux])
flux -= np.convolve(flux, np.ones(31) / 31, mode='same')
flux = np.maximum(flux, 0)

# tempo: autocorrelation of the onset strength, weighted towards 120 bpm
ac = np.correlate(flux, flux, mode='full')[len(flux) - 1:]
lags = np.arange(len(ac))
bpm_of = lambda lag: 60 * fps / lag
cand = [(lag, ac[lag] * np.exp(-0.5 * (np.log2(bpm_of(lag) / 120) / 0.9) ** 2)) for lag in range(int(60 * fps / 200), int(60 * fps / 50))]
period = max(cand, key=lambda c: c[1])[0]
# refine to a fractional period
ref = [(p, sum(np.interp(k * p, lags, ac) for k in range(1, 9))) for p in np.arange(period - 1, period + 1, 0.01)]
period = max(ref, key=lambda r: r[1])[0]
bpm = bpm_of(period)

# beats: dynamic programming, rewarding onsets and spacing near the period
score = flux / (flux.std() + 1e-9)
best = score.copy(); back = np.full(len(score), -1)
for t in range(len(score)):
    lo, hi = int(t - 2 * period), int(t - period / 2)
    if hi <= 0: continue
    prev = np.arange(max(0, lo), hi)
    pen = -100 * (np.log((t - prev) / period)) ** 2
    k = np.argmax(best[prev] + pen)
    best[t] = score[t] + best[prev][k] + pen[k]; back[t] = prev[k]
t = int(np.argmax(best[-int(2 * period):])) + len(best) - int(2 * period)
beats = []
while t >= 0:
    beats.append(t); t = back[t]
beats = np.array(beats[::-1])

# per-frame features: chroma (12 pitch classes, 80 Hz - 5 kHz) and log-mel (40 bands)
valid = (freqs > 80) & (freqs < 5000)
pc = np.round(12 * np.log2(freqs[valid] / 440)).astype(int) % 12
chroma = np.zeros((frames, 12))
for c in range(12):
    chroma[:, c] = (spec[:, valid][:, pc == c] ** 2).sum(1)
mel_edges = 700 * (10 ** (np.linspace(2595 * np.log10(1 + 40 / 700), 2595 * np.log10(1 + 16000 / 700), 42) / 2595) - 1)
mel = np.stack([(spec[:, (freqs >= mel_edges[i]) & (freqs < mel_edges[i + 2])] ** 2).sum(1) for i in range(40)], 1)
logmel = np.log10(mel + 1e-9)
rms_db = 10 * np.log10((spec ** 2).sum(1) + 1e-12)


def beat_sync(f):
    return np.stack([f[a:b].mean(0) for a, b in zip(beats[:-1], beats[1:])])


C = beat_sync(chroma); C = C / (np.linalg.norm(C, axis=1, keepdims=True) + 1e-12)
M = beat_sync(logmel); M = M - M.mean(1, keepdims=True); M = M / (np.linalg.norm(M, axis=1, keepdims=True) + 1e-12)
E = beat_sync(rms_db[:, None])[:, 0]
nb = len(C)
sim = 0.5 * (C @ C.T) + 0.5 * (M @ M.T)
loud = -np.abs(E[:, None] - E[None, :]) / 6         # dB apart, a 6 dB step costs 1

# candidate pairs: i < j, a whole number of bars (4 beats) apart, loop at least 40 % of the song, the loop end
# before the song gets quieter at the end (the last 12 % of beats excluded), the start after the first 6 %
W = 8
pairs = []
for i in range(W, nb - W):
    for j in range(i + 16, nb - W):
        if (j - i) % 4: continue
        if (j - i) < 0.40 * nb or i < 0.06 * nb or j > 0.88 * nb: continue
        before = np.mean([sim[i - k, j - k] for k in range(1, W + 1)])
        after = np.mean([sim[i + k, j + k] for k in range(0, W)])
        lv = np.mean([loud[i + k, j + k] for k in range(-W, W)])
        pairs.append((before + after + 0.3 * lv, i, j, before, after, lv))
pairs.sort(reverse=True)
beat_t = beats / fps

top = []
for s, i, j, b, a, lv in pairs:
    if any(abs(i - ti) < 8 and abs(j - tj) < 8 for _, ti, tj, *_ in top): continue
    top.append((s, i, j, b, a, lv))
    if len(top) == 6: break
result = {'bpm': round(bpm, 2), 'beats': len(beats), 'candidates': [
    {'score': round(s, 3), 'A_beat': int(i), 'B_beat': int(j), 'A': round(float(beat_t[i]), 3), 'B': round(float(beat_t[j]), 3),
     'loop_s': round(float(beat_t[j] - beat_t[i]), 2), 'bars': (j - i) // 4, 'sim_before': round(b, 3), 'sim_after': round(a, 3), 'loudness': round(lv, 3)}
    for s, i, j, b, a, lv in top]}
json.dump({'beat_times': beat_t.tolist(), **result}, open(os.path.join(out, 'loop.json'), 'w'))
print(json.dumps(result, indent=1))

# picture: the beat self-similarity, the loudness over time, and the best pair
S = 600
img = Image.new('RGB', (S + 20, S + 140), (0, 0, 0))
simg = np.clip((sim - sim.min()) / (sim.max() - sim.min()), 0, 1)
img.paste(Image.fromarray((simg * 255).astype(np.uint8)).resize((S, S)).convert('RGB'), (10, 10))
d = ImageDraw.Draw(img)
_, i, j, *_ = top[0]
for k, (ci, cj) in enumerate([(c['A_beat'], c['B_beat']) for c in result['candidates']]):
    x, y = 10 + cj * S / nb, 10 + ci * S / nb
    d.ellipse([x - 5, y - 5, x + 5, y + 5], outline=(255, 60, 60) if k == 0 else (255, 200, 0), width=2)
env = rms_db[::max(1, frames // S)][:S]
env = (env - env.max() + 40) / 40
for x, v in enumerate(env):
    d.line([(10 + x, S + 130), (10 + x, S + 130 - max(0, v) * 100)], fill=(120, 170, 255))
for k in (i, j):
    x = 10 + beat_t[k] / (len(mono) / SR) * S
    d.line([(x, S + 25), (x, S + 135)], fill=(255, 60, 60), width=2)
d.text((12, S + 14), f'{bpm:.1f} bpm, A {beat_t[i]:.2f} s, B {beat_t[j]:.2f} s', fill=(255, 255, 255))
img.save(os.path.join(out, 'loop.png'))
