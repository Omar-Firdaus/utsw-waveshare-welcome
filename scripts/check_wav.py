#!/usr/bin/env python3
"""Check RECALL WAV files: format, exact length, and signs of gaps or clicks."""
import array
import math
import sys
import wave


def check(path):
    with wave.open(path, "rb") as w:
        rate, width, channels, frames = w.getframerate(), w.getsampwidth(), w.getnchannels(), w.getnframes()
        samples = array.array("h", w.readframes(frames))
    print(f"{path}")
    print(f"  format   {rate} Hz, {width * 8}-bit, {channels} ch, PCM")
    print(f"  length   {frames} samples = {frames / rate:.4f} s")
    if not samples:
        return
    mean = sum(samples) / len(samples)
    rms = math.sqrt(sum((s - mean) ** 2 for s in samples) / len(samples))
    peak = max(abs(s) for s in samples)
    print(f"  level    rms {20 * math.log10(max(rms, 1e-9) / 32768):.1f} dBFS, peak {20 * math.log10(max(peak, 1) / 32768):.1f} dBFS, dc {mean:.0f}")
    # A dropped buffer shows up as a run of identical samples or a jump far above the signal's usual step.
    longest, run = 1, 1
    for a, b in zip(samples, samples[1:]):
        run = run + 1 if a == b else 1
        longest = max(longest, run)
    steps = sorted(abs(b - a) for a, b in zip(samples, samples[1:]))
    typical = steps[int(len(steps) * 0.999)] or 1
    jumps = sum(1 for s in steps if s > 8 * typical and s > 2000)
    print(f"  gaps     longest constant run {longest} samples; large jumps {jumps}")


for p in sys.argv[1:]:
    check(p)
