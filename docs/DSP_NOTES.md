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

---

## Phase 7 — harmonies and MIDI

### MIDI (`dsp/MidiNoteState.h`, processor)
- Note on/off parsed per block from the host MIDI buffer (FL: plugin wrapper ⚙ → MIDI → Input port, plus a
  "MIDI Out" channel on the same port). Held notes: 128-bit set; a relaxed-atomic snapshot feeds the UI.
- **MIDI Target** (`midiTarget`): the held note whose octave copy is closest to the sung centre pitch is
  the target (overrides the NoteMapper). Sing in any octave.
- **Learn scale** (`midiScaleLearn`): pitch classes of note-ons are OR-ed into an atomic mask on the audio
  thread; the processor's message-thread timer writes them into `scale = Personalizada` and
  `noteState0..11` (learnt → Activa, others → Eliminar), so the learnt scale is visible, automatable and
  saved with the project. Each new learning pass starts empty.

### HarmonyEngine (`dsp/HarmonyEngine.{h,cpp}`)
- 4 voices; each owns a PsolaShifter (formant preserving, own formant factor) running with **the lead's
  latency** (3 / 10 / 100 ms), so harmonies stay aligned with the corrected lead in every mode. The lead's
  analysis is reused (f0, voicing, target note, scale); voices only add their interval.
- Voice pitch: base = lead target note (incl. transposition); deviation = lead output − base (vibrato and
  expression are inherited).
  - Diatonic: `stepDegrees(base, n, scaleMask)` — n scale steps in the current effective scale (Active or
    Ignore notes); a base outside the scale snaps to the nearest scale note first. In C major from E: +2 → G
    (minor 3rd), from C: +2 → E (major 3rd).
  - Semitones: lead output + n.
  - MIDI: i-th lowest held note (+ deviation); silent when fewer notes are held.
- Humanize h: slow random detune (±12 c × h, ~0.5 Hz) and a fixed delay (0–30 ms × h, different per voice).
- Gate: voices open only when the input level is above the bus gate; attack/release envelope per voice.
- Bus: HPF → LPF → peaking EQ (RBJ, Q 0.9) → width (M/S), then added to the output (stereo; summed for
  mono outputs). Mute/solo per voice. The lead's mix knob doesn't affect harmonies; bypass mutes them.
- Measured: diatonic third / +7 st / MIDI chord voices within 5 c (tests), pan hard-left < 1 % on R, gate
  and mute silent, 0 allocations with 4 voices and MIDI chords toggling.
- CPU (Linux container, 48 kHz, 128-sample blocks): Tracking lead 1.9 %, + 4 voices 4.1 % (~0.55 % per voice).
  Each voice currently tracks epochs itself; sharing the lead's epochs is a possible optimisation (ROADMAP).

## Phase 8 — graphical mode without ARA

FL Studio has no ARA, so the graphical mode works by **capture by playback**: the plugin records the
detected pitch while the host plays and indexes it by the host timeline.

### Host time
- Per block: `getPlayHead()->getPosition()` → `isPlaying` and `timeInSamples / sampleRate` (fallback
  `timeInSeconds`). The detector's frames carry the input sample counter, so a frame's timeline time is
  `blockTime + (frameSample − chunkStart − detectionLag) / fs`: the time of the analysed audio, not of the
  moment the estimate was produced.
- Capture and playback use the same timebase, so any constant offset the host applies to the reported
  position cancels out. Open question for the manual FL test: whether FL's PDC changes the reported position
  when the plugin latency changes (capture in Tracking, play in HQ). If it does, store the capture latency
  and compensate (ROADMAP).

### Capture
- While "Capturar" is on and the host plays, the audio thread pushes `CaptureFrame {time, midi, rmsDb}`
  (16 bytes) into a lock-free SPSC FIFO, decimated to one frame per 10 ms (plus every voicing change).
  The processor's 30 Hz message-thread timer drains it into the model.
- A capture pass ends when capture is switched off, the transport stops or the position jumps back
  (loop / relocation). On commit, the pass **replaces the captured data in its own time range** (± half a
  frame): re-capturing a section works like a punch-in.
- Stored in the project as binary (`Graph@captured`, base64 in the XML): 1.6 kB per second of capture
  (~1.3 MB per 10 minutes in the .flp).

### Objects and model (`plugin/graph/GraphModel`)
- `Graph` child of the plugin state tree: `Note {start, end, pitch, retuneMs, vibratoKeep, formantSt}`,
  `Line {start, end, pitch, endPitch}`, `Curve {start, end, points}`; times in seconds on the host timeline,
  pitches as fractional MIDI notes (relative to the A4 reference). Stable integer ids for the UI selection.
