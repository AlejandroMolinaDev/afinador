# DSP notes

Technical notes, measurements and design decisions for the DSP modules.

## Latency per mode (to be measured)

| Mode | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| Passthrough (phase 1) | 0 | 0 | 0 | 0 |
| Live (PSOLA) | phase 3 | | | |
| Tracking (PSOLA) | phase 3 | | | |
| HQ | phase 4 | | | |
| Vintage | phase 4 | | | |

Values in samples (and ms). Reported latency must equal measured latency (impulse/correlation test).

## Modules
Sections for PitchDetector, NoteMapper, CorrectionCurve, PsolaShifter, HqShifter, FormantProcessor,
VibratoAnalyzer/Synth, HarmonyEngine and KeyDetector are added in their respective phases.
