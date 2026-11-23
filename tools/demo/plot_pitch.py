# Pitch-tracks the demo renders (simple YIN) and plots them on a semitone grid.
# Usage: python3 plot_pitch.py <out.png>  (run inside the demo output folder)
import numpy as np, wave, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

def read(path):
    with wave.open(path) as w:
        sr = w.getframerate(); n = w.getnframes(); ch = w.getnchannels(); sw = w.getsampwidth()
        raw = w.readframes(n)
    if sw == 2:
        x = np.frombuffer(raw, "<i2").astype(float) / 32768
    else:  # 24-bit
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x) / float(1 << 23)
    return x.reshape(-1, ch)[:, 0], sr

def yin_track(x, sr, fmin=80, fmax=900, hop=240, W=1200, thr=0.12):
    tmax, tmin = int(sr / fmin), int(sr / fmax)
    times, f0 = [], []
    for start in range(0, len(x) - W - tmax, hop):
        seg = x[start:start + W + tmax]
        if np.sqrt(np.mean(seg[:W] ** 2)) < 0.01:
            times.append((start + W / 2) / sr); f0.append(np.nan); continue
        d = np.array([np.sum((seg[:W] - seg[t:t + W]) ** 2) for t in range(tmax + 1)])
        cm = d[1:] * np.arange(1, tmax + 1) / np.maximum(np.cumsum(d[1:]), 1e-12)
        cm = np.concatenate([[1], cm])
        cand = np.where(cm[tmin:] < thr)[0]
        if len(cand) == 0:
            times.append((start + W / 2) / sr); f0.append(np.nan); continue
        t = cand[0] + tmin
        while t + 1 <= tmax and cm[t + 1] < cm[t]: t += 1
        a, b, c = d[t - 1], d[t], d[t + 1]
        den = a - 2 * b + c
        tt = t + (0.5 * (a - c) / den if den != 0 else 0)
        times.append((start + W / 2) / sr); f0.append(sr / tt)
    return np.array(times), np.array(f0)

files = [("1_original_desafinado.wav", "Original (desafinada)"),
         ("2_natural.wav", "NovaPitch · corrección natural (60 ms, sostenido 50 %)"),
         ("3_hard_tune.wav", "NovaPitch · hard tune (velocidad 0 ms)")]

SURFACE, INK, INK2, MUTED, GRID, SERIES = "#fcfcfb", "#0b0b0b", "#52514e", "#8a8984", "#e6e5e0", "#2a78d6"
plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10})
fig, axes = plt.subplots(len(files), 1, figsize=(9, 7.2), sharex=True, facecolor=SURFACE)
names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
a_minor = {0, 2, 4, 5, 7, 9, 11}
summary = []
for ax, (path, title) in zip(axes, files):
    x, sr = read(path)
    t, f = yin_track(x, sr)
    midi = 69 + 12 * np.log2(f / 440)
    dev = np.abs(100 * (midi - np.round(midi)))
    valid = ~np.isnan(dev)
    med = np.nanmedian(dev[valid]); p90 = np.nanpercentile(dev[valid], 90)
    summary.append((title, med, p90))
    ax.set_facecolor(SURFACE)
    for n in range(55, 67):
        if n % 12 in a_minor:
            ax.axhline(n, color=GRID, lw=0.8, zorder=0)
    ax.plot(t, midi, color=SERIES, lw=2, solid_capstyle="round", zorder=2)
    ax.set_ylim(54.5, 65.5)
    ticks = [n for n in range(55, 66) if n % 12 in a_minor]
    ax.set_yticks(ticks, [f"{names[n % 12]}{n // 12 - 1}" for n in ticks], color=INK2, fontsize=8.5)
    ax.tick_params(axis="both", length=0, colors=INK2)
    for s in ax.spines.values(): s.set_visible(False)
    ax.set_title(title, loc="left", color=INK, fontsize=11, fontweight="bold", pad=4)
    ax.text(1.0, 1.02, f"desviación mediana {med:.1f} ¢ · p90 {p90:.1f} ¢", transform=ax.transAxes,
            ha="right", va="bottom", color=INK2, fontsize=9)
axes[-1].set_xlabel("tiempo (s)", color=INK2)
fig.suptitle("Pitch de la misma frase antes y después de NovaPitch (escala La menor)", x=0.02, ha="left",
             color=INK, fontsize=12.5, fontweight="bold")
fig.text(0.02, 0.935, "Líneas grises = notas de la escala. La voz sintética está desafinada entre −30 y +40 cents a propósito.",
         ha="left", color=INK2, fontsize=9)
fig.tight_layout(rect=(0, 0, 1, 0.93))
fig.savefig(sys.argv[1], dpi=160, facecolor=SURFACE)
for s in summary: print(f"{s[0]}: mediana {s[1]:.1f} c, p90 {s[2]:.1f} c")
