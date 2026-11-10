#!/usr/bin/env python3
"""The launcher's sounds, synthesized: original sounds in the spirit of a ninja
game's menus (a scroll unrolling, a wooden clack, a paper flick, a kunai).
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


def flick():
    """A paper flick: a short crisp burst of high noise and a light tick."""
    s = 0.13
    env = np.minimum(t(s) / 0.004, 1) * np.exp(-t(s) / 0.03)
    x = band(rng.standard_normal(len(t(s))), 2200, 9500) * env
    x += 0.35 * mode(2400, 0.01, s)
    return finish(x, 0.55)


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


def kunai():
    """Play: a whoosh that ends in a kunai's metallic ring."""
    s = 0.85
    out = np.zeros(len(t(s)))
    w = 0.26
    noise = rng.standard_normal(len(t(w)))
    # The whoosh: noise through a band that rises, swelling to the ring.
    sweep = np.zeros_like(noise)
    pieces = 24
    for i in range(pieces):
        a, b = i * len(noise) // pieces, (i + 1) * len(noise) // pieces
        low = 300 + 2200 * (i / pieces) ** 1.6
        sweep[a:b] = band(noise, low, low * 2.6, 2)[a:b]
    sweep *= (t(w) / w) ** 2.2
    place(out, 0.8 * sweep, 0.0)
    ring = np.zeros(len(t(s - w)))
    x = t(s - w)
    vibrato = 1 + 0.002 * np.sin(2 * np.pi * 7 * x)
    for freq, amp, decay in [(2450, 1.0, 0.42), (3770, 0.6, 0.3), (5150, 0.45, 0.22),
                             (6900, 0.3, 0.16), (9100, 0.2, 0.1)]:
        ring += amp * np.sin(2 * np.pi * freq * x * vibrato) * np.exp(-x / decay)
    ring *= np.minimum(x / 0.002, 1)
    place(out, 0.55 * ring, w - 0.01)
    return finish(out, 0.75, 0.05)


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
    write("ui_flick", flick())
    write("ui_scroll", scroll())
    write("ui_kunai", kunai())
    sys.exit(0)
