# DSP notes

Technical notes, measurements and design decisions for the DSP modules.

## Latency per mode (phase 4, measured = reported)

| Mode | Engine | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|---|
| Live | PSOLA, 3 ms | 132 (3.0 ms) | 144 (3.0 ms) | 288 (3.0 ms) | 576 (3.0 ms) |
| Tracking (default) | PSOLA, 10 ms | 441 (10 ms) | 480 (10 ms) | 960 (10 ms) | 1920 (10 ms) |
| HQ | signalsmith-stretch, 90 ms block + 10 ms interval | 4410 (100 ms) | 4800 (100 ms) | 9600 (100 ms) | 19200 (100 ms) |
| Vintage | PSOLA, dry grains, 3 ms | 132 | 144 | 288 | 576 |

Values in samples. `tests/unit/CorrectionTests.cpp` measures them with an impulse for every mode at
44.1 / 48 / 96 kHz (measured == reported, exactly), and `tools/hosttest` re-checks every mode on the
built binary in CI. The dry path in `mix` and in bypass is delayed by the same amount, so FL's PDC
keeps everything aligned.

**Live mode note:** with 3 ms of lookahead, a grain often has to come from the previous pitch
period, so voiced parts get up to ~1 extra period of delay (≈ 2.3 ms at 440 Hz, ≈ 8 ms at 120 Hz)
on top of the reported 3 ms; unvoiced parts (consonants) are exactly 3 ms. This is inaudible as
timing, but with `mix` < 100 % in Live mode it can cause slight phasing on low voices. Tracking
mode (10 ms) aligns grains for f0 ≥ ~50 Hz.

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

---

## NoteMapper (phase 3) — `src/dsp/NoteMapper.{h,cpp}`, `src/dsp/Scales.h`
- Key (0–11) + scale (14 types, append-only enum) → 12-bit mask; per pitch class override
  `Escala | Activa | Ignorar | Eliminar` (parameters `noteState0..11`). Effective state: override if
  set, else Active in scale / Remove out of scale. "Personalizada" = chromatic shaped by overrides.
- Target = nearest **valid** note (Active or Ignore) within ±12 semitones. Ignore notes are targets
  that are not corrected (original pitch passes). Remove notes are never targets (redirect).
- Hysteresis: stay on the current note until it is 2 × 0.15 semitones farther than the nearest one
  (≈ 15 cents past the midpoint) and < 1.5 semitones away.
- Pitches are in MIDI units relative to the A4 reference (400–480 Hz parameter).

## CorrectionCurve (phase 3) — `src/dsp/CorrectionCurve.{h,cpp}`
- Error e = (target − detected) × 100 cents (0 for Ignore / no target / unvoiced → reset).
- Tolerance T: weight 1 for |e| ≤ T, cosine fade to 0 over the next 20 cents, 0 beyond; T = 0 disables.
- One-pole smoother on the weighted error per analysis frame: τ = retune speed (0 → instant).
  Not reset on note changes, so slow speeds glide between notes and fast deviations (vibrato) pass.
- Natural hold h: τ += h · 300 ms · ramp, ramp 0→1 between 120 ms and 420 ms after the target note
  changed. Attacks stay tight; long notes keep micro-variations.
- Ratio = 2^((correction + 100·transpose) / 1200) → PsolaShifter.

## PsolaShifter (phase 3) — `src/dsp/PsolaShifter.{h,cpp}`
- Streaming TD-PSOLA, one set of grains for all channels (phase-coherent stereo).
- Epochs: predicted one period after the last, snapped to the max of a 1 kHz one-pole low-passed
  mono signal within ±30 % of the period; reseeded after gaps. Unvoiced: fixed 5 ms grid.
- Synthesis marks every period/ratio; each takes the nearest known epoch e ≤ s + latency and
  overlap-adds a Hann grain of ±1 analysis period (left half shortened if created late).
- Normalised OLA: Σ w·x / max(Σ w, 1) — no gain pumping for ratio > 1, natural gaps for
  ratio < 1 (formant-preserving PSOLA behaviour).
- Unvoiced grains use e == s on a constant grid: Hann COLA → output == input delayed exactly
  (unit-tested bit-exact). A 6 ms voicing gain also crossfades wet/dry at transitions.
- Mix smoothed (20 ms); latency changes fade in over 10 ms; NaN/Inf input sanitised by the processor.
- Measured (tests): pitch error < 3 cents for −12…+12 semitones on a band-limited saw; level within
  −8…+4 dB; no clicks at voiced/unvoiced transitions (second difference < 0.02 on a 0.5 sine).

