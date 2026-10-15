# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# Candidate HOME Menu banner tunes for Themezer: short piano phrases with a stereo echo, synthesized from scratch.
# usage: make_tunes.py <out dir> [--lower | --instruments | --only <name>]  -> tune-<name>.wav for each candidate (44.1 kHz
# stereo 16-bit, 2.95 s: the HOME Menu plays about 3 s at most). The banner's tune is --only bloom-harp.
# Each phrase is over by about 1.2 s, so the notes and their echoes ring out for the rest; the fade only follows
# that decay. Piano: inharmonic partials of a stiff string, two slightly detuned strings per note, a fast then slow
# decay, brighter when played harder, a hammer thump; the sustain pedal changes with the chords.
import os, sys, wave
import numpy as np

SR = 44100
LENGTH = 2.95
N = int(SR * LENGTH)
t_all = np.arange(N) / SR


def midi(name):
    notes = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
    acc = name[1:-1]
    return 12 * (int(name[-1]) + 1) + notes[name[0]] + acc.count('#') - acc.count('b')


def piano(m, vel, n_s, rng, damp_at=None):
    f0 = 440.0 * 2 ** ((m - 69) / 12)
    t = np.arange(n_s) / SR
    B = np.clip(2.5e-4 * (f0 / 261.6), 6e-5, 1.5e-3)
    out = np.zeros(n_s)
    pitch_scale = (261.6 / f0) ** 0.55
    bright = 0.55 + 0.45 * vel
    for n in range(1, 40):
        fn = n * f0 * np.sqrt(1 + B * n * n)
        if fn > 12000: break
        amp = (1 / n ** 1.15) * abs(np.sin(np.pi * n * 0.12)) * np.exp(-(n - 1) * (1.05 - bright) * 0.55)
        tau1 = 0.35 * pitch_scale / (1 + 0.18 * (n - 1))
        tau2 = 3.2 * pitch_scale / (1 + 0.10 * (n - 1))
        env = 0.65 * np.exp(-t / tau1) + 0.35 * np.exp(-t / tau2)
        for detune in (-0.55, 0.55):
            out += 0.5 * amp * env * np.sin(2 * np.pi * fn * 2 ** (detune / 1200) * t + rng.uniform(0, 2 * np.pi))
    out *= np.clip(t / 0.0015, 0, 1)
    thump_len = int(0.03 * SR)
    k = np.exp(-np.linspace(0, 1, 64) * 6); k /= k.sum()
    thump = np.convolve(rng.standard_normal(thump_len), k, mode='same') * np.exp(-np.arange(thump_len) / (0.006 * SR))
    out[:thump_len] += 0.06 * vel * thump
    if damp_at is not None:
        # the pedal lifts: the dampers stop the string within about a tenth of a second
        out *= np.where(t < damp_at, 1.0, np.exp(-(t - damp_at) / 0.07))
    return out * vel ** 1.3


def damp(out, t, damp_at):
    # the pedal lifts: the dampers stop the sound within about a tenth of a second
    return out if damp_at is None else out * np.where(t < damp_at, 1.0, np.exp(-(t - damp_at) / 0.07))


def mallet(n_s, rng, vel, level, decay=0.004, smooth=24):
    k = np.exp(-np.linspace(0, 1, smooth) * 6); k /= k.sum()
    n = int(0.03 * SR)
    hit = np.convolve(rng.standard_normal(n), k, mode='same') * np.exp(-np.arange(n) / (decay * SR))
    out = np.zeros(n_s); out[:min(n, n_s)] = level * vel * hit[:n_s]
    return out


def bar(partials, tau, attack=0.002, tremolo=None, hit=0.04, hit_smooth=24):
    # struck bars and tines: a few inharmonic partials (ratio, level, decay factor), the higher ones dying faster
    def voice(m, vel, n_s, rng, damp_at=None):
        f0 = 440.0 * 2 ** ((m - 69) / 12)
        t = np.arange(n_s) / SR
        ps = (261.6 / f0) ** 0.5
        out = np.zeros(n_s)
        for ratio, amp, tf in partials:
            fn = ratio * f0
            if fn > 15000: continue
            a = amp * (0.6 + 0.4 * vel) ** (1 if ratio == 1 else 2)
            out += a * np.exp(-t / (tau * tf * ps)) * np.sin(2 * np.pi * fn * t + rng.uniform(0, 2 * np.pi))
        out *= np.clip(t / attack, 0, 1)
        if tremolo:
            rate, depth = tremolo
            out *= 1 - depth * (0.5 - 0.5 * np.cos(2 * np.pi * rate * t))
        out += mallet(n_s, rng, vel, hit, smooth=hit_smooth)
        return damp(out, t, damp_at) * vel ** 1.2
    return voice


