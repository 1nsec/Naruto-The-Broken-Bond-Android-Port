#!/usr/bin/env python3
"""The launcher's sounds, synthesized: original sounds in the spirit of a ninja
game's menus (a scroll unrolling, a wooden clack, a puff of smoke, and for Play
the theater's clappers, the kunai landing and a drum).
Nothing is sampled. Writes android/app/src/main/res/raw/ui_*.ogg (ffmpeg with
libvorbis), or .wav when ffmpeg is missing.

    python3 tools/android/make-launcher-sounds.py
"""
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np
from scipy import signal

RATE = 44100
OUT = Path(__file__).resolve().parents[2] / "android/app/src/main/res/raw"
rng = np.random.default_rng(1)


def t(seconds):
    return np.arange(int(RATE * seconds)) / RATE


def band(noise, low, high, order=4):
    sos = signal.butter(order, [low, high], btype="band", fs=RATE, output="sos")
    return signal.sosfilt(sos, noise)


def mode(freq, decay, seconds, phase=0.0):
    x = t(seconds)
    return np.sin(2 * np.pi * freq * x + phase) * np.exp(-x / decay)


def place(buffer, sound, at):
    start = int(at * RATE)
    end = min(len(buffer), start + len(sound))
    buffer[start:end] += sound[: end - start]


def finish(x, peak=0.7, fade=0.01):
    x = x - np.mean(x)
    n = int(fade * RATE)
    if n:
        x[-n:] *= np.linspace(1, 0, n)
    return x / (np.max(np.abs(x)) + 1e-9) * peak


def clack():
    """A wooden clack: two damped modes and a short click."""
    s = 0.09
    x = 0.8 * mode(820, 0.022, s) + 0.5 * mode(1960, 0.012, s, 0.7) + 0.25 * mode(3150, 0.006, s)
    click = band(rng.standard_normal(len(t(s))), 2500, 9000) * np.exp(-t(s) / 0.002)
    return finish(x + 0.6 * click, 0.75)


def scroll():
    """A scroll unrolling: paper crackles that swell and fade, then the roller's soft knock."""
    s = 0.55
    out = np.zeros(len(t(s)))
    base = band(rng.standard_normal(len(out)), 1200, 7000)
    # A bed of rustle under the crackles.
    env = np.sin(np.pi * np.clip(t(s) / 0.42, 0, 1)) ** 1.5
    out += 0.25 * base * env
    for _ in range(90):
        at = rng.beta(2.2, 2.6) * 0.42
        length = rng.uniform(0.004, 0.018)
        burst = band(rng.standard_normal(int(length * RATE) + 32), 1800, 8500)
        burst *= np.exp(-np.arange(len(burst)) / (length * RATE / 3))
        place(out, burst * rng.uniform(0.3, 1.0), at)
    knock = 0.9 * mode(190, 0.05, 0.12) + 0.4 * mode(410, 0.03, 0.12)
    place(out, knock, 0.42)
    return finish(out, 0.7, 0.03)


def poof():
    """A puff of smoke: a soft burst of air that darkens as it spreads, over a low thump."""
    s = 0.6
    x = t(s)
    noise = rng.standard_normal(len(x))
    attack = np.minimum(x / 0.005, 1)
    air = attack * (0.3 * band(noise, 2500, 9000) * np.exp(-x / 0.03)
                    + 0.75 * band(noise, 700, 2500) * np.exp(-x / 0.075)
                    + 0.9 * band(noise, 140, 700) * np.exp(-x / 0.14))
    # The cloud rolling out after the burst.
    billow = band(rng.standard_normal(len(x)), 250, 1200) * (x / 0.1) * np.exp(1 - x / 0.1)
    f = 58 + 120 * np.exp(-x / 0.022)
    thump = np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-x / 0.065) * attack
    return finish(air + 0.45 * billow + 0.8 * thump, 0.75, 0.06)