- Every edit goes through a `juce::UndoManager` (one transaction per gesture; ValueTree coalesces drags).
- **Import from automatic**: the captured curve is smoothed (centred ±40 ms mean of voiced frames, so
  vibrato and jitter don't split notes) and mapped with a `NoteMapper` configured with the current key,
  scale and note states (its hysteresis applies); runs of the same target ≥ 80 ms become notes, gaps
  > 50 ms or unvoiced frames end a run; "Ignore" notes produce no object (the original passes). One undo step.

### Compiled track and publication (`dsp/graph/CorrectionTrack.h`, `plugin/graph/GraphRuntime.h`)
- Each change recompiles the objects into an immutable `CorrectionTrack` (segments sorted by start; where
  objects overlap, the later start wins; curve points in one array). `lookup(t)` is a binary search, no
  allocation.
- Published by swapping an atomic pointer (message thread). The audio thread loads it once per block. The
  previous track goes to a graveyard and is deleted only after the audio thread has finished two more
  blocks (a block that started after the swap has completed), or in `prepareToPlay` / `releaseResources`.
  The audio thread never allocates, frees or locks.

### Playback (processor, per analysis frame)
- `graphMode` on + transport playing + an object at the frame's time → the object decides the target:
  - Note: target = note pitch; speed = per-note value or the global "Velocidad"; natural hold global;
    vibrato kept = per-note or global; extra formant shift per note.
  - Line / Curve: target = drawn pitch, followed exactly (speed 0, vibrato 0, no natural hold).
  - Tolerance is ignored (objects are explicit decisions).
- No object at that time, or transport stopped: **no correction** (original voice, still delayed by the
  latency). MIDI Target and the NoteMapper are not used in graphical mode.
- The vibrato split, formants, transposition, harmonies and engines (PSOLA/HQ) work as in automatic mode.

### Editor preview ("curva corregida")
- The editor draws, over the captured (original) curve, the predicted output where objects act: centre =
  running median (±90 ms, removes a vibrato cycle but keeps note steps sharp), correction = one-pole towards
  the object pitch with the note's (or global) speed, plus the oscillation around the centre × vibrato kept
  (clamped to ±0.8 st). Lines and curves are drawn exactly. It is a preview: the audio path uses the
  VibratoAnalyzer and the real-time CorrectionCurve.

### Measured
- End to end (unit test with a simulated play head): A3 +30 c captured → import gives one A3 note → edited
  to B3 and shortened to 0.8 s → playback: < 5 c from B3 inside the note, < 5 c from the input after it;
  transport stopped: input pitch; save → new instance → same result.
- Binary (host test, VST3 through JUCE hosting): a B3 note written into the project state → play
  0.4 c, loop 0.0 c, stopped 0.2 c from the input, reopened 0.4 c.
- 0 allocations in processBlock with graph mode, capture on and lines/curves/notes.
- CPU (Linux container, 128-sample blocks, 200 notes, capture on): 1.8 % / 1.9 % / 2.4 % at
  44.1 / 48 / 96 kHz — the same as Tracking alone.

## Feature audit (0.9.x) — note transitions, MIDI out, vibrato shapes

### Hard-tune note transitions (PSOLA)
- Measured before: with speed 0 and an abrupt A3 → C4 step, the output kept singing A3 for 17 ms (Tracking)
  and 24 ms (Live, Vintage) after the step (+latency). Cause: synthesis marks were spaced by the
  *detector's* period, which reports the new note ~27 ms after it starts (window centre + median + hop),
  while the analysis epochs (peak-snapped on the waveform) already followed the new period.
- Now each grain also knows the measured spacing of its epoch (mean of the spacings on both sides). When
  it differs from the detector's period by more than 6 % (~1 semitone) — i.e. on note changes, never on
  vibrato (≤ ~4 % even at ±100 c with the detector lag) — the synthesis uses the measured spacing. The
  previous correction ratio applies to the new note until the detector confirms it.
- After: within 60 c of the new note 0–6 ms after the step (Tracking at once; Live/Vintage 6 ms); exact
  (15 c) at once when the intonation error is the same before and after, 18–25 ms when it differs by
  50 c. Regression test: `TransitionTests` (3 modes × 2 intonation cases).
- Limit: steps larger than ~4 semitones fall outside the ±30 % epoch search and are followed when the
  detector catches up (as before).

### MIDI note output
- `midiOut` (append): the corrected note (target + transposition; nearest note when nothing is corrected)
  is sent as MIDI note on/off with velocity from the input level; a change must last 2 analysis frames
  (~5 ms). Events are queued on the audio thread in a fixed array and written at the end of the block
  (plugin wrappers pre-size their MIDI buffer: 0 allocations). The output never echoes the input MIDI.
  Bypass and switching it off send the pending note off. The plugin now declares MIDI output
  (`NEEDS_MIDI_OUTPUT TRUE`): VST3 event output bus, CLAP note output port, AU MIDI output.

### Other additions
- `midiTargetExact`: MIDI Target uses the held note in its own octave (no octave folding).
- `vibShape`: sine, triangle, softened square (tanh 5·sin) and sawtooth (rise over 96 % of the cycle, fall
  over 4 %: no click); all start at 0 at the note onset.
- Graph import: detail levels (min note 150/80/40 ms, smoothing ±80/40/15 ms, mapper hysteresis 0.25/
  0.15/0.08 st), import as curves (one curve per phrase, a point every 20 ms, pitch followed exactly),
  curve vibrato scaling at compile time around a ±90 ms running median (non-destructive, undoable).

## Phase 9 — key detection, instance sync, time correction

### KeyDetector (`dsp/KeyDetector.h`)
- Monophonic voice → the chromagram is a pitch-class histogram of the detected f0 frames, each weighted
  by duration × stability (1 at the centre of a semitone, 0 at ±0.4 st: glides count little) × a gentle
  level weight (10^(dB/40)).
- Pearson correlation with the 24 rotated Krumhansl–Schmuckler profiles; best key wins. Confidence = margin
  over the best key with different notes (the relative major/minor has the same notes, so for a tuner the
  choice between them does not change the correction).
- "Escuchar": the audio thread adds voiced frames to 12 relaxed atomics (single writer) until N voiced
  seconds (10/20/30) are heard; the processor's timer then writes key + Major/Natural minor to the
  parameters (message thread, with gestures). Also from the graphical mode's captured curve.
- Tests: melodies in C, D major and A, G, D minor; a sung E major phrase through the processor sets
  key = E, scale = Major.

### Instance sync (`plugin/InstanceRegistry.h`)
- In-process registry (one per plugin binary: VST3, CLAP and AU instances don't see each other). Thread-safe
  (mutex); "Sincronizar" broadcasts key, scale, the 12 note states and A4; receivers store them under a
  spin lock and apply them on their own message thread. Bridged instances (separate process) are alone
  and the button is disabled with an explanation.

### Captured audio (`plugin/graph/AudioCapture`)
- While "Capturar" is on and the host plays, the input (1–2 channels, after NaN sanitising, before any
  processing) goes through lock-free rings (4 s) + a header FIFO `{hostSample, n}` to a writer thread that
  writes 24-bit WAV files, one per contiguous pass (a position jump starts a new pass). Full rings drop
  the block (counted), never block.
- Files live outside the project, per user: Windows `%LOCALAPPDATA%\NovaPitch\Captures\<audioId>`,
  macOS `~/Library/Application Support/NovaPitch/Captures/<audioId>`, Linux
  `~/.local/share/NovaPitch/Captures/<audioId>`. The state stores `audioId` and the pass list (plugin
  state child `GraphAudio`); later passes win where they overlap. "Borrar la curva capturada" deletes them.
- While capturing, substitution is off: the capture always records the original input.

### Time map (`dsp/graph/TimeMap.h`) and model
- Piecewise-linear monotonic anchors (out, src), identity outside, ≥ 20 ms apart on both axes; stored as
  `Graph@warp` (undoable). Objects and the captured curve stay in **source time**; `compile()` maps them to
  output time, so a pitch edit stays on its syllable when the syllable moves. Phase-8 projects = identity.
- Editor "Tiempo" tool: a note's start/end become moving anchors, the previous note's start and the next
  note's end become fixed anchors (neighbours absorb the change); body = move, edge = stretch. Alt+click
  removes the edit around a note; "Quitar ediciones de tiempo" removes all (undoable).

### Rendering (`plugin/graph/TimeRenderer`)
- Background thread, 150 ms debounce after the last edit. Each warped range (+ 250 ms margins, merged) is
  read from the WAV passes (resampled with Lagrange if the session rate changed) and run through
  signalsmith-stretch (`presetDefault`, fixed seed) segment by segment with `process(in, srcLen, out,
  outLen)` — pitch untouched.
- Alignment: after `seek()` with the preceding audio, the output is delayed by inputLatency +
  outputLatency. The breakpoints are shifted by the input latency on both axes, so an event at source time
  s comes out at exactly toOutput(s) once that delay is removed (without the shift, events near anchors
  were off by up to ~15 ms).
- A phase vocoder keeps a phase offset after a stretch, so the switch back to the live input is placed at
  the quietest 10 ms window of the margin (≥ 80 ms after the last edited anchor) — normally the breath gap
  after the syllable. Before the edit the rendered audio equals the source (< −30 dB residual).
- Published like the correction track (`Publisher<RenderedTime>`). During playback (graph mode, transport
  running, not capturing) `RenderedTime::substitute` replaces the input sample-accurately by host position
  with 10 ms crossfades; the whole chain (detection, objects, engines, harmonies) then processes it. The
  project audio is never modified.

### Measured
- Attack moved 1.00 → 1.10 s: rendered at 1.1004 s; unedited attacks at 0.3007 / 1.9003 s; level of the
  stretched syllable within 12 %; pitch within 5 c.
- End to end (capture → import → move a syllable 100 ms and retune it to B3 → play): onset at 1.10 s + the
  10 ms latency (± 8 ms), the moved syllable < 8 c from B3, the stretched one < 8 c from A3; the same after
  save/reopen; undo restores the identity map; clearing deletes the WAV files.
- 0 allocations on the audio thread while capturing audio and while playing time edits.
- CPU (Linux container, 128-sample blocks): time-edit playback 2.6 / 2.1 / 2.4 % at 44.1 / 48 / 96 kHz
  (Tracking alone ≈ 1.9 %). Rendering runs on its own thread.