def harp(m, vel, n_s, rng, damp_at=None):
    # a plucked string: harmonics shaped by where it is plucked, the upper ones dying fast
    f0 = 440.0 * 2 ** ((m - 69) / 12)
    t = np.arange(n_s) / SR
    ps = (261.6 / f0) ** 0.5
    out = np.zeros(n_s)
    for n in range(1, 24):
        if n * f0 > 12000: break
        amp = abs(np.sin(np.pi * n * 0.2)) / n ** 1.5 * np.exp(-(n - 1) * (1 - vel) * 0.4)
        out += amp * np.exp(-t / (2.4 * ps / (1 + 0.35 * (n - 1)))) * np.sin(2 * np.pi * n * f0 * t + rng.uniform(0, 2 * np.pi))
    out *= np.clip(t / 0.003, 0, 1)
    out += mallet(n_s, rng, vel, 0.02, decay=0.002, smooth=8)
    return damp(out, t, damp_at) * vel ** 1.2


def rhodes(m, vel, n_s, rng, damp_at=None):
    # electric piano as two FM pairs: a warm body (1:1) and a bright tine at the attack (14:1)
    f0 = 440.0 * 2 ** ((m - 69) / 12)
    t = np.arange(n_s) / SR
    ps = (261.6 / f0) ** 0.5
    ph = rng.uniform(0, 2 * np.pi)
    index = (0.6 + 1.2 * vel) * np.exp(-t / 0.5) + 0.25
    body = np.exp(-t / (2.0 * ps)) * np.sin(2 * np.pi * f0 * t + ph + index * np.sin(2 * np.pi * f0 * t))
    tine = 0.22 * vel * np.exp(-t / 0.06) * np.sin(2 * np.pi * f0 * t + 2.5 * np.exp(-t / 0.03) * np.sin(2 * np.pi * 14 * f0 * t))
    out = (body + tine) * np.clip(t / 0.002, 0, 1)
    return damp(out, t, damp_at) * vel ** 1.2


INSTRUMENTS = {
    'piano': (None, 0),
    'rhodes': (rhodes, 0),
    'harp': (harp, 0),
    'musicbox': (bar([(1, 1, 1), (6.27, 0.30, 0.12), (17.55, 0.10, 0.04)], 1.2, attack=0.0008, hit=0.015, hit_smooth=6), 12),
    'marimba': (bar([(1, 1, 1), (3.93, 0.28, 0.18), (9.17, 0.08, 0.06)], 0.55, hit=0.05), 0),
    'vibraphone': (bar([(1, 1, 1), (4.0, 0.20, 0.35), (10.0, 0.05, 0.12)], 2.6, tremolo=(5.2, 0.35), hit=0.02), 0),
}


def pan_gains(p):
    a = (p + 1) * np.pi / 4
    return np.cos(a), np.sin(a)


def one_pole_lowpass(x, cutoff):
    n = len(x)
    m = 1 << int(np.ceil(np.log2(n * 2)))
    a = 1 - np.exp(-2 * np.pi * cutoff / SR)
    w = 2 * np.pi * np.fft.rfftfreq(m)
    return np.fft.irfft(np.fft.rfft(x, m) * (a / (1 - (1 - a) * np.exp(-1j * w))), m)[:n]


def shift(x, d):
    y = np.zeros_like(x); y[d:] = x[:len(x) - d]; return y


def convolve(x, h):
    m = 1 << int(np.ceil(np.log2(len(x) + len(h))))
    return np.fft.irfft(np.fft.rfft(x, m) * np.fft.rfft(h, m), m)[:len(x)]


