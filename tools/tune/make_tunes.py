# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# Candidate HOME Menu banner tunes for Themezer: short phrases (piano, or strings and flute) with a stereo echo, synthesized from scratch.
# usage: make_tunes.py <out dir> [--lower | --instruments | --only <name>]  -> tune-<name>.wav for each candidate (44.1 kHz
# stereo 16-bit, 2.95 s: the HOME Menu plays about 3 s at most). The banner's tune is --only rise-strings.
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


def damp(out, t, damp_at, release=0.07):
    # the pedal lifts: the dampers stop the sound within about a tenth of a second
    return out if damp_at is None else out * np.where(t < damp_at, 1.0, np.exp(-(t - damp_at) / release))


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


def resonances(f, peaks, floor=0.12):
    # a spectral envelope: resonances (centre, half-width, gain) over a floor, each falling off like a real resonance
    return floor + sum(g / np.sqrt(1 + ((f - fc) / bw) ** 2) for fc, bw, g in peaks)


def sustained(spectrum, players=1, spread=6, vibrato=(5.5, 12), attack=0.06, release=0.15, scoop=0, noise=0.0, top=7000):
    # a held tone (bowed, blown, sung or synthesized): harmonics up to top weighted by spectrum(k, frequency, velocity).
    # Several players, each up to spread cents off, a few ms late and with a vibrato of their own, make a section,
    # which is what keeps it smooth. Vibrato comes in after the attack; scoop is how many cents below the note it
    # starts; noise is breath or bow. It swells a little as it is held and is let go at damp_at.
    def voice(m, vel, n_s, rng, damp_at=None):
        f0 = 440.0 * 2 ** ((m - 69) / 12)
        n = n_s if damp_at is None else min(n_s, int((damp_at + 6 * release) * SR))
        t = np.arange(n) / SR
        k = np.arange(1, max(2, int(top / f0) + 1))
        gains = spectrum(k, k * f0, vel)
        rate, depth = vibrato
        out = np.zeros(n)
        for _ in range(players):
            cents = (rng.uniform(-spread, spread) if players > 1 else 0) - scoop * np.exp(-t / 0.04) + depth * np.clip(
                (t - 0.15) / 0.3, 0, 1) * np.sin(2 * np.pi * rate * rng.uniform(0.93, 1.07) * t + rng.uniform(0, 2 * np.pi))
            phase = 2 * np.pi * np.cumsum(f0 * 2 ** (cents / 1200)) / SR
            d = int(rng.uniform(0, 0.02) * SR) if players > 1 else 0
            out[d:] += sum(g * np.sin(kk * phase[:n - d] + rng.uniform(0, 2 * np.pi)) for kk, g in zip(k, gains))
        out /= np.sqrt(players)
        if noise:
            hiss = rng.standard_normal(n)
            out += noise * (one_pole_lowpass(hiss, 5000) - one_pole_lowpass(hiss, 1200)) * (0.3 + np.exp(-t / 0.05))
        out *= (1 - np.exp(-t / attack)) * (0.85 + 0.15 * np.clip(t / 0.5, 0, 1))
        return np.pad(damp(out, t, damp_at, release), (0, n_s - n)) * vel ** 1.2
    return voice


VIOLIN_BODY = [(280, 50, 1.0), (460, 60, 0.8), (550, 70, 1.0), (1000, 400, 0.35), (2600, 900, 0.9), (4500, 1400, 0.3)]
CELLO_BODY = [(100, 20, 0.3), (175, 35, 0.5), (210, 40, 0.7), (420, 180, 0.6), (1100, 500, 0.7), (2300, 900, 0.3)]
AH = [(800, 40, 1.0), (1150, 45, 0.63), (2800, 60, 0.1), (3500, 65, 0.016)]  # the formants of a sung "ah" (alto)