def tsuzumi(seconds=0.9):
    """A shoulder drum's 'pon': a ringing, hollow tone that bends down, and the slap of the hand."""
    x = t(seconds)
    f = 300 + 140 * np.exp(-x / 0.018) - 25 * (1 - np.exp(-x / 0.25))
    tone = np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-x / 0.3)
    tone += 0.45 * np.sin(2 * np.pi * np.cumsum(f * 1.52) / RATE) * np.exp(-x / 0.12)
    tone += 0.2 * np.sin(2 * np.pi * np.cumsum(f * 2.31) / RATE) * np.exp(-x / 0.06)
    slap = band(rng.standard_normal(len(x)), 900, 6000) * np.exp(-x / 0.006)
    return tone * np.minimum(x / 0.001, 1) + 0.7 * slap


def hyoshigi():
    """The theater's wooden clappers struck together: a hard, bright crack."""
    s = 0.12
    x = 0.9 * mode(1250, 0.03, s) + 0.55 * mode(2700, 0.018, s, 0.4) + 0.3 * mode(4100, 0.01, s, 1.1)
    click = band(rng.standard_normal(len(t(s))), 3000, 10000) * np.exp(-t(s) / 0.0015)
    return x * np.minimum(t(s) / 0.0005, 1) + 0.7 * click


def landing():
    """A kunai or a shuriken landing in wood: a short knock with a little steel in it."""
    s = 0.09
    x = 0.8 * mode(560, 0.018, s) + 0.35 * mode(1400, 0.01, s, 0.5) + 0.3 * mode(3300, 0.02, s, 0.9)
    x += 0.4 * band(rng.standard_normal(len(t(s))), 1500, 7000) * np.exp(-t(s) / 0.003)
    return x


def whoosh(seconds, low, high):
    """Air cut by something thrown: noise through a band that rises, swelling then gone."""
    noise = rng.standard_normal(len(t(seconds)))
    out = np.zeros_like(noise)
    pieces = 16
    for i in range(pieces):
        a, b = i * len(noise) // pieces, (i + 1) * len(noise) // pieces
        f = low + (high - low) * (i / pieces) ** 1.5
        out[a:b] = band(noise, f, f * 2.2, 2)[a:b]
    x = t(seconds) / seconds
    return out * np.sin(np.pi * x) ** 2


def play():
    """Play: the clappers twice, the throws cutting the air, each one landing (in time with the
    launcher's volley, EffectsView), and a shoulder drum's 'pon' with the last."""
    s = 1.5
    out = np.zeros(len(t(s)))
    place(out, 0.85 * hyoshigi(), 0.0)
    place(out, 1.0 * hyoshigi(), 0.17)
    place(out, 0.35 * whoosh(0.34, 500, 2600), 0.06)
    place(out, 0.25 * whoosh(0.3, 700, 3200), 0.2)
    for at, gain in ((0.38, 0.5), (0.43, 0.45), (0.49, 0.5), (0.55, 0.45)):
        place(out, gain * landing(), at)
    place(out, 0.55 * landing(), 0.62)
    place(out, 1.0 * tsuzumi(), 0.62)
    # A small hall around it all.
    ir_t = t(0.9)
    ir = rng.standard_normal(len(ir_t)) * np.exp(-ir_t / 0.16)
    ir[: int(0.015 * RATE)] = 0
    ir /= np.sqrt(np.sum(ir ** 2))
    out = out + 0.25 * signal.fftconvolve(out, ir)[: len(out)]
    return finish(out, 0.8, 0.08)


def write(name, x):
    OUT.mkdir(parents=True, exist_ok=True)
    pcm = (np.clip(x, -1, 1) * 32767).astype("<i2").tobytes()
    ffmpeg = shutil.which("ffmpeg")
    with tempfile.TemporaryDirectory() as folder:
        wav = Path(folder) / f"{name}.wav"
        with wave.open(str(wav), "wb") as f:
            f.setnchannels(1)
            f.setsampwidth(2)
            f.setframerate(RATE)
            f.writeframes(pcm)
        if ffmpeg:
            target = OUT / f"{name}.ogg"
            subprocess.run([ffmpeg, "-loglevel", "error", "-y", "-i", str(wav), "-c:a", "libvorbis", "-q:a", "5",
                            str(target)], check=True)
        else:
            target = OUT / f"{name}.wav"
            shutil.copyfile(wav, target)
    print(target.relative_to(OUT.parents[3]), target.stat().st_size, "bytes")


if __name__ == "__main__":
    write("ui_clack", clack())
    write("ui_poof", poof())
    write("ui_scroll", scroll())
    write("ui_play", play())
    sys.exit(0)
