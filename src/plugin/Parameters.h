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
// IDs are STABLE: never rename, reorder or reuse them (FL Studio stores automation
// by parameter ID/index). New parameters are added with juce::ParameterID{ id, N }
// where N is the plugin version hint in which they were introduced.
namespace ParamIDs
{
inline constexpr const char* inputProfile = "inputProfile"; // phase 2
inline constexpr const char* sensitivity = "sensitivity";   // phase 2
} // namespace ParamIDs

inline constexpr float kDefaultSensitivity = 50.0f;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace novapitch