def flute(m, vel, n_s, rng, damp_at=None):
    # nearly a sine, with soft harmonics that grow when it is blown harder and a second harmonic that speaks first (the
    # chiff), breath noise, and a vibrato that is as much loudness as pitch. The note is let go at damp_at.
    f0 = 440.0 * 2 ** ((m - 69) / 12)
    n = n_s if damp_at is None else min(n_s, int((damp_at + 0.6) * SR))
    t = np.arange(n) / SR
    wobble = np.clip((t - 0.1) / 0.25, 0, 1) * np.sin(2 * np.pi * 5.0 * rng.uniform(0.95, 1.05) * t + rng.uniform(0, 2 * np.pi))
    phase = 2 * np.pi * np.cumsum(f0 * 2 ** (8 * wobble / 1200)) / SR
    out = np.zeros(n)
    for k, amp in enumerate([1, 0.25 + 0.3 * vel + 0.5 * np.exp(-t / 0.02), 0.08 + 0.12 * vel, 0.04, 0.02], 1):
        out += amp * np.sin(k * phase + rng.uniform(0, 2 * np.pi))
    noise = rng.standard_normal(n)
    out += (one_pole_lowpass(noise, 6000) - one_pole_lowpass(noise, 1500)) * (0.05 + 0.3 * np.exp(-t / 0.03))
    out *= (1 - np.exp(-t / 0.03)) * (1 + 0.12 * wobble)
    return np.pad(damp(out, t, damp_at), (0, n_s - n)) * vel ** 1.2


def riser(m, vel, n_s, rng, damp_at=None):
    # not an instrument: a swell of airy noise, brightening as it grows, that stops at damp_at (the note only sets the pan)
    t = np.arange(n_s) / SR
    dur = damp_at or 0.6
    noise = rng.standard_normal(n_s)
    g = np.clip(t / dur, 0, 1)
    out = ((one_pole_lowpass(noise, 2000) - one_pole_lowpass(noise, 400)) * (1 - g)
           + (one_pole_lowpass(noise, 9000) - one_pole_lowpass(noise, 3000)) * g) * g ** 3
    return damp(out, t, dur) * vel


def kick(m, vel, n_s, rng, damp_at=None):
    # not an instrument: an electronic kick drum, a sine falling quickly from 320 to 90 Hz, driven so that its
    # harmonics carry it on the 3DS speakers (which can't play the sine itself), and a click (the note only sets the pan)
    n = min(n_s, int(0.5 * SR))
    t = np.arange(n) / SR
    body = np.tanh(4 * np.sin(2 * np.pi * np.cumsum(90 + 230 * np.exp(-t / 0.035)) / SR)) * np.exp(-t / 0.10)
    click = np.convolve(rng.standard_normal(n), [1, -1], 'same') * np.exp(-t / 0.002)
    return np.pad(body + 0.3 * click, (0, n_s - n)) * vel


def cymbal(m, vel, n_s, rng, damp_at=None):
    # not an instrument: a cymbal swelling up to damp_at and ringing on a little from there: six detuned square waves
    # (as in drum machines) and noise, only the highs kept (the note only sets the pan)
    t = np.arange(n_s) / SR
    dur = damp_at or 0.6
    x = rng.standard_normal(n_s)
    for f in (205.3, 304.4, 369.6, 522.7, 540.0, 800.0):
        x += 0.3 * sum(np.sin(2 * np.pi * k * f * t + rng.uniform(0, 2 * np.pi)) / k for k in range(1, int(16000 / f), 2))
    for _ in range(2):
        x -= one_pole_lowpass(x, 5000)
    return x * np.where(t < dur, (t / dur) ** 3, np.exp(-(t - dur) / 0.3)) * vel


