#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace novapitch
{

// ---------------------------------------------------------------------------
// State versioning
// ---------------------------------------------------------------------------
// Bump kStateVersion whenever the serialised layout changes and add a migration
// step in migrateState() (PluginProcessor.cpp). Saved FL Studio projects must always load.
inline constexpr int kStateVersion = 1;
inline constexpr const char* kStateTreeType = "NovaPitchState";
inline constexpr const char* kStateVersionProperty = "stateVersion";

// ---------------------------------------------------------------------------
// Parameter IDs
// ---------------------------------------------------------------------------
// IDs are STABLE: never rename or reuse them, and only APPEND new parameters at
// the end of createParameterLayout() (some hosts/wrappers address parameters by
// index; FL Studio stores automation per parameter). Choice parameters only
// ever get new items appended.
namespace ParamIDs
{
inline constexpr const char* inputProfile = "inputProfile"; // phase 2
inline constexpr const char* sensitivity = "sensitivity";   // phase 2
inline constexpr const char* qualityMode = "qualityMode";   // phase 3
inline constexpr const char* key = "key";
inline constexpr const char* scale = "scale";
inline constexpr const char* referenceA4 = "referenceA4";
inline constexpr const char* retuneSpeed = "retuneSpeed";
inline constexpr const char* tolerance = "tolerance";
inline constexpr const char* naturalHold = "naturalHold";
inline constexpr const char* transpose = "transpose";
inline constexpr const char* mix = "mix";
inline constexpr const char* formantPreserve = "formantPreserve"; // phase 4
inline constexpr const char* formantShift = "formantShift";
inline constexpr const char* formantLink = "formantLink";
inline constexpr const char* character = "character";
inline constexpr const char* vibratoPreserve = "vibratoPreserve"; // phase 5
inline constexpr const char* vibRate = "vibRate";
inline constexpr const char* vibDepth = "vibDepth";
inline constexpr const char* vibDelay = "vibDelay";
inline constexpr const char* vibVariation = "vibVariation";
inline constexpr const char* vibAmpMod = "vibAmpMod";
inline constexpr const char* vibFormantMod = "vibFormantMod";
inline constexpr const char* midiTarget = "midiTarget";         // phase 7
inline constexpr const char* midiScaleLearn = "midiScaleLearn";
inline constexpr const char* hBusHpf = "hBusHpf";
inline constexpr const char* hBusLpf = "hBusLpf";
inline constexpr const char* hBusEqFreq = "hBusEqFreq";
inline constexpr const char* hBusEqGain = "hBusEqGain";
inline constexpr const char* hBusAttack = "hBusAttack";
inline constexpr const char* hBusRelease = "hBusRelease";
inline constexpr const char* hBusGate = "hBusGate";
inline constexpr const char* hBusWidth = "hBusWidth";
inline constexpr const char* graphMode = "graphMode";           // phase 8
inline constexpr const char* midiOut = "midiOut";               // feature audit (0.9.x)
inline constexpr const char* midiTargetExact = "midiTargetExact";
inline constexpr const char* vibShape = "vibShape";

/** Harmony voice parameters, voice = 1..4: "hvMode1", "hvDegrees1", ... */
juce::String harmonyVoice (const char* field, int voice);

/** "noteState0" (C) .. "noteState11" (B). */
juce::String noteState (int pitchClass);

/** "noteDetune0" (C) .. "noteDetune11" (B): per-note detune in cents (feature audit). */
juce::String noteDetune (int pitchClass);
} // namespace ParamIDs

/** Processing modes. Stored in state: append only. */
enum class QualityMode : int
{
    Live = 0,     ///< PSOLA, ~3 ms latency
    Tracking = 1, ///< PSOLA, ~10 ms latency (default)
    HQ = 2,       ///< signalsmith-stretch phase vocoder, ~60 ms latency
    Vintage = 3   ///< classic hard tune: dry PSOLA grains, formants follow pitch, instant steps
};

inline constexpr int kNumQualityModes = 4;
inline constexpr QualityMode kDefaultQualityMode = QualityMode::Tracking;

inline constexpr float kDefaultSensitivity = 50.0f;
inline constexpr float kDefaultRetuneMs = 20.0f;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace novapitch
