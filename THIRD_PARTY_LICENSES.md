# Third-party licenses

| Dependency | Version (pinned) | Used for | License |
|---|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) | 8.0.15 | Framework, plugin wrappers | Dual: JUCE commercial license (Starter/Indie/Pro) **or** AGPLv3. Some modules (juce_core, juce_audio_basics, juce_audio_devices, juce_events…) are ISC. |
| VST3 SDK (bundled with JUCE) | as shipped by JUCE 8.0.15 | VST3 format | MIT (since VST3 SDK 3.8) |
| [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) | `main` @ 3837f4a | CLAP format | MIT |
| [CLAP](https://github.com/free-audio/clap) | 1.2.10 | CLAP API headers | MIT |
| [clap-helpers](https://github.com/free-audio/clap-helpers) | as pinned by clap-juce-extensions | CLAP helpers | MIT |
| [Catch2](https://github.com/catchorg/Catch2) | v3.16.1 | Unit tests only (not shipped) | BSL-1.0 |

## CI-only tools (not shipped)
| Tool | Version | License |
|---|---|---|
| [pluginval](https://github.com/Tracktion/pluginval) | v1.0.4 | GPLv3 (tool only, not linked) |
| [clap-validator](https://github.com/free-audio/clap-validator) | 0.3.2 | MIT |

## Planned (added in later phases)
| Dependency | Phase | License |
|---|---|---|
| [signalsmith-stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) | 4 | MIT |
| Celemony ARA SDK 2.3.0 | 9 | Apache 2.0 |
| AAX SDK | 9/10 (optional) | Avid license agreement |
