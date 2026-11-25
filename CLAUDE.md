# CLAUDE.md — NovaPitch

Real-time monophonic vocal pitch-correction plugin, C++20 / JUCE 8.0.15, VST3 / AU / CLAP / Standalone. **Target: FL Studio on Windows x64 and macOS (Apple Silicon + Intel/Rosetta).**
Status: version 0.9.0 (beta), phases 1–10 done (ARA2 postponed, AAX needs the Avid SDK). Next: the user's manual FL tests (`docs/TESTING_FL.md`; start with «Lo que queda para la prueba en FL» in `docs/TESTING_AUTOMATICO.md`), then 1.0.0. Pending on the user's side: Apple/Authenticode certificates, product licence (JUCE commercial vs AGPLv3).
More: `docs/ROADMAP.md` (pending work), `docs/DECISIONES.md` (full decisions log — append new decisions there), `docs/DSP_NOTES.md`.

## Frozen identifiers — never change (FL stores them in every project)
Manufacturer `Nvpt` · plugin `Npc1` · bundle / CLAP id `com.novapitch.novapitch` · AU type `aumf` · MIDI in + out enabled · buses main in/out (Stereo→Stereo default, Mono→Mono, Mono→Stereo) · parameter IDs and order (`src/plugin/Parameters.h`): **append only**, never rename / reuse / reorder · state root `NovaPitchState` + `stateVersion`, migrations in `migrateState()` · Windows installer AppId `{237AEEF3-3E15-4C10-9146-21EAD2F85A67}` · macOS pkg ids `com.novapitch.novapitch.pkg.{vst3,au,clap,app,docs}`. Guarded by `tests/unit/CompatibilityTests.cpp` (reference files in `tests/data/`: never edit them to make a test pass).

## Rules every change must respect
- **Real time:** `processBlock` never allocates, locks, does I/O or logs; buffers in `prepareToPlay`; UI↔audio via atomics / lock-free FIFOs. Handle `numSamples == 0` and irregular blocks. Latency reported only from `prepareToPlay` / message thread.
- **FL:** no VST3 MIDI-CC emulation (`JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0`); no ARA assumed (graph mode works by capture-on-playback); the editor never grabs keyboard focus by itself; static MSVC runtime; macOS Universal, 10.13+.
- **Presets / A/B change the sound only:** key, scale, note states/detunes, A4, `graphMode`, `midiOut` are song settings.
- **UI:** Spanish strings via `juce::String::fromUTF8` (`u8()`); no squashed text (`widthToFitText` for dense toolbars); every pop-up menu = `create*Menu()` + `*MenuItemChosen()`, calls `setLookAndFeel`, async callbacks hold a `SafePointer`.
- Code and comments in English, JUCE style (space before call parentheses, Allman braces). User docs in Spanish. No third-party trademarks. Dependencies in `THIRD_PARTY_LICENSES.md`. No silent TODOs (→ ROADMAP).
- Tests and tools write presets / captures to temp folders (`setUserFolder`, `setCaptureRoot`). Test names ASCII.

## Build & test
```bash
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
xvfb-run -a -s "-screen 0 1920x1600x24" ctest --test-dir build -j4 --output-on-failure   # Linux (no xvfb-run on Win/mac)
```
Windows: `cmake -B build -A x64`, `--config Release`, `ctest -C Release`. Artefacts: `build/NovaPitch_artefacts/Release/{VST3,AU,CLAP,Standalone}`.

Test layers (all in CI on Windows, macOS, Linux — `.github/workflows/build.yml`):
- `NovaPitchTests` (`tests/unit`, Catch2): DSP, processor, presets, MIDI, graph mode, state, compatibility; `RealtimeSafetyTests` asserts 0 allocations in `processBlock`.
- `NovaPitchUiTests` (`tests/ui`, label `ui`, RUN_SERIAL): the real editor in a frameless desktop window driven through its ComponentPeer (`UiHarness.h`: `Harness`, `Bot`, `waitFor`, `fitWidth`); layout checks at 75–200 % (`LayoutChecks.h`); private access via `UiTestAccess.h`; screenshots in `build/ui-shots`. Check timer-driven state with `waitFor`, never a fixed pump; size click-tested windows with `fitWidth` (CI screens can be 1024 × 768).
- `NovaPitchHostTest <plugin> --editor` (FL-like scenarios on the binary) and `--show-editor <dir>` (editor in a real host window at 100/200 %, screen capture on Linux).
- Validators (`tools/bin/`, git-ignored): pluginval 1.0.4 `--strictness-level 10 --validate-in-process`, clap-validator 0.3.2, `auval -strict -v aumf Npc1 Nvpt`.
- Tools: `NovaPitchBench` (CPU), `NovaPitchSnapshot` (UI PNGs), `NovaPitchRender` (offline render), `tools/demo/run_demo.sh`. Installers: `installer/windows/NovaPitch.iss`, `installer/macos/build_pkg.sh` (CI builds, installs, tests, uninstalls).
- Coverage of the manual FL checklist: `docs/TESTING_AUTOMATICO.md`.

## Architecture (where things are)
- `src/dsp/` pure DSP: `PitchDetector` (YIN + MPM), `NoteMapper` / `Scales`, `CorrectionCurve`, `PsolaShifter`, `HqShifter` (signalsmith-stretch), `Vibrato*`, `HarmonyEngine`, `KeyDetector`, `graph/{CorrectionTrack,TimeMap}`.
- `src/plugin/`: `NovaPitchProcessor` (32-sample sub-blocks: detector → mapper → curve → shifter), `Parameters` (96 params), `PresetManager` (factory + user, A/B, "*"), `MidiLearn`, `graph/*` (GraphModel, capture, time render), `InstanceRegistry`.
- `src/ui/`: `PluginEditor` (760×640 content scaled 75–200 %, 7 tabs, Auto/Gráfico), `components/*` (GraphEditor, NoteKeyboard, meter, trace…), `LookAndFeel`.
- `libs/` submodules: JUCE 8.0.15, clap-juce-extensions, signalsmith-stretch / linear, Catch2.

## Workflow
Build, run unit + UI tests + host test, commit, push to the session branch, check CI on the 3 platforms, record decisions in `docs/DECISIONES.md`.
