# DSP notes

Technical notes, measurements and design decisions for the DSP modules.

## Latency per mode

| Mode | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| Passthrough (phases 1–2) | 0 | 0 | 0 | 0 |
| Live (PSOLA) | phase 3 | | | |
| Tracking (PSOLA) | phase 3 | | | |
| HQ | phase 4 | | | |
| Vintage | phase 4 | | | |

Values in samples. Reported latency must equal measured latency: `tools/hosttest` checks it
with an impulse at 44.1 / 48 / 96 kHz on every CI run.

---

## PitchDetector (phase 2) — `src/dsp/PitchDetector.{h,cpp}`

### Signal chain (analysis only — the audio path is not touched)
```
input (mono sum of all input channels, NaN/Inf -> 0)
  -> [if fs >= 40 kHz] 4th-order Butterworth LPF @ 0.4·fa, decimate by D (power of two)
        so the analysis rate fa = fs / D is in [20, 40) kHz
        (44.1 -> 22.05, 48 -> 24, 88.2 -> 22.05, 96 -> 24, 192 -> 24 kHz)
  -> 2nd-order HPF @ max(15 Hz, minF0 / 2)            (rumble)
  -> zero-crossing counter (broadband)                 (voicing)
  -> 4th-order LPF @ clamp(4·maxF0, 2 kHz, 0.45·fa)    (rejects breath / hiss)
  -> ring buffer; one analysis frame every hop
```

### Geometry
- τmax = ⌈fa / minF0⌉ + 1, τmin = ⌊fa / maxF0⌋, integration window W = τmax,
  frame N = W + τmax + 2 (≈ 2 periods of the lowest f0, as per spec).
- FFT size = next power of two ≥ N. One FFT object per order is created in `prepare()`, so
  switching profile at runtime never allocates.
- Hop = 2.67 ms (64 samples at 24 kHz, ~375 frames/s); for very long windows (low
  instrument profile) hop = max(base, N/12) to bound CPU.

### Per frame
1. r(τ) = Σ_{j<W} x_j x_{j+τ} via FFT: IFFT(conj(FFT(x[0..W))) · FFT(x[0..N))). JUCE's inverse
   FFT is normalised by 1/N on every backend (fallback, vDSP, IPP, MKL).
2. Energy terms by prefix sums: m(τ) = Σ x_j² + Σ x_{j+τ}²; YIN difference d(τ) = m(τ) − 2r(τ).
   Unit test checks it against the direct O(N²) sum (relative error < 1e-3).
3. YIN: CMNDF d'(τ) = d(τ)·τ / Σ_{1..τ} d. First dip under the threshold → **minimum of the
   whole sub-threshold region** (the classic "first local minimum" stops on noise ripples:
   50–200 cent errors at high sensitivity before the fix). If no dip is under the threshold,
   take the first dip within 1.3 × global minimum + 0.02 (the global minimum is often 2T).
4. **Sub-multiple check**: if a period T/k (k = 4..2) is almost as periodic
   (d'(T/k) < 1.25·d'(T) + 0.04), prefer it — removes octave-down errors in noise.
5. Parabolic interpolation of the minimum on d(τ) (not d', which is biased by the cumulative
   normalisation).
6. MPM: NSDF n(τ) = 1 − d(τ)/m(τ) from the same terms (almost free). Key maxima of positive
   lobes; first one ≥ 0.9 × highest. Parabolic interpolation.
7. Hybrid choice: YIN by default; if YIN and MPM disagree by more than half an octave, keep
   the shorter period unless its d' is clearly worse (> +0.03).
8. Octave hysteresis: an octave jump relative to the previous voiced frame is held at the old
   octave (if still periodic there) for up to 8 frames (~21 ms); a real leap is accepted after
   that.
9. Voicing: periodic (d' ≤ voicing threshold) AND RMS > gate AND NOT (ZCR > limit AND weakly
   periodic). Onset after 2 voiced frames, release after 3 unvoiced frames.
10. Output f0 = median of the last 5 voiced estimates (in MIDI/log domain).

### Sensitivity (0–100, default 50)
| Sensitivity | YIN threshold | Voicing threshold | Gate | ZCR limit |
|---|---|---|---|---|
| 0 | 0.06 | 0.18 | −45 dBFS | 2.5 kHz |
| 50 | 0.15 | 0.27 | −55 dBFS | 4.0 kHz |
| 100 | 0.40 | 0.52 | −65 dBFS | 5.5 kHz |

### Measured accuracy (unit tests, synthetic signals)
- Sines and band-limited sawtooths, 3 frequencies per profile (near min, geometric mean,
  near max), at 44.1 / 48 / 96 / 192 kHz: **max error < 5 cents** on every voiced frame after
  200 ms (requirement: < 5 cents).
- Weak fundamental (fundamental 8 % of the 2nd harmonic): no octave errors, < 10 cents.
- Sine + white noise: ~17 dB SNR → max 2.6 cents; ~−2 dB SNR (sensitivity 100) → median
  ~16 cents, no octave errors.
- Real octave leap (200 → 400 Hz) followed within ~80 ms.
- YIN alone and MPM alone both < 5 cents on sines (kept selectable: `Config::algorithm`).

### CPU (tools/bench, 128-sample blocks, Linux x64 container, JUCE fallback FFT)
| Profile | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| Soprano | 0.88 % | 0.85 % | 1.07 % | 0.98 % |
| Alto / Tenor | 1.35 % | 1.37 % | 1.40 % | 1.50 % |
| Voz grave masculina | 1.33 % | 1.34 % | 1.36 % | 1.44 % |
| Instrumento | 1.14 % | 1.15 % | 1.18 % | 1.26 % |
| Instrumento grave | 1.75 % | 1.74 % | 1.78 % | 1.84 % |

Whole `processBlock` (stereo, default profile): ~1.5 % of one core at 44.1–192 kHz. CI prints the
same table for Windows (fallback FFT) and macOS (vDSP, expected faster) in the run summary.
Analysing at 40–48 kHz instead of 20–40 kHz cost 2–4× more for the same accuracy.

### Known limitations / next steps
- Detection lag ≈ half the analysis window (≈ 1 period of the lowest f0 of the profile) +
  median (2 frames). Phase 3 must compensate it with the plugin latency (lookahead) in
  Tracking/HQ modes; Live mode accepts it.
- Polyphonic or heavily distorted input is out of scope (monophonic detector).
