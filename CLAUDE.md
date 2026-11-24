# CLAUDE.md — NovaPitch

Real-time monophonic pitch correction plugin (vocal tuner) in C++20 / JUCE 8.
Full spec: the original prompt document (phases 1–10). Current status: **Phase 10 done — version 0.9.0 (beta)**: installers (Windows Inno Setup, macOS Universal `.pkg`), Spanish user guide; all phases 1–10 delivered except ARA2 (postponed until after phase 10 by the user; FL has no ARA) and AAX (needs Avid SDK). Automated UI test campaign done (`docs/TESTING_AUTOMATICO.md`: 73 of the 114 FL checklist items fully automated, 33 partly, 8 FL/hardware only). Pending on the user's side: manual FL tests (`docs/TESTING_FL.md`, start with "Lo que queda para la prueba en FL"), Apple/Authenticode certificates, product licence (JUCE commercial vs AGPLv3). Next: whatever the user's FL testing reports, then 1.0.0.
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
| MIDI output | enabled (`NEEDS_MIDI_OUTPUT TRUE`, feature audit 0.9.x; empty unless "Enviar notas MIDI") | `CMakeLists.txt` |
| Buses | main in + main out; Stereo→Stereo default, Mono→Mono, Mono→Stereo | `PluginProcessor.cpp` |
| Parameter IDs | stable strings + version hint, never renamed/reused | `src/plugin/Parameters.h` |
| State root | `NovaPitchState` + `stateVersion` attribute, migrations in `migrateState()` | `PluginProcessor.cpp` |
| Windows installer AppId | `{237AEEF3-3E15-4C10-9146-21EAD2F85A67}` (upgrades/uninstall) | `installer/windows/NovaPitch.iss` |
| macOS package ids | `com.novapitch.novapitch.pkg.{vst3,au,clap,app,docs}` | `installer/macos/build_pkg.sh`, `distribution.xml` |

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
UI tests (`NovaPitchUiTests`, part of ctest, label `ui`, RUN_SERIAL): the real editor in a desktop window driven through its ComponentPeer (mouse, wheel, keys), automatic layout checks at 75–200 %, screenshots in `build/ui-shots` (or `$NOVAPITCH_UI_SHOTS`). On Linux they need a display: `xvfb-run -a -s "-screen 0 1920x1600x24" ctest --test-dir build` (without one they are reported as skipped). Real host window: `NovaPitchHostTest <plugin> --show-editor <dir>` (Linux also captures the screen with ImageMagick `import` and checks the plugin's colours).

(On headless Linux prefix with `xvfb-run -a`.) The host test loads the *binary* through JUCE's hosting layer and runs FL-like scenarios: 6 sample rates × 4 max buffer sizes with random (and empty) blocks, parameter automation, MIDI, bypass toggling, latency (impulse) vs reported, save/reopen state, 8 instances, mono/stereo layouts, editor open/close while the audio thread runs.

Installers (phase 10; CI builds, installs, tests and uninstalls them):
```bash
python3 tools/docs/build_guide.py docs/USER_GUIDE_es.md build/Guia_de_usuario.html     # needs `pip install markdown`
installer/macos/build_pkg.sh build/NovaPitch_artefacts/Release 0.9.0 build/Guia_de_usuario.html dist   # macOS (.pkg)
```
Windows: `iscc /DAppVersion=0.9.0 /DArtefactsDir=<abs>\build\NovaPitch_artefacts\Release /DDocsDir=<abs>\build /DOutputDir=<abs>\dist installer\windows\NovaPitch.iss` (Inno Setup 6; `.iss` is UTF-8 with BOM). macOS signing/notarization via `DEVELOPER_ID_APP`, `DEVELOPER_ID_INSTALLER`, `NOTARY_PROFILE` (GitHub secrets of the same names).

Validators (downloaded to `tools/bin/`, git-ignored): pluginval v1.0.4 `--strictness-level 10 --validate-in-process --validate <plugin>`, clap-validator 0.3.2 `validate <plugin.clap>`, `auval -strict -v aumf Npc1 Nvpt` on macOS. On Linux headless use `xvfb-run -a`.

CMake options: `NOVAPITCH_BUILD_TESTS` (ON), `NOVAPITCH_BUILD_TOOLS` (ON), `NOVAPITCH_COPY_AFTER_BUILD` (OFF), `NOVAPITCH_ENABLE_CLAP` (ON), `NOVAPITCH_ENABLE_AAX` (OFF).

## Architecture
- `libs/` — git submodules pinned by commit: JUCE 8.0.15, clap-juce-extensions (main @ CLAP 1.2.10), signalsmith-stretch 1.4.0 + signalsmith-linear 0.6.4 (linear provided as a submodule so stretch's FetchContent is skipped; both marked SYSTEM), Catch2 v3.16.1.
- CMake INTERFACE libs: `NovaPitchDsp` (src/dsp + juce_dsp) ⊂ `NovaPitchCore` (all plugin sources). Linked by the plugin target `NovaPitch`, `NovaPitchTests` and the tools, so everything compiles exactly the plugin code.
- `src/dsp/` — pure DSP, no GUI: `NoteMapper` + `Scales` (key/scale/note states → target, hysteresis), `CorrectionCurve` (speed/tolerance/natural hold, in cents), `PsolaShifter` (streaming TD-PSOLA, fixed latency, identity on unvoiced, grain resampling for formants, grain scale for Vintage), `HqShifter` (signalsmith-stretch wrapper, 100 ms, formant base in f0/fs units, control queue aligned by inputLatency − detector lag, dry delay line), `VibratoAnalyzer` (own method: detrended autocorrelation, centre/vibrato split), `VibratoSynth` (artificial vibrato per note), `HarmonyEngine` (4 PSOLA voices at the lead's latency: diatonic/semitone/MIDI, humanize, bus EQ/gate/width), `MidiNoteState` (held notes + learnt pitch classes, lock-free), `graph/CorrectionTrack` (immutable compiled graph objects, binary-search lookup by host time), `graph/TimeMap` (piecewise-linear output↔source time anchors), `KeyDetector` (f0 chromagram + Krumhansl–Schmuckler), `PitchDetector` (YIN-FFT + MPM validator, voicing, octave hysteresis, median; see `docs/DSP_NOTES.md`), `InputProfile` (f0 ranges, enum values stored in state: append only), `PitchUtils` (Hz/MIDI/cents/note names), `Biquad`.
- `src/plugin/` — `NovaPitchProcessor` (AudioProcessor + `clap_properties` with CLAP; runs the chain in 32-sample sub-blocks: mono sum → PitchDetector → (frame callback) NoteMapper → CorrectionCurve → PsolaShifter control; latency per mode, reported to the host from a message-thread timer when the mode changes; bypass keeps the latency-compensated dry path), `Parameters` (APVTS layout, IDs, state version), `PitchFrameFifo` (lock-free SPSC audio→UI), `PresetManager` (factory presets in code + user `.novapreset` XML; never touches key/scale/noteState/noteDetune/A4; current name in state; A/B slots and "*" modified marker), `MidiLearn` (CC→parameter, learnt on the audio thread with atomics, applied on the message thread by the processor's 30 Hz timer, saved in state as `MidiMap` by parameter ID), `graph/GraphModel` (graphical-mode objects in the state's `Graph` child + captured curve as binary, UndoManager, import from automatic, compile), `graph/GraphRuntime` (`CaptureFifo` audio→message thread, `Publisher<T>`: atomic pointer + graveyard freed after the audio thread completes 2 blocks), `graph/AudioCapture` (input → lock-free rings → writer thread → 24-bit WAV per pass in the user's capture folder), `graph/TimeRenderer` (background signalsmith-stretch render of the time edits, latency-shifted breakpoints, switch-back at the quietest point; `RenderedTime::substitute` on the audio thread), `InstanceRegistry` (in-process instances for "Sincronizar"). The processor reads the play head per block, stamps each analysis frame with its timeline time (minus detection lag), captures while the host plays and, with `graphMode`, takes the target from the track (outside objects / transport stopped: no correction). With time edits, the rendered audio replaces the input (sample-accurate by host position, 10 ms crossfades) before the chain; never while capturing.
- `src/ui/` — `PluginEditor` (fixed 760×640 `Content`, tabs Corrección / Notas / Formantes / Vibrato / Armonías / Bus / MIDI, scaled by AffineTransform: 75–200 %, crisp on HiDPI; 30 Hz timer drains the FIFO), `components/TuningMeter` (detected note, cents, target + correction), `components/PitchTrace` (5 s piano-roll: input vs corrected), `components/LabeledControls` (knob/combo/toggle bound to APVTS), `components/VibratoDisplay` (detected rate/depth + live oscillation), `components/NoteKeyboard` (12-note states, effective colours, live detected/target), `components/GraphEditor` (graphical mode: piano roll with captured curve, tools Select/Note/Line/Curve/Erase/Split, merge, undo/redo, capture/import/clear, follow/zoom, per-note inspector; the view follows the `graphMode` parameter via the header Auto/Gráfico switch), `NovaLookAndFeel` (original palette, configurable accent, custom knob/combo).
- `tests/ui/` — UI tests: `UiHarness.h` (Harness = processor + editor on the desktop, temp preset/capture folders; Bot = mouse/wheel/keys through the peer, checks every click lands on its control and raises the window first; real-time `feed`), `UiTestAccess.h` (friend of PluginEditor / GraphEditor / NoteKeyboard: private controls and menu builders/handlers), `LayoutChecks.h` (inside window/parent, no overlap, centre hit, text fits with the real fonts, min target size), `UiMain.cpp` (one JUCE session; `tests/support/XvfbSupport.h` creates the window-manager atoms a bare Xvfb lacks, shared with the host test).
- `tests/unit/` — Catch2 tests (incl. `RealtimeSafetyTests`: replaces global operator new and asserts 0 allocations on the audio thread in processBlock for every mode, graph mode, audio capture and time-edit playback; `GraphTests`, `KeySyncTests`, `TimeEditTests`); `tests/signals/SignalGenerators.h` — deterministic synthetic signals (sine, band-limited saw, weak fundamental, vibrato, glissando, noise).
- `tools/` — `hosttest` (FL-like host scenarios on the binary, incl. a graphical-mode note injected into the VST3 project state and played with a host transport), `bench` (CPU), `snapshot` (UI PNGs), `render` (offline WAV render with any parameters, latency removed), `demo/` (Python: synthetic out-of-tune phrase → renders → pitch plot).
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
- Phase 7: parameters appended: midiTarget, midiScaleLearn, hv{Mode,Degrees,Semitones,Formant,Pan,Level,Humanize,Mute,Solo}{1..4}, hBus{Hpf,Lpf,EqFreq,EqGain,Attack,Release,Gate,Width} (80 parameters in total).
- Phase 7: learnt MIDI scale is written into scale/noteState parameters from the message thread (visible, automatable, saved) instead of a hidden override.
- Phase 7: every parameter must round-trip text → value → text (hosts and clap-validator type values); a unit test checks all of them.
- Phase 8: parameter appended: `graphMode` (81 parameters). It is a song setting: presets never change it.
- Phase 8: graph objects and the captured curve live in the plugin state (`Graph` child, times in seconds of the host timeline, captured frames 16 bytes at 100 frames/s). Old projects have no `Graph` child: it is created on load, so no state-version bump.
- Phase 8: host time = play head `timeInSamples / fs` (fallback seconds). Capture and playback share that timebase, so constant offsets cancel; whether FL's PDC shifts it when the latency changes is a manual FL check (ROADMAP).
- Phase 8: the audio thread reads an immutable compiled track through an atomic pointer; retired tracks are freed on the message thread after 2 completed audio blocks (or in prepare/release). No locks, no allocation.
- Phase 8: graphical mode = objects only: outside objects or with the transport stopped the original voice passes. Lines/curves are followed exactly (speed 0, vibrato 0); notes use global or per-note speed / vibrato / formant.
- Phase 8: right click deletes objects (FL piano-roll convention); the editor view follows the `graphMode` parameter, so automation switches it.
- Phase 8: raw audio capture (waveform, time correction) deferred to phase 9.
- Phase 9: key detection = f0 pitch-class histogram (monophonic voice) + Krumhansl–Schmuckler; writes key + Major/Natural minor through the parameters (message thread).
- Phase 9: instance sync is in-process only (per plugin binary); bridged instances are alone and the button says so.
- Phase 9: captured audio is stored per user (LocalAppData / Application Support / ~/.local/share), not inside the project (FL projects would grow by MBs per minute); the state keeps `audioId` + pass list (`GraphAudio` child). Tests and tools must call `setCaptureRoot` with a temp folder.
- Phase 9: objects and the captured curve stay in source time; the time map (undoable `Graph@warp`) maps them to output time at compile, so pitch edits follow their syllables.
- Phase 9: time edits are rendered offline (background thread) and substituted for the input during playback, then processed by the normal chain — no second real-time engine, no extra latency.
- Phase 9: ARA2 postponed until after phase 10 (user decision, 2026-11-23).
- Phase 10: version 0.9.0 = feature-complete beta; 1.0.0 after the user's FL tests.
- Phase 10: installers put plugins in the system folders FL scans (`Common Files\{VST3,CLAP}`, `/Library/Audio/Plug-Ins/*`); macOS components are non-relocatable (all formats share one bundle id); user data (presets, captures) is never removed on uninstall.
- Phase 10: the user guide is Markdown (`docs/USER_GUIDE_es.md`) converted to one self-contained HTML (images embedded) shipped with the installers.
- Feature audit (0.9.x, `docs/AUDITORIA_FUNCIONES.md`): parameters appended `midiOut`, `midiTargetExact`, `vibShape` (84 parameters); `midiOut` is a song setting (presets never change it). The plugin declares MIDI output; `processBlock` clears the MIDI buffer after reading it (no MIDI thru) and writes only its own notes.
- Feature audit: PSOLA synthesis spacing follows the measured epoch spacing when it differs from the detector period by > 6 % (note transitions), so hard-tune note changes are immediate; regression test `TransitionTests`.
- Feature audit: 12-note state keyboard as a 7th tab "Notas" (second position); page indices shifted, the `Page` enum is the reference.
- Feature audit: parameters appended `noteDetune0..11` (±50 c per pitch class; 96 parameters). They are song settings (presets and A/B never change them) and travel with Sincronizar. NoteMapper decides and targets detuned positions; harmony voices use their own note's detune.
- Feature audit: A/B comparison in `PresetManager` (state child `ABCompare`, sound parameters only); "*" modified marker vs the current preset. The header host badge moved to the footer to make room for A/B.
- UI tests: every pop-up menu is built by a `create*Menu()` and handled by a `*MenuItemChosen()` (tests run exactly the click's code); async menu / dialog callbacks hold a `SafePointer` (the host may close the window with a menu or dialog open). JUCE menus don't inherit the target's LookAndFeel: every menu calls `setLookAndFeel`. Controls carry component IDs (parameter ID for knobs / combos / switches).
- UI tests harness rules (learnt from CI on Windows / macOS): test windows have no native frame (like a plugin view embedded in a host; a titled window gets OS size limits and screen clamping); everything a UI timer updates is checked with `waitFor`, never after a fixed `pump`; tests that click with the real mouse path size the window with `Harness::fitWidth` (CI screens can be 1024 × 768); `Bot` drags follow a path fixed in window coordinates (the component may move, e.g. the resize corner) and hide tooltips first, as a moving mouse does.
- UI tests: no text may be squashed (`kMinTextScale = 1`). Dense toolbars size their buttons with `widthToFitText` (fonts differ between Windows, macOS and Linux) and may use the compact 14 px font (`setCompact`). A knob's MIDI mapping shows as a "CCn" badge, not in the caption.
- Graphical mode: frames before the timeline start (detector look-back) are not captured; imports never create objects before 0; the time tool keeps the start of a note that has no room before it and stretches from its end.
- Phase 10: the plugin keeps `EDITOR_WANTS_KEYBOARD_FOCUS FALSE` so it never steals FL's shortcuts (space = play); the graph editor's shortcuts work when it has focus and every action also has a button.
