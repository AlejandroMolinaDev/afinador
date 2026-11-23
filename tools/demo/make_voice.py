# Synthetic "sung" phrase (source-filter model), deliberately out of tune.
import numpy as np, wave, sys
sr = 48000
# (start s, midi, cents off) — A minor melody, sung flat/sharp with scoops
notes = [(0.00, 57, -30), (0.55, 60, +35), (1.10, 64, -25), (1.80, 62, +40), (2.40, 60, -20), (3.00, 57, +25)]
dur = 3.9
t = np.arange(int(sr * dur)) / sr
midi = np.zeros_like(t)
for i, (start, m, c) in enumerate(notes):
    end = notes[i + 1][0] if i + 1 < len(notes) else dur
    seg = (t >= start) & (t < end)
    local = t[seg] - start
    prev = notes[i - 1][1] + notes[i - 1][2] / 100 if i > 0 else m - 1
    target = m + c / 100
    scoop = np.clip(local / 0.08, 0, 1)
    vib = np.where(local > 0.25, 0.3 * np.sin(2 * np.pi * 5.3 * local) * np.clip((local - 0.25) / 0.2, 0, 1), 0)
    midi[seg] = prev + (target - prev) * scoop + vib
f0 = 440 * 2 ** ((midi - 69) / 12)
phase = np.cumsum(f0 / sr)
# glottal-like source: harmonics with -12 dB/oct
src = np.zeros_like(t)
for k in range(1, 60):
    amp = 1.0 / k ** 2
    mask = k * f0 < 0.45 * sr
    src += mask * amp * np.sin(2 * np.pi * k * phase)
# formant resonators ("a")
def resonator(x, f, bw):
    r = np.exp(-np.pi * bw / sr); th = 2 * np.pi * f / sr
    a1, a2 = -2 * r * np.cos(th), r * r
    y = np.zeros_like(x); y1 = y2 = 0.0
    g = 1 - r
    for n in range(len(x)):
        yn = g * x[n] - a1 * y1 - a2 * y2
        y[n] = yn; y2 = y1; y1 = yn
    return y
out = resonator(src, 800, 80) + 0.6 * resonator(src, 1150, 90) + 0.25 * resonator(src, 2900, 120)
# phrase envelope + short breath gaps
env = np.ones_like(t)
for start, _, _ in notes[1:]:
    env *= 1 - 0.85 * np.exp(-((t - start) / 0.012) ** 2)
env *= np.clip(t / 0.03, 0, 1) * np.clip((dur - t) / 0.08, 0, 1)
out = out * env
out += 0.003 * np.random.default_rng(1).standard_normal(len(t))
out = 0.6 * out / np.max(np.abs(out))
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes((out * 32767).astype("<i2").tobytes())
print("wrote", sys.argv[1])