INSTRUMENTS = {
    'piano': (None, 0),
    'rhodes': (rhodes, 0),
    'harp': (harp, 0),
    'musicbox': (bar([(1, 1, 1), (6.27, 0.30, 0.12), (17.55, 0.10, 0.04)], 1.2, attack=0.0008, hit=0.015, hit_smooth=6), 12),
    'marimba': (bar([(1, 1, 1), (3.93, 0.28, 0.18), (9.17, 0.08, 0.06)], 0.55, hit=0.05), 0),
    'vibraphone': (bar([(1, 1, 1), (4.0, 0.20, 0.35), (10.0, 0.05, 0.12)], 2.6, tremolo=(5.2, 0.35), hit=0.02), 0),
    # a section of three, the harmonics of the bowed string through the body, darker the softer it is played; the cello's
    # fundamental is thinned, the 3DS speakers can't play it and it would only drown the rest
    'violin': (sustained(lambda k, f, v: resonances(f, VIOLIN_BODY) / k ** 1.3 * np.exp(-f / (2500 + 2000 * v)),
                         players=3, attack=0.04, release=0.08, scoop=8, noise=0.01), 0),
    'cello': (sustained(lambda k, f, v: resonances(f, CELLO_BODY) / k / np.sqrt(1 + (220 / f) ** 4) * np.exp(-f / (1500 + 1500 * v)),
                        players=3, vibrato=(5.2, 10), attack=0.08, release=0.12, noise=0.01), 0),
    # three horns: nearly all harmonics, but the hand in the bell takes off everything above 1-1.5 kHz; a lip up to pitch
    'horn': (sustained(lambda k, f, v: 1 / k ** 0.4 / (1 + (f / (700 + 1200 * v)) ** 4), players=3, spread=3,
                       vibrato=(5.0, 3), attack=0.05, release=0.08, scoop=30), 0),
    # mostly odd harmonics (a cylindrical tube closed at one end), no vibrato
    'clarinet': (sustained(lambda k, f, v: np.where(k % 2, 1.0, 0.12) / k / (1 + (f / (1200 + 1500 * v)) ** 2),
                           vibrato=(5.0, 0), attack=0.03, release=0.08, noise=0.015), 0),
    # four singers on "ah"
    'choir': (sustained(lambda k, f, v: resonances(f, AH, floor=0.02) / k, players=4, vibrato=(5.3, 16), attack=0.07,
                        release=0.12, noise=0.02), 0),
    # synthesizers: a soft lead (a triangle wave, odd harmonics falling fast), a pad of five detuned saws, a round bass
    # (its fundamental thinned like the cello's)
    'synthlead': (sustained(lambda k, f, v: np.where(k % 2, 1.0, 0) / k ** 2, vibrato=(6.0, 14), attack=0.01, release=0.1), 0),
    'pad': (sustained(lambda k, f, v: 1 / k / (1 + (f / 1400) ** 2), players=5, spread=14, vibrato=(5, 0), attack=0.2,
                      release=0.4), 0),
    'synthbass': (sustained(lambda k, f, v: 1 / k ** 1.5 / np.sqrt(1 + (220 / f) ** 4) / (1 + (f / 1200) ** 4), vibrato=(5, 0), attack=0.01, release=0.1), 0),
    'flute': (flute, 0),
    'timpani': (bar([(1, 1, 1), (1.5, 0.6, 0.7), (1.98, 0.4, 0.5), (2.44, 0.25, 0.4)], 0.9, attack=0.003, hit=0.25, hit_smooth=48), 0),
    'riser': (riser, 0),
    'kick': (kick, 0),
    'cymbal': (cymbal, 0),
}
# the instruments the other tunes can be played on (--instruments)
ALTERNATIVES = [inst for inst in INSTRUMENTS if inst not in ('piano', 'timpani', 'riser', 'kick', 'cymbal')]


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
    # score: (time in sixteenths, note, velocity[, instrument[, length in sixteenths]]); pedal: times in sixteenths where the
    # pedal is changed (notes started before it, and with no length, are damped there); echo: delay in sixteenths; end:
    # where the phrase is over, in sixteenths
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
        damp_at = entry[4] * S if len(entry) > 4 else None if lift is None else (lift - pos) * S
        sig = (voice or piano)(m, vel, N - start, rng, damp_at)
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
    # The "rise" tunes: the piano motif of a phone recording (Eb Ab Bb C, up to the third of Ab major, a crotchet each at
    # 158 bpm) at two thirds more speed. It goes up over Eb in the bass and lands on Ab with the C; while the C is held
    # something answers with a run or a rolled chord up to Ab, and a music box twinkles the motif once more at the top.
    # strings: violins over cellos, a noise swell into the landing and a timpani on it, the flute answering
    'rise-strings': dict(bpm=132, end=14, echo=3.0, feedback=0.3, echo_level=0.16, room=0.28, score=[
        (0, 'Eb5', .60, 'violin', 2), (2, 'Ab5', .64, 'violin', 2), (4, 'Bb5', .68, 'violin', 2), (6, 'C6', .80, 'violin', 10),
        (0, 'Bb4', .34, 'violin', 6.2), (6, 'Eb5', .36, 'violin', 10), (0, 'Eb3', .48, 'cello', 6.2), (6, 'Ab2', .56, 'cello', 10),
        (0, 'E4', .30, 'riser', 6), (6, 'Ab2', .55, 'timpani'),
        (8, 'Eb6', .48, 'flute', 1.1), (9, 'F6', .52, 'flute', 1.1), (10, 'G6', .56, 'flute', 1.1), (11, 'Ab6', .66, 'flute', 5),
        (12, 'Eb5', .36, 'musicbox'), (12.5, 'Ab5', .38, 'musicbox'), (13, 'Bb5', .40, 'musicbox'), (13.5, 'C6', .46, 'musicbox')]),
    # majestic: in place of the one held inner part, two more violins hold chords under the tune, an open Eb fifth under
    # its first three notes (no G, which would rub against its Ab) and Ab Eb under the C; a kick on the first note and a
    # big one on the landing, and a cymbal swelling into it in place of the airy swell
    'rise-strings-majestic': dict(bpm=132, end=14, echo=3.0, feedback=0.3, echo_level=0.16, room=0.28, score=[
        (0, 'Eb5', .60, 'violin', 2), (2, 'Ab5', .64, 'violin', 2), (4, 'Bb5', .68, 'violin', 2), (6, 'C6', .80, 'violin', 10),
        (0, 'Bb4', .33, 'violin', 6.2), (6, 'Eb5', .38, 'violin', 10), (0, 'Eb4', .31, 'violin', 6.2), (6, 'Ab4', .36, 'violin', 10),
        (0, 'Eb3', .48, 'cello', 6.2), (6, 'Ab2', .56, 'cello', 10), (6, 'Ab2', .55, 'timpani'),
        (0, 'E4', .60, 'cymbal', 6), (0, 'E4', .70, 'kick'), (6, 'E4', 1.0, 'kick'),
        (8, 'Eb6', .48, 'flute', 1.1), (9, 'F6', .52, 'flute', 1.1), (10, 'G6', .56, 'flute', 1.1), (11, 'Ab6', .66, 'flute', 5),
        (12, 'Eb5', .36, 'musicbox'), (12.5, 'Ab5', .38, 'musicbox'), (13, 'Bb5', .40, 'musicbox'), (13.5, 'C6', .46, 'musicbox')]),
    # rise-strings with one thing changed. turn: the flute answers with the motif itself (Eb Ab Bb) and turns home to Ab
    'rise-strings-turn': dict(bpm=132, end=14, echo=3.0, feedback=0.3, echo_level=0.16, room=0.28, score=[
        (0, 'Eb5', .60, 'violin', 2), (2, 'Ab5', .64, 'violin', 2), (4, 'Bb5', .68, 'violin', 2), (6, 'C6', .80, 'violin', 10),
        (0, 'Bb4', .34, 'violin', 6.2), (6, 'Eb5', .36, 'violin', 10), (0, 'Eb3', .48, 'cello', 6.2), (6, 'Ab2', .56, 'cello', 10),
        (0, 'E4', .30, 'riser', 6), (6, 'Ab2', .55, 'timpani'),
        (8, 'Eb6', .48, 'flute', 1.1), (9, 'Ab6', .54, 'flute', 1.1), (10, 'Bb6', .58, 'flute', 1.1), (11, 'Ab6', .66, 'flute', 5),
        (12, 'Eb5', .36, 'musicbox'), (12.5, 'Ab5', .38, 'musicbox'), (13, 'Bb5', .40, 'musicbox'), (13.5, 'C6', .46, 'musicbox')]),
    # high: the violins climb one step further, from the C up to Eb, the inner violins taking over the C
    'rise-strings-high': dict(bpm=132, end=14, echo=3.0, feedback=0.3, echo_level=0.16, room=0.28, score=[
        (0, 'Eb5', .60, 'violin', 2), (2, 'Ab5', .64, 'violin', 2), (4, 'Bb5', .68, 'violin', 2), (6, 'C6', .80, 'violin', 6), (12, 'Eb6', .82, 'violin', 4),
        (0, 'Bb4', .34, 'violin', 6.2), (6, 'Eb5', .36, 'violin', 6), (12, 'C6', .34, 'violin', 4),
        (0, 'Eb3', .48, 'cello', 6.2), (6, 'Ab2', .56, 'cello', 10), (0, 'E4', .30, 'riser', 6), (6, 'Ab2', .55, 'timpani'),
        (8, 'Eb6', .48, 'flute', 1.1), (9, 'F6', .52, 'flute', 1.1), (10, 'G6', .56, 'flute', 1.1), (11, 'Ab6', .66, 'flute', 5),
        (12, 'Eb5', .36, 'musicbox'), (12.5, 'Ab5', .38, 'musicbox'), (13, 'Bb5', .40, 'musicbox'), (13.5, 'C6', .46, 'musicbox')]),
    # sixths: the inner violins move with the tune a sixth below (G C D Eb) instead of holding Bb
    'rise-strings-sixths': dict(bpm=132, end=14, echo=3.0, feedback=0.3, echo_level=0.16, room=0.28, score=[
        (0, 'Eb5', .60, 'violin', 2), (2, 'Ab5', .64, 'violin', 2), (4, 'Bb5', .68, 'violin', 2), (6, 'C6', .80, 'violin', 10),
        (0, 'G4', .36, 'violin', 2), (2, 'C5', .38, 'violin', 2), (4, 'D5', .40, 'violin', 2), (6, 'Eb5', .40, 'violin', 10),
        (0, 'Eb3', .48, 'cello', 6.2), (6, 'Ab2', .56, 'cello', 10), (0, 'E4', .30, 'riser', 6), (6, 'Ab2', .55, 'timpani'),
        (8, 'Eb6', .48, 'flute', 1.1), (9, 'F6', .52, 'flute', 1.1), (10, 'G6', .56, 'flute', 1.1), (11, 'Ab6', .66, 'flute', 5),
        (12, 'Eb5', .36, 'musicbox'), (12.5, 'Ab5', .38, 'musicbox'), (13, 'Bb5', .40, 'musicbox'), (13.5, 'C6', .46, 'musicbox')]),
    # pulse: short inner-violin Bbs on the off-beats between the notes of the tune, instead of one held Bb
    # (seed 20: the take where every note of the tune starts as strongly as in rise-strings)
    'rise-strings-pulse': dict(bpm=132, end=14, echo=3.0, feedback=0.3, echo_level=0.16, room=0.28, seed=20, score=[
        (0, 'Eb5', .60, 'violin', 2), (2, 'Ab5', .64, 'violin', 2), (4, 'Bb5', .68, 'violin', 2), (6, 'C6', .80, 'violin', 10),
        (1, 'Bb4', .40, 'violin', 0.8), (3, 'Bb4', .42, 'violin', 0.8), (5, 'Bb4', .44, 'violin', 0.8), (6, 'Eb5', .36, 'violin', 10),
        (0, 'Eb3', .48, 'cello', 6.2), (6, 'Ab2', .56, 'cello', 10), (0, 'E4', .30, 'riser', 6), (6, 'Ab2', .55, 'timpani'),
        (8, 'Eb6', .48, 'flute', 1.1), (9, 'F6', .52, 'flute', 1.1), (10, 'G6', .56, 'flute', 1.1), (11, 'Ab6', .66, 'flute', 5),
        (12, 'Eb5', .36, 'musicbox'), (12.5, 'Ab5', .38, 'musicbox'), (13, 'Bb5', .40, 'musicbox'), (13.5, 'C6', .46, 'musicbox')]),
    # bb: all of it a tone higher, in Bb major (F Bb C D), a little brighter (seed 20, as for pulse)
    'rise-strings-bb': dict(bpm=132, end=14, echo=3.0, feedback=0.3, echo_level=0.16, room=0.28, seed=20, score=[
        (0, 'F5', .60, 'violin', 2), (2, 'Bb5', .64, 'violin', 2), (4, 'C6', .68, 'violin', 2), (6, 'D6', .80, 'violin', 10),
        (0, 'C5', .34, 'violin', 6.2), (6, 'F5', .36, 'violin', 10), (0, 'F3', .48, 'cello', 6.2), (6, 'Bb2', .56, 'cello', 10),
        (0, 'E4', .30, 'riser', 6), (6, 'Bb2', .55, 'timpani'),
        (8, 'F6', .48, 'flute', 1.1), (9, 'G6', .52, 'flute', 1.1), (10, 'A6', .56, 'flute', 1.1), (11, 'Bb6', .66, 'flute', 5),
        (12, 'F5', .36, 'musicbox'), (12.5, 'Bb5', .38, 'musicbox'), (13, 'C6', .40, 'musicbox'), (13.5, 'D6', .46, 'musicbox')]),
    # strings, cadence: a chord under every note, the cellos walking I IV V I (Ab Db Eb Ab), the inner violins C Db Eb
    'rise-strings-cadence': dict(bpm=132, end=14, echo=3.0, feedback=0.3, echo_level=0.16, room=0.28, score=[
        (0, 'Eb5', .60, 'violin', 2), (2, 'Ab5', .64, 'violin', 2), (4, 'Bb5', .68, 'violin', 2), (6, 'C6', .80, 'violin', 10),
        (0, 'C5', .32, 'violin', 2), (2, 'Db5', .32, 'violin', 2), (4, 'Eb5', .30, 'violin', 12),
        (0, 'Ab2', .50, 'cello', 2), (2, 'Db3', .48, 'cello', 2), (4, 'Eb3', .52, 'cello', 2), (6, 'Ab2', .56, 'cello', 10),
        (0, 'E4', .30, 'riser', 6), (4, 'Eb3', .38, 'timpani'), (6, 'Ab2', .55, 'timpani'),
        (8, 'Eb6', .48, 'flute', 1.1), (9, 'F6', .52, 'flute', 1.1), (10, 'G6', .56, 'flute', 1.1), (11, 'Ab6', .66, 'flute', 5),
        (12, 'Eb5', .36, 'musicbox'), (12.5, 'Ab5', .38, 'musicbox'), (13, 'Bb5', .40, 'musicbox'), (13.5, 'C6', .46, 'musicbox')]),
    # strings, pizzicato: a little faster, the violins shorter, the cellos plucking a bouncing line high enough for the 3DS
    # speakers under them (the harp's
    # plucked string, stopped short) and on through the held C; no swell or timpani
    'rise-strings-pizz': dict(bpm=140, end=14, echo=3.0, feedback=0.3, echo_level=0.16, room=0.26, score=[
        (0, 'Eb5', .60, 'violin', 1.6), (2, 'Ab5', .64, 'violin', 1.6), (4, 'Bb5', .68, 'violin', 1.6), (6, 'C6', .80, 'violin', 9),
        (6, 'Eb5', .34, 'violin', 9), (6, 'Ab2', .40, 'cello', 9),
        (0, 'Eb4', .56, 'harp', 0.8), (1, 'Bb3', .42, 'harp', 0.8), (2, 'Eb4', .52, 'harp', 0.8), (3, 'Bb3', .42, 'harp', 0.8),
        (4, 'Eb4', .52, 'harp', 0.8), (5, 'Bb3', .46, 'harp', 0.8), (6, 'Ab3', .62, 'harp', 1), (6, 'Eb4', .50, 'harp', 1),
        (8, 'Ab4', .46, 'harp', 0.8), (9, 'Eb4', .42, 'harp', 0.8), (10, 'Ab3', .50, 'harp', 0.8), (11, 'Eb4', .42, 'harp', 0.8), (12, 'Ab3', .50, 'harp', 1.5),
        (8, 'Eb6', .48, 'flute', 1.1), (9, 'F6', .52, 'flute', 1.1), (10, 'G6', .56, 'flute', 1.1), (11, 'Ab6', .66, 'flute', 4),
        (12, 'Eb5', .36, 'musicbox'), (12.5, 'Ab5', .38, 'musicbox'), (13, 'Bb5', .40, 'musicbox'), (13.5, 'C6', .46, 'musicbox')]),
    # strings, grand: slower and bigger, the cellos doubling the tune an octave down, a timpani roll into the landing and
    # horns joining the chord there, a larger room
    'rise-strings-grand': dict(bpm=116, end=14, echo=3.0, feedback=0.3, echo_level=0.14, room=0.34, score=[
        (0, 'Eb5', .62, 'violin', 2), (2, 'Ab5', .66, 'violin', 2), (4, 'Bb5', .70, 'violin', 2), (6, 'C6', .84, 'violin', 10),
        (0, 'Eb4', .40, 'cello', 2), (2, 'Ab4', .42, 'cello', 2), (4, 'Bb4', .44, 'cello', 2), (6, 'C5', .46, 'cello', 10),
        (0, 'Bb4', .32, 'violin', 6.2), (6, 'Eb5', .36, 'violin', 10), (0, 'Eb3', .48, 'cello', 6.2), (6, 'Ab2', .58, 'cello', 10),
        (6, 'Ab3', .38, 'horn', 10), (6, 'C4', .36, 'horn', 10), (6, 'Eb4', .36, 'horn', 10), (0, 'E4', .36, 'riser', 6),
        (0, 'Eb3', .40, 'timpani'), (4, 'Eb3', .26, 'timpani'), (4.5, 'Eb3', .30, 'timpani'), (5, 'Eb3', .34, 'timpani'),
        (5.25, 'Eb3', .38, 'timpani'), (5.5, 'Eb3', .42, 'timpani'), (5.75, 'Eb3', .46, 'timpani'), (6, 'Ab2', .70, 'timpani'),
        (8, 'Eb6', .48, 'flute', 1.1), (9, 'F6', .52, 'flute', 1.1), (10, 'G6', .56, 'flute', 1.1), (11, 'Ab6', .66, 'flute', 5),
        (12, 'Eb5', .36, 'musicbox'), (12.5, 'Ab5', .38, 'musicbox'), (13, 'Bb5', .40, 'musicbox'), (13.5, 'C6', .46, 'musicbox')]),
    # strings, gentle: slower and softer, legato, no swell or timpani; the flute answers with three slow notes and the
    # music box twinkles more slowly
    'rise-strings-gentle': dict(bpm=120, end=14, echo=3.0, feedback=0.32, echo_level=0.16, room=0.30, score=[
        (0, 'Eb5', .48, 'violin', 2.2), (2, 'Ab5', .52, 'violin', 2.2), (4, 'Bb5', .56, 'violin', 2.2), (6, 'C6', .66, 'violin', 12),
        (0, 'Bb4', .30, 'violin', 6.2), (6, 'Eb5', .32, 'violin', 12), (0, 'Eb3', .40, 'cello', 6.2), (6, 'Ab2', .46, 'cello', 12),
        (8, 'Eb6', .40, 'flute', 1.9), (10, 'F6', .44, 'flute', 1.9), (12, 'Ab6', .50, 'flute', 5),
        (12, 'Eb5', .30, 'musicbox'), (12.75, 'Ab5', .32, 'musicbox'), (13.5, 'Bb5', .34, 'musicbox'), (14.25, 'C6', .38, 'musicbox')]),
    # brass: a horn fanfare at the recording's own pitch, more horns under it, timpani, the clarinet answering
    'rise-brass': dict(bpm=132, end=14, echo=3.0, feedback=0.3, echo_level=0.18, room=0.26, score=[
        (0, 'Eb4', .62, 'horn', 1.8), (2, 'Ab4', .66, 'horn', 1.8), (4, 'Bb4', .70, 'horn', 1.8), (6, 'C5', .82, 'horn', 10),
        (0, 'Bb3', .40, 'horn', 5.8), (6, 'Eb4', .38, 'horn', 10), (0, 'Eb3', .44, 'cello', 6.2), (6, 'Ab2', .52, 'cello', 10),
        (0, 'Eb3', .40, 'timpani'), (6, 'Ab2', .62, 'timpani'),
        (8, 'Eb5', .50, 'clarinet', 1.1), (9, 'F5', .54, 'clarinet', 1.1), (10, 'G5', .58, 'clarinet', 1.1), (11, 'Ab5', .66, 'clarinet', 5),
        (12, 'Eb5', .34, 'musicbox'), (12.5, 'Ab5', .36, 'musicbox'), (13, 'Bb5', .38, 'musicbox'), (13.5, 'C6', .44, 'musicbox')]),
    # choir: sopranos sing it over an open Eb fifth, landing on Ab with the B flat of the recording's last chord in it;
    # a harp rolls up the chord
    'rise-choir': dict(bpm=132, end=14, echo=3.0, feedback=0.32, echo_level=0.16, room=0.30, score=[
        (0, 'Eb5', .68, 'choir', 2), (2, 'Ab5', .70, 'choir', 2), (4, 'Bb5', .72, 'choir', 2), (6, 'C6', .78, 'choir', 10),
        (0, 'Eb4', .44, 'choir', 6.2), (0, 'Bb4', .44, 'choir', 6.2), (0, 'Eb3', .48, 'choir', 6.2),
        (6, 'Ab2', .54, 'choir', 10), (6, 'Ab4', .44, 'choir', 10), (6, 'Bb4', .36, 'choir', 10), (6, 'Eb5', .46, 'choir', 10),
        (8, 'Eb5', .40, 'harp'), (8.5, 'Ab5', .42, 'harp'), (9, 'C6', .44, 'harp'), (9.5, 'Eb6', .46, 'harp'), (10, 'Ab6', .50, 'harp'),
        (12, 'Eb5', .34, 'musicbox'), (12.5, 'Ab5', .36, 'musicbox'), (13, 'Bb5', .38, 'musicbox'), (13.5, 'C6', .44, 'musicbox')]),
    # synth: a soft triangle lead over a detuned pad and a bass pulsing in octaves, a noise swell, the vibraphone
    # answering, a longer echo
    'rise-synth': dict(bpm=132, end=14, echo=3.0, feedback=0.4, echo_level=0.26, room=0.18, score=[
        (0, 'Eb5', .62, 'synthlead', 1.6), (2, 'Ab5', .66, 'synthlead', 1.6), (4, 'Bb5', .70, 'synthlead', 1.6), (6, 'C6', .80, 'synthlead', 8),
        (0, 'Eb4', .44, 'pad', 6.2), (0, 'Bb4', .44, 'pad', 6.2), (6, 'Ab4', .46, 'pad', 10), (6, 'C5', .46, 'pad', 10), (6, 'Eb5', .46, 'pad', 10),
        (0, 'Eb2', .80, 'synthbass', 0.8), (1, 'Eb3', .62, 'synthbass', 0.8), (2, 'Eb2', .74, 'synthbass', 0.8), (3, 'Eb3', .62, 'synthbass', 0.8),
        (4, 'Eb2', .74, 'synthbass', 0.8), (5, 'Eb3', .62, 'synthbass', 0.8), (6, 'Ab2', .85, 'synthbass', 8), (0, 'E4', .32, 'riser', 6),
        (8, 'Eb6', .50, 'vibraphone'), (9, 'F6', .52, 'vibraphone'), (10, 'G6', .55, 'vibraphone'), (11, 'Ab6', .62, 'vibraphone'),
        (12, 'Eb5', .34, 'musicbox'), (12.5, 'Ab5', .36, 'musicbox'), (13, 'Bb5', .38, 'musicbox'), (13.5, 'C6', .44, 'musicbox')]),
}

out_dir = sys.argv[1]
os.makedirs(out_dir, exist_ok=True)
# with --lower, bloom and themezer one, two and three octaves down as well
jobs = [(name, spec, 0) for name, spec in TUNES.items()]
if '--lower' in sys.argv:
    jobs = [(name, TUNES[name], k) for name in ('bloom', 'themezer') for k in (1, 2, 3)]
# with --instruments, bloom and themezer on each instrument other than the piano (the music box an octave up)
if '--instruments' in sys.argv:
    jobs = [(name, TUNES[name], inst) for name in ('themezer', 'bloom') for inst in ALTERNATIVES]


def stem(name, var):
    return f'{name}-{var}' if isinstance(var, str) else name + (f'-down{var}' if var else '')


# with --only, the one tune of that name from any of the lists above (any tune, octave or instrument)
if '--only' in sys.argv:
    want = sys.argv[sys.argv.index('--only') + 1]
    every = [(name, spec, var) for name, spec in TUNES.items()
             for var in (0, 1, 2, 3, *ALTERNATIVES)]
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
