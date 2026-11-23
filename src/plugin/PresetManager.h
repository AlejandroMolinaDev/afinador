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

    // Sound snapshots ------------------------------------------------------------
    using Values = std::vector<std::pair<juce::String, float>>;
    /** Real values of every sound (non-song) parameter. */
    Values captureSound() const;

    // A/B comparison -------------------------------------------------------------
    /** Two slots of sound settings saved with the project. The current settings
        are the active slot; switching stores them and recalls the other one
        (an empty slot starts as a copy). Song settings are never touched. */
    static constexpr const char* kCompareTag = "ABCompare";
    int getActiveSlot() const; // 0 = A, 1 = B
    void selectSlot (int slot);
    /** Copies the active slot's settings (and preset name) into the other slot. */
    void copyActiveToOther();

    /** True when the sound differs from the current preset as loaded / saved. */
    bool isModified() const;

private:
    void applyValues (const std::vector<std::pair<juce::String, float>>& values);
    /** Full set of real values a preset sets (defaults for anything it doesn't list). */
    bool presetSound (const juce::String& name, Values& out) const;
    juce::ValueTree compareTree() const;
    void storeSlot (int slot);

    mutable juce::String cachedPresetName;
    mutable juce::Time cachedPresetTime;
    mutable Values cachedPresetValues;

    juce::AudioProcessorValueTreeState& state;
    juce::File userFolder;
};

} // namespace novapitch