### Control rate
The processor runs the chain in sub-blocks of 32 samples, so detection → mapping → curve →
shifter control is updated every analysis frame (~2.7 ms) whatever block size FL delivers.

### Demo / objective check (`tools/demo/run_demo.sh`, also run in CI)
Synthetic source-filter "sung" phrase, out of tune by −30…+40 cents, A minor:

| Render | Median deviation from scale | p90 |
|---|---|---|
| Original | 28.9 ¢ | 44.5 ¢ |
| Natural (60 ms, hold 50 %) | 10.9 ¢ (vibrato kept) | 28.6 ¢ |
| Hard tune (0 ms) | 0.6 ¢ | 2.1 ¢ |

### CPU (phase 3, whole processBlock, stereo, Tracking, Linux container)
44.1 kHz 1.62 % · 48 kHz 1.66 % · 96 kHz 2.00 % · 192 kHz 2.23 % of one core (budget: Live < 3 %).

---

## Phase 4 — formants, HQ engine, Vintage

### Formant parameters → factors
- `formantPreserve` (default on), `formantShift` (±12 st), `formantLink`, `character` (±50 %).
- Static factor F = 2^(shift/12) · (1 + character/100) · (preserve && link ? 2^(transpose/12) : 1).
- PSOLA: grain resampling factor fr = preserve ? F : F · pitchRatio (formants follow the pitch:
  "chipmunk"). Grains are read at e + (x − s)·fr with linear interpolation; window half-length in
  the output = grainScale · period / fr. A look-ahead guard keeps every source sample inside what
  has already arrived when fr > 1.
- HQ: `setFormantFactor (F, compensatePitch = preserve)` + `setFormantBase (f0 / fs)` when voiced.
  **Units matter:** signalsmith wants the base frequency normalised to the sample rate — passing Hz
  silently disabled formant handling (bug found by the formant tests, fixed).
- Vintage forces preserve off (formants follow the pitch), grainScale 0.7 (shorter, drier grains:
  windows no longer overlap fully → classic "buzzier" hard-tune texture), speed 0, no tolerance/hold.

### Measured (spectral centroid of a 2-formant vowel, ratio output/input)
| Test | Expected | PSOLA | HQ |
|---|---|---|---|
| transpose +7, preserve on | 1.00 | 0.98 | 0.97 |
| transpose +7, preserve off | 1.50 | 1.49 | 1.49 |
| formant shift −5 st | 0.75 | 0.75 | 0.74 |
| formant shift +5 st | 1.33 | 1.32 | 1.45 |
| character −40 % / +40 % (PSOLA) | 0.60 / 1.40 | 0.61 / 1.39 | — |

