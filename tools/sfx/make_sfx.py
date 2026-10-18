# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# The app's UI sounds, synthesized from scratch to sit with its music ("Aether", tools/music): soft electric piano
# notes on the music's G minor pentatonic, mostly between G4 and G5, as warm as the music is (it has little above
# 1 kHz), with no noise and nothing glassy.
# usage: make_sfx.py <out dir>  -> <name>.pcm (raw 16-bit mono at the DSP's own 32728 Hz, as the app plays them) and a
# <name>.wav of each to listen to.
import os, sys, wave
import numpy as np

SR = 32728


def note(name):
    names = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
    m = 12 * (int(name[-1]) + 1) + names[name[0]] + name[1:-1].count('#')
    return 440.0 * 2 ** ((m - 69) / 12)


def times(length):
    return np.arange(int(length * SR)) / SR


def keys(name, tau, length, bright=1.0):
    # an electric piano note: a sine with a 1:1 FM bite at the attack that softens into the plain tone, and a little
    # second harmonic that dies first
    f = note(name)
    t = times(length)
    ph = 2 * np.pi * f * t
    index = bright * 1.1 * np.exp(-t / 0.035) + 0.12
    out = np.sin(ph + index * np.sin(ph)) + 0.18 * np.exp(-t / (tau * 0.4)) * np.sin(2 * ph)
    return out * np.minimum(t / 0.002, 1) * np.exp(-t / tau)


def muted(name, tau, length):
    # a dull, buzzy note for "no": a few odd harmonics, low-passed by their own fast decay
    f = note(name)
    t = times(length)
    out = sum(np.exp(-t * k / (tau * 3)) * np.sin(2 * np.pi * k * f * t) / k for k in (1, 3, 5))
    return out * np.minimum(t / 0.002, 1) * np.exp(-t / tau)


def lowpass(x, cutoff):
    a = 1 - np.exp(-2 * np.pi * cutoff / SR)
    y = np.zeros_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc += a * (x[i] - acc)
        y[i] = acc
    return y


def db(x):
    return 10 ** (x / 20)


def mix(peak, *notes, echo=None):
    # notes: (start in seconds, gain in dB, sound); the sum, with an optional duller echo (delay, gain in dB), is
    # normalized to peak (dBFS) and ends where it has faded below -66 dBFS, with a short fade to silence
    length = max(start + len(sound) / SR for start, _, sound in notes)
    if echo:
        length += echo[0]
    out = np.zeros(int(length * SR) + 1)
    for start, gain, sound in notes:
        at = int(start * SR)
        out[at:at + len(sound)] += db(gain) * sound
    if echo:
        delay = int(echo[0] * SR)
        out[delay:] += db(echo[1]) * lowpass(out[:len(out) - delay], 1800)
    out = lowpass(out, 4000)
    out *= db(peak) / np.abs(out).max()
    loud = np.nonzero(np.abs(out) > db(-66))[0]
    out = out[:min(len(out), loud[-1] + 1 + int(0.004 * SR))]
    fade = int(0.004 * SR)
    out[-fade:] *= 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, fade))
    return out


SOUNDS = {
    # a cursor step: one short, soft note
    'cursor': lambda: mix(-18, (0, 0, keys('G5', 0.022, 0.12, bright=0.7))),
    # a page or a preview further: two quick notes up
    'page': lambda: mix(-16, (0, -2, keys('D5', 0.03, 0.15)), (0.04, 0, keys('G5', 0.045, 0.2))),
    # another tab: a fifth, the low note first
    'tab': lambda: mix(-14, (0, -4, keys('G4', 0.04, 0.2)), (0.035, 0, keys('D5', 0.08, 0.35))),
    # something opens: three notes rising, quickly
    'open': lambda: mix(-15, (0, -3, keys('A#4', 0.04, 0.2)), (0.03, -2, keys('D5', 0.05, 0.2)),
                        (0.06, 0, keys('F5', 0.08, 0.3))),
    # and closes: the same, falling
    'close': lambda: mix(-15, (0, 0, keys('F5', 0.04, 0.2)), (0.03, -2, keys('D5', 0.05, 0.2)),
                         (0.06, -3, keys('A#4', 0.08, 0.3))),
    # yes: up a fourth to the home note, ringing a little
    'confirm': lambda: mix(-13, (0, -3, keys('D5', 0.05, 0.25)), (0.045, 0, keys('G5', 0.12, 0.5)),
                           (0.045, -12, keys('G4', 0.12, 0.5))),
    # back, or something taken away: two notes down
    'back': lambda: mix(-16, (0, 0, keys('F5', 0.03, 0.15)), (0.04, -2, keys('D5', 0.045, 0.2))),
    # done: up the chord to the home note, an octave over it, with an echo
    'done': lambda: mix(-10, (0, -2, keys('G4', 0.2, 0.8)), (0.08, -2, keys('A#4', 0.2, 0.8)),
                        (0.16, -1, keys('D5', 0.22, 0.8)), (0.25, 0, keys('G5', 0.3, 1.0)),
                        echo=(0.15, -14)),
    # no: two dull notes down a tritone, out of the key on purpose
    'error': lambda: mix(-14, (0, 0, muted('C#5', 0.05, 0.2)), (0.1, 0, muted('G4', 0.07, 0.25))),
}

out_dir = sys.argv[1]
os.makedirs(out_dir, exist_ok=True)
for name, make in SOUNDS.items():
    pcm = np.round(make() * 32767).astype('<i2')
    with open(os.path.join(out_dir, f'{name}.pcm'), 'wb') as f:
        f.write(pcm.tobytes())
    with wave.open(os.path.join(out_dir, f'{name}.wav'), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())
    print(f'{name}: {len(pcm) / SR * 1000:.0f} ms')
