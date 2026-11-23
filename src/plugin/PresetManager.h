#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace novapitch
{

/** Factory and user presets (message thread only).

    - Factory presets are defined in code (stable across versions).
    - User presets are XML files (*.novapreset) in
      <Documents>/NovaPitch/Presets (or a folder given for tests).
    - Presets describe the *sound*: they never change key, scale, note
      states or the A4 reference (those belong to the song).
    - The current preset name is kept in the plugin state, so FL Studio
      projects reopen showing it. */
class PresetManager
{
public:
    struct Preset
    {
        juce::String name;
        bool factory = true;
        juce::File file; // user presets only
    };

    static constexpr const char* kFileExtension = ".novapreset";
    static constexpr const char* kPresetNameProperty = "presetName";
    static constexpr const char* kDefaultPresetName = "Por defecto";

    explicit PresetManager (juce::AudioProcessorValueTreeState&, juce::File userFolder = {});

    /** Factory presets first, then user presets (sorted by name). */
    std::vector<Preset> getPresets() const;

    bool loadPreset (const juce::String& name);
    /** Saves the current settings as a user preset (overwrites a user preset of the same name). */
    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);

    juce::String getCurrentPresetName() const;
    int getCurrentIndex() const;
    bool loadNext (int direction); // +1 / -1, wraps around

    juce::File getUserFolder() const { return userFolder; }
    static juce::File defaultUserFolder();

    /** Parameters that presets never touch (song settings). */
    static bool isSongParameter (const juce::String& paramId);

private:
    void applyValues (const std::vector<std::pair<juce::String, float>>& values);

    juce::AudioProcessorValueTreeState& state;
    juce::File userFolder;
};

} // namespace novapitch