### HqShifter — `src/dsp/HqShifter.{h,cpp}`
- signalsmith-stretch 1.4.0 (MIT), `configure (channels, 0.09·fs, 0.01·fs, splitComputation = true)`:
  computation spread over calls (no CPU spikes with FL's small buffers). Latency = input + output
  latency of the STFT = 100 ms at every sample rate.
- Block size choice (measured, ratio 2 / 1.5 / 0.5, formant compensation on):
  45 ms → +4.1 / +2.6 / −1.7 cents · 70 ms → +2.2 / +1.4 / −0.4 · **90 ms → +1.5 / +0.9 / −0.05** ·
  105 ms → +1.3 / +0.8 / −0.2. Shorter blocks reduce frequency resolution and bias the shifted pitch.
  90 ms keeps the spec's 50–120 ms latency. Residual: ~+2 c median at +12 st in the full chain
  (PSOLA: < 0.4 c); max error < 5 c in every transposition test.
- Transposition with an 8 kHz tonality limit (keeps breath and sibilance natural); unvoiced → ratio 1.
- Controls are cached and re-applied after `reset()` (engine switch).
- Engine switch at runtime (Tracking ↔ HQ): the newly active engine is reset → ~latency of silence
  then fade-in. Acceptable for a mode change; see ROADMAP.

### Real-time safety (all modes)
`tests/unit/RealtimeSafetyTests.cpp` replaces the global `operator new` in the test executable and
counts allocations on the audio thread during `processBlock` (random block sizes, voiced + noise,
transpose and formant shift active): **0 allocations in Live, Tracking, HQ and Vintage**. A
self-test checks that the counter does detect allocations.

### CPU (phase 4, whole processBlock, stereo, 128-sample blocks, Linux container, generic FFT)
| Mode | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| Live | 1.8 % | 2.3 % | 2.4 % | 3.2 % |
| Tracking | 1.8 % | 1.8 % | 2.5 % | 3.3 % |
| HQ | 4.9 % | 5.5 % | 10.9 % | 22.6 % |
| Vintage | 1.7 % | 1.9 % | 2.2 % | 3.3 % |

HQ scales with the sample rate (STFT); macOS builds use Accelerate (faster). HQ is meant for mixing /
export, not live monitoring.

---

## Phase 5 — vibrato

### VibratoAnalyzer (own method) — `src/dsp/VibratoAnalyzer.{h,cpp}`
- Input: detected pitch in cents per analysis frame (~375 frames/s). Every 4 frames (~10 ms) the
  last **0.6 s** (≥ 2 periods at 3.5 Hz) are linearly detrended (least squares); the normalised
  autocorrelation of the residual is searched for a local maximum at lags for **3.5–8 Hz**.
- Accepted as vibrato when r ≥ 0.45, the residual crosses zero ≥ max(3, 0.7 · 2N/lag) times
  (a note change or a glide doesn't) and the peak depth is **15–150 cents**.
- Rate: parabolic refinement of the correlation peak. Depth: local trend from the means of the
  last two full periods (each cancels the oscillation exactly) → RMS·√2 over the last period; exact
  even during a glide (a plain detrend over 1.8 periods underestimated depth by 14 % at 4 Hz).
- Split: centre = mean of the last period; vibrato = weight · (pitch − centre), clamped to
  ±1.5 × depth; weight follows detection with 60 ms attack / 40 ms release.
- Measured (curve): rate error < 0.1 %, depth error < 0.5 % at 4 / 5.5 / 7 Hz × 25 / 50 / 120 c;
  through audio + pitch detector (5.5 Hz ±50 c): within ±10 % (spec). Glides, note steps and
  ±3 c detector jitter are rejected (weight < 0.3).
- Known limitation: needs ~0.6 s of oscillation before it is recognised; with fast correction the
  first part of a vibrato is flattened, then preserved (see ROADMAP).

### "Conservar vibrato" (0–200 %, default 100 %)
NoteMapper and CorrectionCurve work on the **centre**; the oscillation is put back scaled:
correction += (keep − 1) · vibrato (only while correcting; Ignore notes pass untouched).
Output ≈ target + keep · vibrato. Vintage forces keep = 0 (classic flat hard tune).

Measured end-to-end (vibrato saw, centre +25 c, 5.5 Hz ±50 c, speed 0):

| keep | PSOLA (Tracking) depth / centre | HQ depth / centre |
|---|---|---|
| 0 % | 1.9 c / 0.1 c | 2.2 c / 1.7 c |
| 100 % | 49.9 c / −0.6 c | 49.2 c / 1.9 c |
| 200 % | 99.7 c / −1.3 c | 98.3 c / 2.6 c |

### HQ control alignment
signalsmith applies a transpose factor to the block it is analysing, centred `inputLatency()`
(~45 ms) in the past, while our ratio describes audio the detector saw ~18 ms ago (half the
analysis window + median group delay, `PitchDetector::getDetectionLagSamples()`). HqShifter queues
every control with its input time and applies it **inputLatency − detectorLag** later. Without
this, vibrato removal in HQ left 46 c of the 50 c (≈ 90° phase error); with it, 2 c. PSOLA needs no
alignment: its output period is built from the detected period and the ratio, which share the
same lag.

### VibratoSynth — `src/dsp/VibratoSynth.{h,cpp}`
Per note (new target note or voiced onset): wait `delay` (0–2000 ms), fade in over 250 ms, then
depth · sin(2π·rate·t) cents (rate 0.1–10 Hz, depth 0–100 c), with optional ±15 % rate / ±30 %
depth random walks (~1 Hz, xorshift, deterministic), amplitude modulation up to ±25 % (applied as a
per-sample ramp after the engine) and formant modulation (formant factor follows the vibrato).
Measured: depth 29.7 c for 30 c set, flat before the delay.

### CPU after phase 5 (whole chain, 48 kHz, 128-sample blocks)
Live 1.9 % · Tracking 1.9 % · HQ 5.8 % · Vintage 1.8 % (vibrato analysis is negligible).
