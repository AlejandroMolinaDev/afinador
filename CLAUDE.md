# CLAUDE.md — NovaPitch

Real-time monophonic pitch correction plugin (vocal tuner) in C++20 / JUCE 8.
Full spec: the original prompt document (phases 1–10). Current status: **Phase 1 done (passthrough skeleton)**.
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
Windows: `cmake -B build -G "Visual Studio 17 2022" -A x64` → `cmake --build build --config Release` → `ctest --test-dir build -C Release` → `tools\install_plugins.ps1` (admin, or `-UserFolder`).

Artefacts: `build/NovaPitch_artefacts/Release/{VST3,AU,CLAP,Standalone}`.

Validators (downloaded to `tools/bin/`, git-ignored): pluginval v1.0.4 `--strictness-level 10 --validate-in-process --validate <plugin>`, clap-validator 0.3.2 `validate <plugin.clap>`, `auval -strict -v aumf Npc1 Nvpt` on macOS. On Linux headless use `xvfb-run -a`.

CMake options: `NOVAPITCH_BUILD_TESTS` (ON), `NOVAPITCH_COPY_AFTER_BUILD` (OFF), `NOVAPITCH_ENABLE_CLAP` (ON), `NOVAPITCH_ENABLE_AAX` (OFF).

## Architecture
- `libs/` — git submodules pinned by commit: JUCE 8.0.15, clap-juce-extensions (main @ CLAP 1.2.10), Catch2 v3.16.1.
- `NovaPitchCore` (CMake INTERFACE lib) holds all plugin sources; linked by the plugin target `NovaPitch` and by `NovaPitchTests`, so tests compile exactly the plugin code.
- `src/plugin/` — `NovaPitchProcessor` (AudioProcessor + `clap_properties` when building with CLAP), `Parameters` (APVTS layout, IDs, state version).
- `src/ui/` — `PluginEditor` (phase 1: host diagnostics panel, resizable 75–200 % fixed aspect, drawn in base coords and scaled), `NovaLookAndFeel` (original palette, configurable accent).
- `src/dsp/` — pure DSP, no GUI dependencies (empty in phase 1).
- `tests/unit/` — Catch2 tests, discovered by CTest.
- `.github/workflows/build.yml` — Windows x64, macOS Universal, Linux x64: build, tests, pluginval 10, auval, clap-validator, artefacts.

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
- Phase 1: no parameters yet (pure passthrough). First parameters arrive in phase 2/3 with versioned IDs.
