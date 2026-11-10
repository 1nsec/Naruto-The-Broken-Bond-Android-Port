#!/usr/bin/env python3
"""The launcher's sounds, synthesized: original sounds in the spirit of a ninja
game's menus (a scroll unrolling, a wooden clack, a puff of smoke, and for Play
a drummer's call and a drum, after the theater).
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


def formants(source, tracks, step=32):
    """A cascade of resonators (a voice's formants) whose frequencies and widths change over time."""
    out = source.copy()
    for freq, width in tracks:
        y = np.zeros_like(out)
        y1 = y2 = 0.0
        for start in range(0, len(out), step):
            f, w = freq[start], width[start]
            c = -np.exp(-2 * np.pi * w / RATE)
            b = 2 * np.exp(-np.pi * w / RATE) * np.cos(2 * np.pi * f / RATE)
            a = 1 - b - c
            for n in range(start, min(start + step, len(out))):
                v = a * out[n] + b * y1 + c * y2
                y2, y1 = y1, v
                y[n] = v
        out = y
    return out


def tsuzumi(seconds=0.9):
    """A shoulder drum's 'pon': a ringing, hollow tone that bends down, and the slap of the hand."""
    x = t(seconds)
    f = 300 + 140 * np.exp(-x / 0.018) - 25 * (1 - np.exp(-x / 0.25))
    tone = np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-x / 0.3)
    tone += 0.45 * np.sin(2 * np.pi * np.cumsum(f * 1.52) / RATE) * np.exp(-x / 0.12)
    tone += 0.2 * np.sin(2 * np.pi * np.cumsum(f * 2.31) / RATE) * np.exp(-x / 0.06)
    slap = band(rng.standard_normal(len(x)), 900, 6000) * np.exp(-x / 0.006)
    return tone * np.minimum(x / 0.001, 1) + 0.7 * slap


def play():
    """Play: a drummer's call, 'yo-o', and the drum's 'pon', as in the theater the games take after."""
    voice_length = 0.62
    x = t(voice_length)
    # The pitch: a step up out of the 'i', a held call with a wide vibrato, a fall at the end.
    pitch = 250 + 150 / (1 + np.exp(-(x - 0.07) / 0.012))
    pitch *= 1 + 0.035 * np.clip((x - 0.15) / 0.2, 0, 1) * np.sin(2 * np.pi * 6.2 * x)
    pitch *= 1 - 0.22 * np.clip((x - 0.46) / 0.16, 0, 1) ** 2
    pitch *= 1 + 0.004 * signal.lfilter([0.02], [1, -0.98], rng.standard_normal(len(x))) * 30
    phase = np.cumsum(pitch) / RATE
    p = phase % 1.0
    # A glottal pulse (open 60% of the period), and its derivative for the lips.
    pulse = np.where(p < 0.4, 0.5 * (1 - np.cos(np.pi * p / 0.4)),
                     np.where(p < 0.6, np.cos(0.5 * np.pi * (p - 0.4) / 0.2), 0.0))
    source = np.diff(pulse, prepend=0.0)
    source += 0.04 * band(rng.standard_normal(len(x)), 500, 8000) * (pulse > 0.05)
    # 'i' into 'yo' into a held, open 'o' (a shout raises the first formant).
    glide = 1 / (1 + np.exp(-(x - 0.075) / 0.015))
    tracks = [
        (300 + 300 * glide, 80 + 40 * glide),
        (2250 - 1300 * glide, 110 + 0 * glide),
        (2950 - 450 * glide, 160 + 0 * glide),
        (3500 + 0 * x, 260 + 0 * x),
        (4400 + 0 * x, 320 + 0 * x),
    ]
    voice = formants(source, tracks)
    env = np.minimum(x / 0.03, 1) * (1 - np.clip((x - 0.42) / 0.2, 0, 1)) ** 1.4
    voice *= env * (1 + 0.15 * np.clip((x - 0.1) / 0.2, 0, 1))
    voice = voice / (np.max(np.abs(voice)) + 1e-9)
    s = 1.55
    out = np.zeros(len(t(s)))
    place(out, 0.8 * voice, 0.0)
    place(out, 1.0 * tsuzumi(), 0.66)
    # A small hall around both.
    ir_t = t(0.9)
    ir = rng.standard_normal(len(ir_t)) * np.exp(-ir_t / 0.16)
    ir[: int(0.015 * RATE)] = 0
    ir /= np.sqrt(np.sum(ir ** 2))
    out = out + 0.22 * signal.fftconvolve(out, ir)[: len(out)]
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
