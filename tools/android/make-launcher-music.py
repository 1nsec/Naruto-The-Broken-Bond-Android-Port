#!/usr/bin/env python3
"""The launcher's theme, synthesized: an original 40-second loop in D minor
with a bamboo flute over a koto, a low string bass, a pad, taiko and shime
drums, written for the port. Nothing is sampled. Writes
android/app/src/main/res/raw/launcher_theme.ogg (ffmpeg with libvorbis), or
.wav when ffmpeg is missing.

    python3 tools/android/make-launcher-music.py [preview.wav]

The loop is seamless: what rings past the last bar (notes and reverb) is
folded onto the first one.
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
BPM = 96
BEAT = 60 / BPM
BAR = 4 * BEAT
BARS = 16
LOOP = int(round(BARS * BAR * RATE))
TAIL = int(4.0 * RATE)
OUT = Path(__file__).resolve().parents[2] / "android/app/src/main/res/raw"
rng = np.random.default_rng(1977)

NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def hz(note):
    """'A4', 'Bb3', 'C#5' to hertz."""
    pitch = NAMES[note[0]]
    rest = note[1:]
    while rest[0] in "b#":
        pitch += -1 if rest[0] == "b" else 1
        rest = rest[1:]
    midi = 12 * (int(rest) + 1) + pitch
    return 440.0 * 2 ** ((midi - 69) / 12)


class Bus:
    """A stereo buffer one loop long plus a tail, with a send to the reverb."""

    def __init__(self):
        self.dry = np.zeros((2, LOOP + TAIL))
        self.wet = np.zeros((2, LOOP + TAIL))

    def add(self, sound, beat, pan=0.0, gain=1.0, reverb=0.25, jitter=0.006):
        at = beat * BEAT + rng.uniform(-jitter, jitter)
        start = max(0, int(round(at * RATE)))
        end = min(self.dry.shape[1], start + sound.shape[-1])
        if sound.ndim == 1:
            angle = (pan + 1) * np.pi / 4
            sound = np.stack([np.cos(angle) * sound, np.sin(angle) * sound])
        piece = sound[:, : end - start] * gain
        self.dry[:, start:end] += piece
        self.wet[:, start:end] += piece * reverb


def seconds(n):
    return np.arange(int(n * RATE)) / RATE


def lowpass(x, freq, order=2):
    return signal.sosfilt(signal.butter(order, freq, fs=RATE, output="sos"), x)


def bandpass(x, low, high, order=2):
    return signal.sosfilt(signal.butter(order, [low, high], btype="band", fs=RATE, output="sos"), x)


# --- Instruments --------------------------------------------------------------------------------

def flute(freq, length, velocity=1.0):
    """A bamboo flute: a near-sine tone that scoops up into the note, opens a vibrato, and breathes."""
    t = seconds(length + 0.3)
    cents = -40 * np.exp(-t / 0.035)
    depth = 16 * np.clip((t - 0.22) / 0.45, 0, 1)
    cents += depth * np.sin(2 * np.pi * 5.3 * t + rng.uniform(0, 6.28))
    phase = 2 * np.pi * np.cumsum(freq * 2 ** (cents / 1200)) / RATE
    tone = (np.sin(phase) + 0.3 * np.sin(2 * phase) + 0.13 * np.sin(3 * phase) + 0.05 * np.sin(4 * phase)
            + 0.02 * np.sin(5 * phase))
    swell = 0.82 + 0.18 * np.clip(t / max(length, 0.2), 0, 1)
    env = np.clip(t / 0.05, 0, 1) ** 1.6 * swell
    env *= np.where(t < length, 1.0, np.exp(-(t - length) / 0.07))
    breath = bandpass(rng.standard_normal(len(t)), freq * 1.5, min(freq * 6, 12000))
    breath *= 0.55 * np.exp(-t / 0.045) + 0.1 * env
    return (0.85 * tone * env + 0.5 * breath) * velocity


def pluck(freq, length, brightness=0.5, decay=2.6, velocity=1.0):
    """A plucked string (Karplus-Strong, tuned with an all-pass): the koto and the bass."""
    period = RATE / freq
    delay = int(np.floor(period - 0.6))
    frac = period - 0.5 - delay
    c = (1 - frac) / (1 + frac)
    g = np.exp(-decay / freq)
    n = int(length * RATE)
    excitation = np.zeros(n)
    burst = rng.uniform(-1, 1, delay + 1)
    burst = signal.lfilter([1 - brightness], [1, -brightness], burst)
    excitation[: len(burst)] = burst - np.mean(burst)
    a = np.zeros(delay + 3)
    a[0], a[1] = 1.0, c
    a[delay] -= g / 2 * c
    a[delay + 1] -= g / 2 * (1 + c)
    a[delay + 2] -= g / 2
    out = signal.lfilter([1.0, c], a, excitation)
    fade = int(0.08 * RATE)
    out[-fade:] *= np.linspace(1, 0, fade)
    return out * velocity


def koto(freq, velocity=1.0):
    tone = pluck(freq, 1.25, brightness=0.25, decay=3.4, velocity=1.0)
    t = seconds(len(tone) / RATE)
    # The plectrum's tick.
    tick = bandpass(rng.standard_normal(len(t)), 2500, 9000) * np.exp(-t / 0.003)
    return (tone + 0.25 * tick) * velocity


def bass(freq, length, velocity=1.0):
    return pluck(freq, length + 0.25, brightness=0.7, decay=2.2) * velocity


def pad(freqs, length, velocity=1.0):
    """Soft strings: detuned, rounded saw voices, one side each."""
    t = seconds(length + 1.4)
    env = np.clip(t / 0.9, 0, 1) ** 2 * np.where(t < length, 1.0, np.exp(-(t - length) / 0.45))
    out = np.zeros((2, len(t)))
    for f in freqs:
        for side, detune in ((0, -7), (1, 7)):
            f0 = f * 2 ** (detune / 1200)
            voice = np.zeros(len(t))
            start = rng.uniform(0, 6.28)
            for k in range(1, 13):
                voice += np.sin(2 * np.pi * f0 * k * t + start * k) * np.exp(-k / 3.2) / k
            out[side] += voice
    return out * env * velocity / len(freqs)


def taiko(velocity=1.0):
    t = seconds(1.3)
    f = 72 + 85 * np.exp(-t / 0.045)
    body = np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-t / 0.36)
    body += 0.35 * np.sin(2 * np.pi * np.cumsum(f * 1.59) / RATE) * np.exp(-t / 0.14)
    skin = lowpass(rng.standard_normal(len(t)), 900) * np.exp(-t / 0.025)
    x = body + 0.9 * skin
    return np.tanh(1.8 * x) / np.tanh(1.8) * velocity


def shime(velocity=1.0, rim=False):
    """The shime-daiko: a tight high drum, or a hit on its rim."""
    t = seconds(0.25)
    if rim:
        x = (np.sin(2 * np.pi * 1850 * t) * 0.7 + np.sin(2 * np.pi * 3300 * t) * 0.4) * np.exp(-t / 0.012)
        x += bandpass(rng.standard_normal(len(t)), 2500, 8000) * np.exp(-t / 0.004)
    else:
        f = 390 + 140 * np.exp(-t / 0.008)
        x = np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-t / 0.06)
        x += 0.6 * bandpass(rng.standard_normal(len(t)), 1500, 7000) * np.exp(-t / 0.01)
    return x * velocity


def bell(freq, velocity=1.0):
    """A small temple bowl: inharmonic partials with a long ring."""
    t = seconds(3.5)
    x = np.zeros(len(t))
    for ratio, amp, decay in ((1.0, 1.0, 2.4), (2.71, 0.45, 1.3), (5.15, 0.25, 0.7), (8.9, 0.1, 0.35)):
        x += amp * np.sin(2 * np.pi * freq * ratio * t + rng.uniform(0, 6.28)) * np.exp(-t / decay)
    return x * np.clip(t / 0.003, 0, 1) * velocity


# --- The score ---------------------------------------------------------------------------------

CHORDS = ["Dm", "Dm", "Bb", "C", "Dm", "Dm", "Bb", "A"] * 2
ROOTS = {"Dm": "D2", "Bb": "Bb1", "C": "C2", "A": "A1"}
FIFTHS = {"Dm": "A2", "Bb": "F2", "C": "G2", "A": "E2"}
PADS = {"Dm": ["A3", "D4", "F4"], "Bb": ["F3", "Bb3", "D4"], "C": ["G3", "C4", "E4"], "A": ["E3", "A3", "D4"]}
KOTO = {
    "Dm": "D4 A4 D5 E5 F5 E5 D5 A4",
    "Bb": "Bb3 F4 Bb4 D5 F5 D5 Bb4 F4",
    "C": "C4 G4 C5 D5 E5 D5 C5 G4",
    "A": "A3 E4 A4 D5 E5 D5 A4 E4",
}
ACCENTS = [1.0, 0.6, 0.78, 0.62, 0.88, 0.6, 0.76, 0.6]
MELODY = [
    # A: the theme, low and calm.
    "A4:1.5 D5:.5 E5:1 F5:1", "E5:1.5 D5:.5 A4:2", "Bb4:1 D5:1 F5:1 E5:.5 D5:.5", "E5:3 r:1",
    "A4:1.5 D5:.5 E5:1 F5:1", "A5:1.5 G5:.5 F5:1 E5:1", "F5:1 D5:1 Bb4:1 D5:1", "E5:3 D5:.5 E5:.5",
    # B: up an octave, with the drums.
    "F5:.5 A5:.5 D6:2 C6:.5 A5:.5", "A5:1 G5:.5 F5:.5 E5:1 F5:1", "D5:1.5 F5:.5 Bb5:2", "A5:.5 G5:.5 E5:1 G5:2",
    "A5:1.5 D6:.5 C6:1 A5:1", "G5:1 F5:1 E5:1 D5:1", "F5:1 E5:.5 D5:.5 Bb4:1 D5:1", "E5:2 r:2",
]


def compose():
    bus = Bus()
    for bar, chord in enumerate(CHORDS):
        b0 = bar * 4
        second_half = bar >= 8
        # Koto: an arpeggio in eighths.
        for i, note in enumerate(KOTO[chord].split()):
            bus.add(koto(hz(note), ACCENTS[i] * (1.0 if second_half else 0.85)), b0 + i * 0.5,
                    pan=-0.35, gain=0.33, reverb=0.3)
        # Pad.
        bus.add(pad([hz(n) for n in PADS[chord]], BAR, 1.0 if second_half else 0.8), b0, gain=0.2, reverb=0.4,
                jitter=0)
        # Bass.
        root, fifth = hz(ROOTS[chord]), hz(FIFTHS[chord])
        if second_half:
            for beat, f, length in ((0, root, 1.4), (1.5, root, 0.45), (2, fifth, 1.4), (3.5, root * 2, 0.45)):
                bus.add(bass(f, length * BEAT), b0 + beat, gain=0.42, reverb=0.08)
        else:
            bus.add(bass(root, 1.9 * BEAT), b0, gain=0.42, reverb=0.08)
            bus.add(bass(fifth, 1.9 * BEAT), b0 + 2, gain=0.36, reverb=0.08)
        # Drums.
        if bar == 15:
            for beat, v in ((0, 0.9), (2, 0.7), (2.5, 0.6), (3, 0.75), (3.25, 0.62), (3.5, 0.85), (3.75, 1.0)):
                bus.add(taiko(v), b0 + beat, gain=0.55, reverb=0.3)
        elif second_half:
            for beat, v in ((0, 0.95), (2, 0.72), (3.5, 0.55)):
                bus.add(taiko(v), b0 + beat, gain=0.55, reverb=0.3)
        else:
            bus.add(taiko(1.0 if bar in (0, 4) else 0.7), b0, gain=0.55, reverb=0.3)
            if bar >= 4:
                bus.add(taiko(0.5), b0 + 2, gain=0.55, reverb=0.3)
        if second_half and bar != 15:
            for i in range(8):
                rim = i in (2, 6)
                bus.add(shime(0.8 if rim else (0.55 if i % 2 == 0 else 0.35), rim), b0 + i * 0.5, pan=0.45,
                        gain=0.16, reverb=0.15, jitter=0.003)
        # The flute.
        beat = b0
        for token in MELODY[bar].split():
            note, length = token.split(":")
            length = float(length)
            if note != "r":
                velocity = (1.0 if second_half else 0.85) * rng.uniform(0.92, 1.0)
                bus.add(flute(hz(note), length * BEAT, velocity), beat, pan=0.1, gain=0.3, reverb=0.4)
            beat += length
    for bar in (0, 8):
        bus.add(bell(hz("D6"), 1.0), bar * 4, pan=0.3, gain=0.07, reverb=0.5, jitter=0)
    return bus


def reverb(wet):
    """A hall: decaying, darkening noise with a short pre-delay, a little different on each side."""
    length = int(2.6 * RATE)
    t = np.arange(length) / RATE
    out = np.zeros_like(wet)
    for side in range(2):
        ir = rng.standard_normal(length) * np.exp(-t / 0.42)
        ir = lowpass(ir, 6000, 1)
        ir[: int(0.02 * RATE)] = 0
        ir /= np.sqrt(np.sum(ir ** 2))
        out[side] = signal.fftconvolve(wet[side], ir)[: wet.shape[1]]
    return out


def master(bus):
    mix = bus.dry + 0.9 * reverb(bus.wet)
    loop = mix[:, :LOOP].copy()
    loop[:, :TAIL] += mix[:, LOOP:LOOP + TAIL]  # What rings past the end goes back to the start.
    loop -= loop.mean(axis=1, keepdims=True)
    rms = np.sqrt(np.mean(loop ** 2))
    loop *= 10 ** (-17 / 20) / rms  # About -17 dBFS RMS.
    loop = np.tanh(loop * 1.1) / 1.1  # Round off the peaks of the drums.
    return loop / max(1.0, np.max(np.abs(loop)) / 0.95)


def write(x, target):
    pcm = (np.clip(x.T, -1, 1) * 32767).astype("<i2").tobytes()
    with tempfile.TemporaryDirectory() as folder:
        wav = Path(folder) / "theme.wav"
        with wave.open(str(wav), "wb") as f:
            f.setnchannels(2)
            f.setsampwidth(2)
            f.setframerate(RATE)
            f.writeframes(pcm)
        if target.suffix == ".wav":
            shutil.copyfile(wav, target)
        elif shutil.which("ffmpeg"):
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(wav), "-c:a", "libvorbis", "-q:a", "3",
                            "-metadata", "ANDROID_LOOP=true", str(target)], check=True)
        else:
            target = target.with_suffix(".wav")
            shutil.copyfile(wav, target)
    print(target, target.stat().st_size, "bytes")


if __name__ == "__main__":
    theme = master(compose())
    if len(sys.argv) > 1:
        write(theme, Path(sys.argv[1]))
    else:
        OUT.mkdir(parents=True, exist_ok=True)
        write(theme, OUT / "launcher_theme.ogg")