def room_ir(rng, size=1.0):
    n = int(1.4 * SR)
    ti = np.arange(n) / SR
    spec = np.fft.rfft(rng.standard_normal(n))
    freqs = np.fft.rfftfreq(n, 1 / SR)
    out = np.zeros(n)
    for lo, hi, rt60 in [(0, 600, 1.2), (600, 2500, 0.9), (2500, 6000, 0.6), (6000, 16000, 0.35)]:
        out += np.fft.irfft(spec * ((freqs >= lo) & (freqs < hi)), n) * np.exp(-6.9 * ti / (rt60 * size))
    pre = int(0.012 * SR)
    out = np.concatenate([np.zeros(pre), out])[:n]
    return out / np.sqrt((out ** 2).sum())


LOWEST = 31  # G1: notes moved lower than this go back up by octaves (the 3DS speakers can't play them anyway)


def render(score, bpm, pedal=(), echo=2.0, feedback=0.42, echo_level=0.30, room=0.16, end=None, seed=7, transpose=0, instrument='piano'):
    # score: (time in sixteenths, note, velocity[, instrument]); pedal: times in sixteenths where the pedal is changed (notes
    # started before it are damped there); echo: delay in sixteenths; end: where the phrase is over, in sixteenths
    rng = np.random.default_rng(seed)
    S = 60 / bpm / 4
    L = np.zeros(N); R = np.zeros(N)
    for entry in score:
        # an entry may name its own instrument as a fourth item
        pos, note, vel = entry[:3]
        inst = entry[3] if len(entry) > 3 else instrument
        start = int(pos * S * SR)
        lift = next((p for p in sorted(pedal) if p > pos), None)
        voice, octave = INSTRUMENTS[inst]
        m = midi(note) + transpose + octave
        while m < LOWEST: m += 12
        sig = (voice or piano)(m, vel, N - start, rng, None if lift is None else (lift - pos) * S)
        gl, gr = pan_gains(np.clip((m - 64) / 30, -0.45, 0.45))
        L[start:] += gl * sig; R[start:] += gr * sig
    D = int(echo * S * SR)
    mono = (L + R) / 2
    eL = np.zeros(N); eR = np.zeros(N)
    rep = mono
    for k in range(1, 8):
        rep = one_pole_lowpass(shift(rep, D), 3800 if k == 1 else 3000) * (echo_level if k == 1 else feedback)
        (eR if k % 2 else eL)[:] += rep
    outL = L + eL + room * convolve(L + 0.5 * eL, room_ir(rng))
    outR = R + eR + room * convolve(R + 0.5 * eR, room_ir(rng))
    mix = np.stack([outL, outR], 1)
    mix /= np.abs(mix).max()
    mix = np.tanh(1.3 * mix) / np.tanh(1.3)
    # the tail: from shortly after the phrase, a slow exponential fade (the ring-out), and the last 60 ms to silence
    t0 = (end * S if end is not None else 1.2) + 0.45
    tau = (LENGTH - t0) / 3.2
    fade = np.where(t_all < t0, 1.0, np.exp(-(t_all - t0) / tau)) * np.clip((LENGTH - t_all) / 0.06, 0, 1)
    mix *= fade[:, None]
    return mix * 10 ** (-1 / 20) / np.abs(mix).max()


