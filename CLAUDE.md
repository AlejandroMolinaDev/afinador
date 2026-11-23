# CLAUDE.md — NovaPitch

Real-time monophonic pitch correction plugin (vocal tuner) in C++20 / JUCE 8.
Full spec: the original prompt document (phases 1–10). Current status: **Phase 6 done (presets, MIDI Learn, accent colour; awaiting the user's visual review)**. Next: phase 7 (harmonies + MIDI).
Roadmap and pending items: `docs/ROADMAP.md`.

## Priority: FL Studio on Windows AND macOS
Every decision is checked first against FL Studio (Windows x64 and macOS — Apple Silicon native and Intel/Rosetta).
Manual test checklist: `docs/TESTING_FL.md`.

### Frozen identifiers — NEVER change after a public release
FL Studio stores these in every saved project; changing them makes projects lose the plugin or its state.

| Item | Value | Where |
|---|---|---|
| Manufacturer code | `Nvpt` | `CMakeLists.txt` |
| Plugin code | `Npc1` | `CMakeLists.txt` |
| Bundle ID | `com.novapitch.novapitch` | `CMakeLists.txt` |
| CLAP ID | `com.novapitch.novapitch` | `CMakeLists.txt` |
| AU type | `aumf` (music effect, receives MIDI) | `CMakeLists.txt` |
| MIDI input | enabled (`NEEDS_MIDI_INPUT TRUE`) | `CMakeLists.txt` |
| Buses | main in + main out; Stereo→Stereo default, Mono→Mono, Mono→Stereo | `PluginProcessor.cpp` |
| Parameter IDs | stable strings + version hint, never renamed/reused | `src/plugin/Parameters.h` |
| State root | `NovaPitchState` + `stateVersion` attribute, migrations in `migrateState()` | `PluginProcessor.cpp` |

The product *name* (`PRODUCT_NAME`) can still change without breaking projects; the codes cannot.

### Other FL-specific rules
- `JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0`: otherwise JUCE adds ~2080 "MIDI CC" pseudo-parameters to the VST3 that clutter FL's parameter browser. MIDI notes still arrive (VST3 note events). CC control in VST3 = FL "Link to controller"; CLAP/AU receive CC as MIDI events.
- `processBlock` must handle `numSamples == 0` and irregular block sizes (FL does this). Never assume the size from `prepareToPlay`.
- Report latency with `setLatencySamples()` only from `prepareToPlay` / message thread (`updateReportedLatency()`); FL uses it for PDC.
- Windows: static MSVC runtime (`/MT`) so no VC++ redistributable is needed. Per-monitor DPI aware; verify UI scaling inside FL.
- macOS: Universal Binary (`arm64;x86_64`), deployment target 10.13, bundles must be signed (ad-hoc in dev/CI, Developer ID + notarization in phase 10).
- FL does **not** support ARA: any ARA feature needs a non-ARA path (capture-by-playback).
- MIDI into an effect in FL: VST3/CLAP effects receive MIDI via the wrapper's MIDI input port (document in user guide).

## Build & test
```bash
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release        # Linux/macOS (or -G Xcode / VS 2022)
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./tools/install_plugins.sh                                 # macOS/Linux user plugin folders
```
Windows: `cmake -B build -A x64` → `cmake --build build --config Release` → `ctest --test-dir build -C Release` → `tools\install_plugins.ps1` (admin, or `-UserFolder`).

Artefacts: `build/NovaPitch_artefacts/Release/{VST3,AU,CLAP,Standalone}`.

Automated host-level checks (no DAW needed):
```bash
build/tools/NovaPitchHostTest_artefacts/Release/NovaPitchHostTest build/NovaPitch_artefacts/Release/VST3/NovaPitch.vst3 --editor
build/tools/NovaPitchBench                     # CPU % of one core, per profile / sample rate
build/tools/NovaPitchSnapshot snapshots        # editor PNGs (100 % and 200 %) after a synthetic sung phrase
build/tools/NovaPitchRender in.wav out.wav retuneSpeed=0 key=9 scale=1   # offline render (no args: list params)
./tools/demo/run_demo.sh build demo-out        # synthetic off-key phrase -> renders -> pitch plot (numpy, matplotlib)
```
(On headless Linux prefix with `xvfb-run -a`.) The host test loads the *binary* through JUCE's hosting layer and runs FL-like scenarios: 6 sample rates × 4 max buffer sizes with random (and empty) blocks, parameter automation, MIDI, bypass toggling, latency (impulse) vs reported, save/reopen state, 8 instances, mono/stereo layouts, editor open/close while the audio thread runs.

Validators (downloaded to `tools/bin/`, git-ignored): pluginval v1.0.4 `--strictness-level 10 --validate-in-process --validate <plugin>`, clap-validator 0.3.2 `validate <plugin.clap>`, `auval -strict -v aumf Npc1 Nvpt` on macOS. On Linux headless use `xvfb-run -a`.

CMake options: `NOVAPITCH_BUILD_TESTS` (ON), `NOVAPITCH_BUILD_TOOLS` (ON), `NOVAPITCH_COPY_AFTER_BUILD` (OFF), `NOVAPITCH_ENABLE_CLAP` (ON), `NOVAPITCH_ENABLE_AAX` (OFF).

## Architecture
- `libs/` — git submodules pinned by commit: JUCE 8.0.15, clap-juce-extensions (main @ CLAP 1.2.10), signalsmith-stretch 1.4.0 + signalsmith-linear 0.6.4 (linear provided as a submodule so stretch's FetchContent is skipped; both marked SYSTEM), Catch2 v3.16.1.
- CMake INTERFACE libs: `NovaPitchDsp` (src/dsp + juce_dsp) ⊂ `NovaPitchCore` (all plugin sources). Linked by the plugin target `NovaPitch`, `NovaPitchTests` and the tools, so everything compiles exactly the plugin code.
- `src/dsp/` — pure DSP, no GUI: `NoteMapper` + `Scales` (key/scale/note states → target, hysteresis), `CorrectionCurve` (speed/tolerance/natural hold, in cents), `PsolaShifter` (streaming TD-PSOLA, fixed latency, identity on unvoiced, grain resampling for formants, grain scale for Vintage), `HqShifter` (signalsmith-stretch wrapper, 100 ms, formant base in f0/fs units, control queue aligned by inputLatency − detector lag, dry delay line), `VibratoAnalyzer` (own method: detrended autocorrelation, centre/vibrato split), `VibratoSynth` (artificial vibrato per note), `PitchDetector` (YIN-FFT + MPM validator, voicing, octave hysteresis, median; see `docs/DSP_NOTES.md`), `InputProfile` (f0 ranges, enum values stored in state: append only), `PitchUtils` (Hz/MIDI/cents/note names), `Biquad`.
- `src/plugin/` — `NovaPitchProcessor` (AudioProcessor + `clap_properties` with CLAP; runs the chain in 32-sample sub-blocks: mono sum → PitchDetector → (frame callback) NoteMapper → CorrectionCurve → PsolaShifter control; latency per mode, reported to the host from a message-thread timer when the mode changes; bypass keeps the latency-compensated dry path), `Parameters` (APVTS layout, IDs, state version), `PitchFrameFifo` (lock-free SPSC audio→UI), `PresetManager` (factory presets in code + user `.novapreset` XML; never touches key/scale/noteState/A4; current name in state), `MidiLearn` (CC→parameter, learnt on the audio thread with atomics, applied on the message thread by the processor's 30 Hz timer, saved in state as `MidiMap` by parameter ID).
- `src/ui/` — `PluginEditor` (fixed 760×640 `Content`, tabs Corrección / Formantes / Vibrato, scaled by AffineTransform: 75–200 %, crisp on HiDPI; 30 Hz timer drains the FIFO), `components/TuningMeter` (detected note, cents, target + correction), `components/PitchTrace` (5 s piano-roll: input vs corrected), `components/LabeledControls` (knob/combo/toggle bound to APVTS), `components/VibratoDisplay` (detected rate/depth + live oscillation), `NovaLookAndFeel` (original palette, configurable accent, custom knob/combo).
- `tests/unit/` — Catch2 tests (incl. `RealtimeSafetyTests`: replaces global operator new and asserts 0 allocations in processBlock for every mode); `tests/signals/SignalGenerators.h` — deterministic synthetic signals (sine, band-limited saw, weak fundamental, vibrato, glissando, noise).
- `tools/` — `hosttest` (FL-like host scenarios on the binary), `bench` (CPU), `snapshot` (UI PNGs), `render` (offline WAV render with any parameters, latency removed), `demo/` (Python: synthetic out-of-tune phrase → renders → pitch plot).
- `.github/workflows/build.yml` — Windows x64, macOS Universal, Linux x64: build, unit tests, host test, CPU bench (run summary), UI snapshots (artefact), pluginval 10, auval, clap-validator, plugin artefacts.

## Conventions
- Code, identifiers and comments in **English**. User docs (`README.md`, `docs/USER_GUIDE_es.md`, `docs/TESTING_FL.md`) and phase summaries in **Spanish**. UI strings in Spanish, always built with `juce::String::fromUTF8` (helper `u8()` in the editor).
- JUCE style formatting (space before parentheses in calls, Allman braces).
- **Real-time rule:** `processBlock` has no allocation, no blocking locks, no I/O, no logging. All buffers allocated in `prepareToPlay`. UI↔audio via atomics / `juce::AbstractFifo`.
- No silent TODOs: pending work goes to `docs/ROADMAP.md`.
- Legal: never use third-party trademarks or feature names (see spec section 1); original UI only; list every dependency in `THIRD_PARTY_LICENSES.md`.
- Work in phases; after each phase: build, test, commit, update this file, and stop for review.

## Decisions log
- Phase 1: submodules instead of CPM (reproducible, offline, recursive CLAP deps).
- Phase 1: JUCE pinned to 8.0.15 as the spec requires JUCE 8 (JUCE 9.x exists; evaluate migration later, see ROADMAP).
- Phase 1: clap-juce-extensions pinned to `main` (its tags are from 2022 and too old).
- Phase 1: MIDI input + `aumf` AU type enabled from day one to avoid breaking FL projects in phase 7.
- Phase 1: no parameters yet (pure passthrough).
- Phase 2: parameters `inputProfile` (choice, default Alto/Tenor) and `sensitivity` (0–100 %, default 50), version hint 1.
- Phase 2: pitch analysis runs at 20–40 kHz after decimation (the analysis signal is low-passed ≤ 8 kHz anyway): same accuracy, ~2–4× less CPU. Detection is on the mono sum so phase 3 can apply one ratio to both channels.
- Phase 2: YIN picks the minimum of the whole sub-threshold dip + sub-multiple check (fixes noise ripples and octave-down errors found while testing with noise).
- Phase 2: VST3 MIDI CC emulation disabled (see FL rules).
- Phase 2: automated host test instead of manual-only FL checks (FL can't run in CI); manual checklist still in `docs/TESTING_FL.md`.
- Phase 3: parameters appended (never inserted): qualityMode, key, scale, referenceA4, retuneSpeed, tolerance, naturalHold, transpose, mix, noteState0..11. `qualityMode` already has its 4 items (Live, Tracking, HQ, Vintage) so automation stays stable when HQ/Vintage get their own engines in phase 4.
- Phase 3: fixed latency per mode (Live 3 ms, Tracking 10 ms) rather than per profile, so FL's PDC only changes when the user changes the mode. Default mode: Tracking.
- Phase 3: choice parameters parse text robustly (exact, case-insensitive, UTF-8 mis-decoded as Latin-1 by the CLAP wrapper, numeric index) — fixes clap-validator's text round-trip on accented names.
- Phase 3: input samples are sanitised (NaN/Inf → 0) before the chain, output sanitised too.
- Phase 4: parameters appended: formantPreserve, formantShift, formantLink, character. MSVC gets `/bigobj` (signalsmith templates).
- Phase 4: HQ = signalsmith-stretch with a 90 ms block / 10 ms interval (100 ms latency): 45 ms biased the shifted pitch by up to +4 c; 90 ms brings it to ~+1.5 c within the spec's 50–120 ms. Per-mode latency is cached in `prepareToPlay` (HQ latency requires configuring an engine → never on the audio thread).
- Phase 4: Vintage = PSOLA with formants following pitch, grain scale 0.7, speed 0, no tolerance/hold.
- Phase 5: parameters appended: vibratoPreserve (default 100 %), vibRate, vibDepth (default 0 = off), vibDelay, vibVariation, vibAmpMod, vibFormantMod. NoteMapper/CorrectionCurve work on the vibrato-free centre; Vintage forces vibratoPreserve = 0.
- Phase 5: HQ needs control/audio alignment (detector lag vs STFT input latency); PSOLA doesn't (output period = detected period / ratio, same lag).
- Phase 5: tabbed UI pulled forward from phase 6 to keep the window compact (760×640).
- Phase 6: presets are "sound" presets (song settings excluded) — loading a preset mid-song never changes the key. User presets in `<Documents>/NovaPitch/Presets`.
- Phase 6: MIDI Learn values are applied from the message thread (processor timer) via setValueNotifyingHost, never from the audio thread. Mappings live in the project state.
- Phase 6: test names must be ASCII (CTest on Windows mangles non-ASCII filters); a meta test enforces it.