TUNES = {
    # the first version, its tail no longer cut short: Gmaj9 arpeggio up, E D C# over A, a rolled D(add9), a high A
    'bloom': dict(bpm=112, pedal=[4, 8], end=12, score=[
        (0, 'G2', .50), (0, 'D3', .42), (0, 'B4', .58), (1, 'D5', .60), (2, 'F#5', .63), (3, 'A5', .68),
        (4, 'A2', .50), (4, 'E3', .42), (4, 'E6', .86), (5, 'D6', .66), (6, 'C#6', .64),
        (8, 'D2', .62), (8, 'A2', .52), (8.15, 'F#3', .50), (8.30, 'D5', .60), (8.42, 'A5', .62), (8.54, 'E6', .64), (8.66, 'F#6', .84),
        (12, 'A6', .40)]),
    # "The-me-zer": short short long, three times, each a step higher, the last one landing on Cmaj9
    'themezer': dict(bpm=124, pedal=[4, 8], end=11, echo=3.0, score=[
        (0, 'C3', .48), (0, 'G3', .40), (0, 'E5', .62), (1, 'G5', .62), (2, 'C6', .80),
        (4, 'D3', .48), (4, 'A3', .40), (4, 'F5', .62), (5, 'A5', .62), (6, 'D6', .80),
        (8, 'C2', .60), (8, 'G2', .50), (8.12, 'E3', .48), (8.24, 'B4', .52), (8.36, 'D5', .54),
        (8, 'G5', .66), (9, 'B5', .66), (10, 'E6', .90)]),
    # a run down the D major pentatonic like a sweep of colour, landing on Dmaj9, then two bright notes up
    'cascade': dict(bpm=120, pedal=[5], end=11, echo=3.0, feedback=0.38, score=[
        (0.0, 'D7', .55), (0.5, 'B6', .56), (1.0, 'A6', .58), (1.5, 'F#6', .60), (2.0, 'E6', .62), (2.5, 'D6', .64),
        (3.0, 'B5', .66), (3.5, 'A5', .68), (4.0, 'F#5', .70),
        (5, 'D2', .62), (5, 'A2', .52), (5.12, 'F#3', .48), (5.24, 'C#4', .44), (5.36, 'E5', .58), (5.48, 'A5', .62), (5.6, 'D6', .74),
        (8, 'F#6', .58), (10, 'A6', .66)]),
    # a skipping music box tune up high in F: C A C F, E D Bb, landing on F with A on top
    'musicbox': dict(bpm=132, pedal=[4, 8], end=10, echo=1.5, feedback=0.40, score=[
        (0, 'F2', .48), (0, 'C3', .40), (0, 'C6', .70), (1.5, 'A5', .55), (2, 'C6', .64), (3, 'F6', .80),
        (4, 'Bb1', .48), (4, 'F2', .40), (4, 'D3', .38), (4.5, 'E6', .60), (5, 'D6', .62), (6, 'Bb5', .62), (7, 'C6', .66),
        (8, 'F2', .62), (8, 'C3', .52), (8.12, 'A3', .48), (8.24, 'A5', .56), (8.36, 'C6', .58), (8.48, 'F6', .66), (8.6, 'A6', .84)]),
    # slow, dreamy: Abmaj9 rolled, the melody falling C Bb G and rising to Eb over a rolled Eb(add9), longer echoes
    'dream': dict(bpm=96, pedal=[6], end=9, echo=3.0, feedback=0.48, echo_level=0.34, room=0.22, score=[
        (0, 'Ab1', .55), (0, 'Eb2', .46), (0.14, 'G3', .44), (0.28, 'C4', .44), (0.42, 'Eb4', .44), (0.56, 'Bb4', .50),
        (1, 'C6', .72), (2.5, 'Bb5', .62), (4, 'G5', .62), (5, 'Bb5', .64),
        (6, 'Eb2', .62), (6, 'Bb2', .52), (6.14, 'G3', .46), (6.28, 'F4', .46), (6.42, 'Bb4', .50), (6.56, 'G5', .56), (6.7, 'Eb6', .86)]),
}

out_dir = sys.argv[1]
os.makedirs(out_dir, exist_ok=True)
# with --lower, bloom and themezer one, two and three octaves down as well
jobs = [(name, spec, 0) for name, spec in TUNES.items()]
if '--lower' in sys.argv:
    jobs = [(name, TUNES[name], k) for name in ('bloom', 'themezer') for k in (1, 2, 3)]
# with --instruments, bloom and themezer on each instrument other than the piano (the music box an octave up)
if '--instruments' in sys.argv:
    jobs = [(name, TUNES[name], inst) for name in ('themezer', 'bloom') for inst in INSTRUMENTS if inst != 'piano']


def stem(name, var):
    return f'{name}-{var}' if isinstance(var, str) else name + (f'-down{var}' if var else '')


# with --only, the one tune of that name from any of the lists above (any tune, octave or instrument)
if '--only' in sys.argv:
    want = sys.argv[sys.argv.index('--only') + 1]
    every = [(name, spec, var) for name, spec in TUNES.items()
             for var in (0, 1, 2, 3, *(inst for inst in INSTRUMENTS if inst != 'piano'))]
    jobs = [job for job in every if stem(job[0], job[2]) == want]
    if not jobs:
        sys.exit(f'no tune called {want}')
for name, spec, var in jobs:
    if isinstance(var, str):
        mix = render(**spec, instrument=var)
    else:
        mix = render(**spec, transpose=-12 * var)
    path = os.path.join(out_dir, f'tune-{stem(name, var)}.wav')
    pcm = np.round(mix * 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())
    rms = 20 * np.log10(np.sqrt((mix ** 2).mean()))
    print(f'{path}: {LENGTH} s, rms {rms:.1f} dBFS')
